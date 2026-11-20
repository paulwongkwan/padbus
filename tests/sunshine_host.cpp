// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file tests/sunshine_host.cpp
 * @brief Compile-and-run check that drives libpadbus the way a Sunshine-style host does.
 *
 * Sunshine (src/platform/windows/input.cpp) owns a single ViGEm client, attaches Xbox 360
 * and DualShock 4 targets, pushes one report per frame, reads rumble and lightbar feedback
 * on the driver's notification thread, polls DualShock 4 output reports, forwards battery
 * state, and asks the driver whether a target is still attached when a client resumes.
 * This check performs that sequence and asserts the behaviour the host depends on.
 *
 * The flow runs against the in-memory backend so it works on machines without the driver,
 * and repeats against the bus driver when one is installed.
 *
 * The translation unit is built as C++23, the standard Sunshine uses, which also proves the
 * headers are consumable from a newer standard than the library's C++17 baseline.
 */
#include <padbus/padbus.hpp>

// standard includes
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 202302L, "this host check must be compiled as C++23");
#else
static_assert(__cplusplus >= 202302L, "this host check must be compiled as C++23");
#endif

namespace {

  int failures = 0;

  void check(bool condition, const char *what) {
    if (!condition) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  }

  void check_eq(unsigned long long actual, unsigned long long expected, const char *what) {
    if (actual != expected) {
      std::printf("FAIL: %s (expected %llu, got %llu)\n", what, expected, actual);
      ++failures;
    }
  }

  /**
   * @brief Xbox 360 flow: attach, feed frames, consume feedback, detach.
   *
   * @param runtime Runtime to create the device on.
   * @param required Whether a creation failure is a test failure.
   */
  void run_xbox_360(padbus::Runtime &runtime, bool required) {
    auto created = runtime.create_gamepad(padbus::profiles::xbox_360());
    if (!created) {
      check(!required, "xbox 360 gamepad is created");
      return;
    }

    auto &gamepad = *created.gamepad;
    check(gamepad.profile().kind == padbus::GamepadProfileKind::xbox_360,
          "xbox 360 profile is reported back to the host");
    check(gamepad.is_attached(), "a freshly attached xbox 360 gamepad reports itself attached");

    // Sunshine registers one feedback callback per target before the first frame.
    std::vector<padbus::GamepadOutput> feedback;
    gamepad.set_output_callback([&feedback](const padbus::GamepadOutput &output) { feedback.push_back(output); });

    // A client frame: south face button, left stick pushed fully up, half right trigger.
    padbus::GamepadState state;
    state.buttons.set(padbus::GamepadButton::a, true);
    state.left_stick = {0.0F, 1.0F};
    state.right_trigger = 0.5F;
    check(gamepad.submit(state).ok(), "an xbox 360 frame submits");

    const auto report = gamepad.last_input_report();
    check_eq(report.size(), 12, "the xbox 360 report is twelve bytes");
    if (report.size() == 12) {
      check_eq(report[0] | (report[1] << 8), 0x1000, "the south face button reaches the report");
      check_eq(report[6] | (report[7] << 8), 0x7FFF, "stick up is positive, as the client sends it");
      check_eq(report[3], 128, "a half pull reaches the trigger byte");
    }

    // The driver reports rumble requests on its own thread; the host converts them back to
    // 8-bit motor values for the client.
    padbus::GamepadOutput rumble;
    rumble.kind = padbus::GamepadOutputKind::rumble;
    rumble.low_frequency_rumble = 0xFFFF;
    rumble.high_frequency_rumble = 0x8000;
    check(gamepad.dispatch_output(rumble).ok(), "rumble feedback dispatches");
    check(!feedback.empty(), "rumble feedback reaches the host callback");
    if (!feedback.empty()) {
      check_eq(feedback.back().kind == padbus::GamepadOutputKind::rumble, true, "rumble reaches the callback as rumble");
      check_eq(feedback.back().low_frequency_rumble, 0xFFFF, "the large motor value reaches the callback");
    }

    // The LED ring change arrives through the same callback.
    padbus::GamepadOutput led;
    led.kind = padbus::GamepadOutputKind::player_leds;
    led.player_leds[1] = true;
    check(gamepad.dispatch_output(led).ok(), "player led feedback dispatches");
    check(feedback.back().kind == padbus::GamepadOutputKind::player_leds,
          "player led feedback reaches the host callback");

    // Sunshine reads the XInput slot so feedback can be attributed to a client. Without the
    // driver this reports -1, with it the slot number.
    check(gamepad.xinput_user_index() >= -1, "the xinput user index query succeeds");

    check(gamepad.close().ok(), "the xbox 360 gamepad detaches");
    check(!gamepad.is_attached(), "a detached xbox 360 gamepad reports itself detached");
  }

  /**
   * @brief DualShock 4 flow: attach, feed a full frame, consume lightbar, detach.
   *
   * @param runtime Runtime to create the device on.
   * @param required Whether a creation failure is a test failure.
   */
  void run_dualshock4(padbus::Runtime &runtime, bool required) {
    auto created = runtime.create_gamepad(padbus::profiles::dualshock4());
    if (!created) {
      check(!required, "dualshock 4 gamepad is created");
      return;
    }

    auto &gamepad = *created.gamepad;
    check(gamepad.profile().kind == padbus::GamepadProfileKind::dualshock4,
          "dualshock 4 profile is reported back to the host");
    check(gamepad.is_attached(), "a freshly attached dualshock 4 gamepad reports itself attached");

    std::vector<padbus::GamepadOutput> feedback;
    gamepad.set_output_callback([&feedback](const padbus::GamepadOutput &output) { feedback.push_back(output); });

    // A Sunshine frame: light trigger pressure, a centered touch, motion samples and the
    // battery level forwarded from the client.
    padbus::GamepadState state;
    state.buttons.set(padbus::GamepadButton::a, true);
    state.left_trigger = 0.02F;
    state.gyroscope = padbus::Vector3 {1024.0F, 0.0F, 0.0F};
    state.acceleration = padbus::Vector3 {0.0F, 8192.0F, 0.0F};
    state.touchpad_contacts[0].active = true;
    state.touchpad_contacts[0].id = 1;
    state.touchpad_contacts[0].x = 0.5F;
    state.touchpad_contacts[0].y = 0.5F;
    state.battery = padbus::GamepadBattery {75, true};
    check(gamepad.submit(state).ok(), "a dualshock 4 frame submits");

    const auto report = gamepad.last_input_report();
    check_eq(report.size(), 63, "the dualshock 4 report is sixty-three bytes");
    if (report.size() == 63) {
      const auto buttons = static_cast<std::uint16_t>(report[4] | (report[5] << 8));
      check_eq(buttons & 0x0400u, 0x0400u, "light trigger pressure sets the digital trigger bit");
      check_eq(report[29], 0x18, "the battery status carries charge and cable state");
      check_eq(report[11], 191, "the battery level byte carries the percentage");
      check_eq(report[13], 0x04, "the gyro sample reaches the report");
      check_eq(report[21], 0x20, "the accelerometer sample reaches the report");
      check_eq(report[32], 1, "one touch packet is published");
      check_eq(report[34], 1, "the touch contact id reaches the report");
      check_eq(report[35], 0xC0, "the touch x low byte reaches the report");
      check_eq(report[36], 0x73, "the touch x high nibble and y low nibble reach the report");
      check_eq(report[37], 0x1D, "the touch y high byte reaches the report");
    }

    // Applications that require a moving timestamp keep accepting the pad.
    const auto first_timestamp = static_cast<std::uint16_t>(report[9] | (report[10] << 8));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(gamepad.submit(state).ok(), "the dualshock 4 frame repeats");
    const auto repeated = gamepad.last_input_report();
    const auto second_timestamp = static_cast<std::uint16_t>(repeated[9] | (repeated[10] << 8));
    check(second_timestamp != first_timestamp, "the report timestamp advances between frames");

    // Sunshine polls the output report for lightbar and rumble requests.
    const auto raw = gamepad.poll_raw_output(0);
    check(!raw.has_value() || raw->kind == padbus::GamepadOutputKind::raw_report,
          "a raw output poll returns a report of the expected kind");

    // The lightbar arrives as its own feedback event.
    padbus::GamepadOutput lightbar;
    lightbar.kind = padbus::GamepadOutputKind::rgb_led;
    lightbar.red = 10;
    lightbar.green = 20;
    lightbar.blue = 30;
    check(gamepad.dispatch_output(lightbar).ok(), "lightbar feedback dispatches");
    check(!feedback.empty(), "lightbar feedback reaches the host callback");
    if (!feedback.empty()) {
      check_eq(feedback.back().kind == padbus::GamepadOutputKind::rgb_led, true, "the lightbar arrives as its own event");
      check_eq(feedback.back().red, 10, "the lightbar red value reaches the callback");
    }

    // Sunshine rebinds a resumed client to an existing target instead of recreating it.
    check(gamepad.is_attached(), "the target stays attached for a resumed client");

    check(gamepad.close().ok(), "the dualshock 4 gamepad detaches");
    check(!gamepad.is_attached(), "a detached dualshock 4 gamepad reports itself detached");
  }

}  // namespace

int main() {
  // The host flow always runs against the in-memory backend, so it works on machines
  // without the bus driver.
  padbus::RuntimeOptions fake_options;
  fake_options.backend = padbus::BackendKind::fake;

  auto fake = padbus::Runtime::create(fake_options);
  check(fake != nullptr, "the in-memory runtime is created");
  if (fake) {
    run_xbox_360(*fake, true);
    run_dualshock4(*fake, true);
    check_eq(fake->active_device_count(), 0, "every device was released");
  }

  // The same flow repeats against the bus driver when one is installed, which is the path
  // a real Sunshine host takes.
  auto platform = padbus::Runtime::create({});
  check(platform != nullptr, "the platform runtime is created");
  if (platform) {
    if (platform->capabilities().available) {
      std::printf("driver backend available, exercising it as well\n");
      run_xbox_360(*platform, true);
      run_dualshock4(*platform, true);
    } else {
      // CI runs with PADBUS_REQUIRE_DRIVER set so a missing driver fails the build instead of
      // silently skipping the only pass that touches real hardware.
      const auto requirement = std::getenv("PADBUS_REQUIRE_DRIVER");
      const bool required = requirement != nullptr && std::string(requirement) != "0" && *requirement != '\0';
      if (required) {
        check(false, "the driver backend is required by PADBUS_REQUIRE_DRIVER but unavailable");
      } else {
        std::printf("driver backend unavailable (%s), skipping the live pass\n",
                    platform->capabilities().backend_name.c_str());
      }
    }
  }

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }

  std::printf("all checks passed\n");
  return 0;
}
