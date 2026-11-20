// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file tests/api_smoke.cpp
 * @brief Behavioural checks for the modern ViGEm client API.
 *
 * The checks run against the in-memory backend so report packing, status
 * handling, callbacks, and the adapter are exercised without the bus driver.
 */
#include <vigem/vigem.hpp>

// standard includes
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

  int failures = 0;

  void check(bool condition, const std::string &description) {
    if (!condition) {
      std::printf("FAIL: %s\n", description.c_str());
      ++failures;
    }
  }

  void check_eq(std::uint64_t actual, std::uint64_t expected, const std::string &description) {
    if (actual != expected) {
      std::printf("FAIL: %s (expected %llu, got %llu)\n", description.c_str(),
                  static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual));
      ++failures;
    }
  }

  vigem::RuntimeOptions fake_options() {
    vigem::RuntimeOptions options;
    options.backend = vigem::BackendKind::fake;
    return options;
  }

  void test_runtime_and_profiles() {
    auto runtime = vigem::Runtime::create(fake_options());
    check(runtime != nullptr, "fake runtime is created");
    check(runtime->backend_kind() == vigem::BackendKind::fake, "runtime reports the fake backend");
    check(runtime->capabilities().available, "fake backend is available");
    check(runtime->capabilities().supports_gamepad, "fake backend supports gamepads");

    const auto xbox = vigem::profiles::xbox_360();
    const auto ds4 = vigem::profiles::dualshock4();
    check_eq(xbox.vendor_id, 0x045E, "xbox 360 vendor id");
    check_eq(xbox.product_id, 0x028E, "xbox 360 product id");
    check_eq(ds4.vendor_id, 0x054C, "dualshock 4 vendor id");
    check(xbox.capabilities.supports_rumble, "xbox 360 supports rumble");
    check(ds4.capabilities.supports_touchpad, "dualshock 4 supports the touchpad");
    check(!xbox.capabilities.supports_touchpad, "xbox 360 has no touchpad");
    check_eq(vigem::profiles::built_in_gamepad_profiles().size(), 2, "two built-in profiles");

    const auto support = vigem::gamepad_profile_support(ds4);
    check(support.supports_touchpad_button, "ds4 profile reports a touchpad button");
    check(!vigem::supports_gamepad_button(xbox, vigem::GamepadButton::touchpad), "xbox 360 has no touchpad button");
    check(vigem::supports_gamepad_output(ds4, vigem::GamepadOutputKind::rgb_led), "ds4 emits lightbar output");
    check(!vigem::supports_gamepad_output(xbox, vigem::GamepadOutputKind::rgb_led), "xbox 360 has no lightbar");
    check(vigem::supports_gamepad_output(xbox, vigem::GamepadOutputKind::player_leds), "xbox 360 emits player leds");
  }

  void test_xbox_report_packing() {
    auto runtime = vigem::Runtime::create(fake_options());
    auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
    check(static_cast<bool>(created), "xbox 360 gamepad is created");
    if (!created) {
      return;
    }

    auto &gamepad = *created.gamepad;
    check(gamepad.is_open(), "xbox 360 gamepad starts open");
    check_eq(gamepad.device_id(), 1, "first device id is one");
    check_eq(runtime->active_device_count(), 1, "runtime tracks one active device");

    vigem::GamepadState state;
    state.buttons.set(vigem::GamepadButton::a);
    state.buttons.set(vigem::GamepadButton::dpad_up);
    state.buttons.set(vigem::GamepadButton::left_shoulder);
    state.left_stick = {1.0F, -1.0F};
    state.left_trigger = 1.0F;
    state.right_trigger = 0.5F;

    const auto status = gamepad.submit(state);
    check(status.ok(), "xbox 360 submit succeeds");
    check_eq(gamepad.submit_count(), 1, "submit count is tracked");

    const auto report = gamepad.last_input_report();
    check_eq(report.size(), 12, "xbox 360 report is 12 bytes");
    if (report.size() == 12) {
      check_eq(report[0] | (report[1] << 8), 0x1101, "xbox 360 button mask");
      check_eq(report[2], 255, "xbox 360 left trigger byte");
      check_eq(report[3], 128, "xbox 360 right trigger byte");
      check_eq(static_cast<std::uint16_t>(report[4] | (report[5] << 8)), 0x7FFF, "xbox 360 left stick x");
      check_eq(static_cast<std::uint16_t>(report[6] | (report[7] << 8)), 0x8001, "xbox 360 left stick y");
    }

    // Values outside the normalized range are clamped instead of wrapping.
    vigem::GamepadState extreme;
    extreme.buttons.set(vigem::GamepadButton::b);
    extreme.left_stick = {5.0F, -5.0F};
    extreme.left_stick.y = -5.0F;
    extreme.right_trigger = 2.0F;
    extreme.right_stick.x = std::nanf("");
    check(gamepad.submit(extreme).ok(), "clamped submit succeeds");
    const auto clamped = gamepad.last_input_report();
    if (clamped.size() == 12) {
      check_eq(static_cast<std::uint16_t>(clamped[4] | (clamped[5] << 8)), 0x7FFF, "stick is clamped at 1.0");
      check_eq(static_cast<std::uint16_t>(clamped[6] | (clamped[7] << 8)), 0x8001, "stick is clamped at -1.0");
      check_eq(clamped[3], 255, "trigger is clamped at 1.0");
      check_eq(static_cast<std::uint16_t>(clamped[8] | (clamped[9] << 8)), 0, "non-finite axis becomes neutral");
      check_eq(clamped[0] | (clamped[1] << 8), 0x2000, "second report button mask");
    }

    // The normalized vertical axis is positive-up, and the Xbox 360 report carries it
    // as a signed value, so no inversion happens on this profile.
    vigem::GamepadState vertical;
    vertical.left_stick = {0.0F, 1.0F};
    check(gamepad.submit(vertical).ok(), "vertical stick submit succeeds");
    const auto vertical_report = gamepad.last_input_report();
    if (vertical_report.size() == 12) {
      check_eq(static_cast<std::uint16_t>(vertical_report[6] | (vertical_report[7] << 8)), 0x7FFF,
               "xbox 360 up is positive");
      check_eq(static_cast<std::uint16_t>(vertical_report[4] | (vertical_report[5] << 8)), 0,
               "xbox 360 centered horizontal axis");
    }

    check(gamepad.close().ok(), "xbox 360 gamepad closes");
    check(!gamepad.is_open(), "closed gamepad reports closed");
    check_eq(runtime->active_device_count(), 0, "closed device leaves the active count");
    check_eq(static_cast<int>(gamepad.submit(state).code()), static_cast<int>(vigem::ErrorCode::device_closed),
             "submit after close reports device_closed");
    check(gamepad.close().ok(), "close is idempotent");
  }

  void test_dualshock4_report_packing() {
    auto runtime = vigem::Runtime::create(fake_options());
    auto created = runtime->create_gamepad(vigem::profiles::dualshock4());
    check(static_cast<bool>(created), "dualshock 4 gamepad is created");
    if (!created) {
      return;
    }

    auto &gamepad = *created.gamepad;
    vigem::GamepadState state;
    state.buttons.set(vigem::GamepadButton::a);
    state.buttons.set(vigem::GamepadButton::dpad_up);
    state.buttons.set(vigem::GamepadButton::touchpad);
    state.left_stick = {1.0F, -1.0F};
    state.left_trigger = 1.0F;
    state.right_trigger = 0.5F;
    state.acceleration = vigem::Vector3 {1.0F, 2.0F, 3.0F};
    state.gyroscope = vigem::Vector3 {4.0F, 5.0F, 6.0F};
    state.touchpad_contacts[0].id = 1;
    state.touchpad_contacts[0].active = true;
    state.touchpad_contacts[0].x = 0.5F;
    state.touchpad_contacts[0].y = 0.5F;

    check(gamepad.submit(state).ok(), "dualshock 4 submit succeeds");
    const auto report = gamepad.last_input_report();
    check_eq(report.size(), 63, "dualshock 4 report is 63 bytes");
    if (report.size() == 63) {
      check_eq(report[0], 255, "dualshock 4 left stick x");
      // The DualShock 4 axis bytes count down from the top, so a stick pushed down is 255.
      check_eq(report[1], 255, "dualshock 4 left stick y is inverted for a stick pushed down");
      check_eq(report[2], 128, "dualshock 4 right stick x center");
      check_eq(report[3], 128, "dualshock 4 right stick y center");

      const auto buttons = static_cast<std::uint16_t>(report[4] | (report[5] << 8));
      check_eq(buttons & 0x0020u, 0x0020u, "cross button bit");
      check_eq(buttons & 0x000Fu, 0x0u, "dpad north hat value");
      check_eq(buttons & 0x0400u, 0x0400u, "digital left trigger bit");
      check_eq(buttons & 0x0800u, 0x0800u, "digital right trigger bit");

      check_eq(report[6], 0x02, "touchpad special button");
      check_eq(report[7], 255, "dualshock 4 left trigger byte");
      check_eq(report[8], 128, "dualshock 4 right trigger byte");
      check_eq(report[11], 0x06, "dualshock 4 temperature byte");
      check_eq(report[29], 0x1B, "dualshock 4 status byte reports wired and full");

      check_eq(report[12] | (report[13] << 8), 4, "gyroscope x");
      check_eq(report[18] | (report[19] << 8), 1, "accelerometer x");
      check_eq(report[22] | (report[23] << 8), 3, "accelerometer z");

      check_eq(report[32], 1, "one touch packet is published");
      check_eq(report[33], 0, "touch tracking counter");
      check_eq(report[34], 1, "touch contact id");
      check_eq(report[35], 0xC0, "touch x low byte");
      check_eq(report[36], 0x73, "touch x high nibble and y low nibble");
      check_eq(report[37], 0x1D, "touch y high byte");

      // The touchpad spans 1920x943, so a full-scale contact reaches the extreme
      // addressable coordinates instead of overshooting the panel.
      state.touchpad_contacts[0].x = 1.0F;
      state.touchpad_contacts[0].y = 1.0F;
      check(gamepad.submit(state).ok(), "full-scale contact submit succeeds");
      const auto extreme = gamepad.last_input_report();
      check_eq(extreme[35], 0x7F, "touch x low byte at the panel edge");
      check_eq(extreme[36], 0xE7, "touch x high nibble at the panel edge");
      check_eq(extreme[37], 0x3A, "touch y high byte at the panel edge");

      // A lifted contact keeps one packet and marks the tracking number as up.
      state.touchpad_contacts[0].active = false;
      check(gamepad.submit(state).ok(), "lifted-contact submit succeeds");
      const auto lifted = gamepad.last_input_report();
      check_eq(lifted[32], 1, "a lifted contact still publishes one touch packet");
      check_eq(lifted[34], 0x80, "lifted contact reports the up flag");

      // Any reported pressure sets the digital trigger bit; a released trigger clears it.
      vigem::GamepadState light;
      light.left_trigger = 0.02F;
      light.right_trigger = 1.0F / 255.0F;
      check(gamepad.submit(light).ok(), "light-trigger submit succeeds");
      const auto light_report = gamepad.last_input_report();
      const auto light_buttons = static_cast<std::uint16_t>(light_report[4] | (light_report[5] << 8));
      check_eq(light_buttons & 0x0400u, 0x0400u, "light left-trigger pressure sets the digital bit");
      check_eq(light_buttons & 0x0800u, 0x0800u, "light right-trigger pressure sets the digital bit");
      check_eq(light_report[7], 5, "light left trigger keeps its analog value");
      check_eq(light_report[8], 1, "light right trigger keeps its analog value");

      vigem::GamepadState released;
      check(gamepad.submit(released).ok(), "released-trigger submit succeeds");
      const auto released_report = gamepad.last_input_report();
      const auto released_buttons = static_cast<std::uint16_t>(released_report[4] | (released_report[5] << 8));
      check_eq(released_buttons & 0x0C00u, 0u, "released triggers clear both digital bits");
    }

    // Vertical direction follows the same rule: up is zero on the DualShock 4 byte.
    vigem::GamepadState vertical;
    vertical.left_stick = {0.0F, 1.0F};
    vertical.right_stick = {0.0F, -1.0F};
    check(gamepad.submit(vertical).ok(), "vertical stick submit succeeds");
    const auto vertical_report = gamepad.last_input_report();
    check_eq(vertical_report[1], 0, "dualshock 4 up is zero");
    check_eq(vertical_report[3], 255, "dualshock 4 down is 255");

    // The report timestamp advances in 5.333 us units, which applications that require
    // a moving DualShock 4 timestamp depend on.
    vigem::GamepadState repeated;
    check(gamepad.submit(repeated).ok(), "repeated submit succeeds");
    const auto first_report = gamepad.last_input_report();
    const auto first_timestamp = static_cast<std::uint16_t>(first_report[9] | (first_report[10] << 8));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(gamepad.submit(repeated).ok(), "second repeated submit succeeds");
    const auto later_report = gamepad.last_input_report();
    const auto second_timestamp = static_cast<std::uint16_t>(later_report[9] | (later_report[10] << 8));
    check(second_timestamp != first_timestamp, "dualshock 4 timestamp advances between reports");

    // Declared battery state is published in both bytes the driver exposes.
    vigem::GamepadState half_charge;
    half_charge.battery = vigem::GamepadBattery {50, true};
    check(gamepad.submit(half_charge).ok(), "battery submit succeeds");
    const auto half_report = gamepad.last_input_report();
    check_eq(half_report[29], 0x15, "half charge on cable sets the status nibble");
    check_eq(half_report[11], 127, "battery level byte carries the percentage");

    vigem::GamepadState full_charge;
    full_charge.battery = vigem::GamepadBattery {100, false};
    check(gamepad.submit(full_charge).ok(), "full battery submit succeeds");
    const auto full_report = gamepad.last_input_report();
    check_eq(full_report[29], 0x0A, "full battery clears the cable bit");
    check_eq(full_report[11], 255, "full battery level byte");

    check(gamepad.is_attached(), "fake device reports itself attached");
    check(!gamepad.poll_raw_output(0).has_value(), "fake backend publishes no raw reports");
    check(gamepad.xinput_user_index() == -1, "dualshock 4 has no xinput index");

    check(gamepad.close().ok(), "dualshock 4 gamepad closes");
    check(!gamepad.is_attached(), "closed device is no longer attached");
  }

  void test_output_dispatch() {
    auto runtime = vigem::Runtime::create(fake_options());
    auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
    if (!created) {
      check(false, "output dispatch device is created");
      return;
    }

    auto &gamepad = *created.gamepad;
    std::vector<vigem::GamepadOutput> received;
    gamepad.set_output_callback([&received](const vigem::GamepadOutput &output) { received.push_back(output); });

    vigem::GamepadOutput rumble;
    rumble.kind = vigem::GamepadOutputKind::rumble;
    rumble.low_frequency_rumble = 65535;
    rumble.high_frequency_rumble = 32768;
    check(gamepad.dispatch_output(rumble).ok(), "output dispatch succeeds");
    check_eq(received.size(), 1, "one output event is delivered");
    check_eq(received[0].low_frequency_rumble, 65535, "low frequency rumble is forwarded");

    // A throwing callback is disabled after its first failure instead of interrupting
    // input delivery, and it must not run again afterwards.
    int throw_count = 0;
    gamepad.set_output_callback([&throw_count](const vigem::GamepadOutput &) {
      ++throw_count;
      throw std::runtime_error("boom");
    });
    check(gamepad.dispatch_output(rumble).ok(), "throwing callback does not fail dispatch");
    check(gamepad.dispatch_output(rumble).ok(), "dispatch after a thrown callback still succeeds");
    check_eq(throw_count, 1, "throwing callback is disabled after it throws");

    received.clear();
    gamepad.set_output_callback({});
    check(gamepad.dispatch_output(rumble).ok(), "dispatch without a callback succeeds");
    check_eq(received.size(), 0, "cleared callback receives nothing");
  }

  void test_adapter_incremental_updates() {
    auto runtime = vigem::Runtime::create(fake_options());
    auto created = vigem::GamepadStateAdapter::create(*runtime, vigem::profiles::dualshock4());
    check(static_cast<bool>(created), "adapter is created");
    if (!created) {
      return;
    }

    auto &adapter = *created.adapter;
    check(adapter.is_open(), "adapter reports an open gamepad");

    check(adapter.set_button(vigem::GamepadButton::x, true).ok(), "adapter button update succeeds");
    check(adapter.set_right_trigger(1.0F).ok(), "adapter trigger update succeeds");
    check(adapter.set_left_stick({-0.5F, 0.25F}).ok(), "adapter stick update succeeds");
    vigem::GamepadTouchContact contact;
    contact.id = 4;
    contact.active = true;
    contact.x = 0.25F;
    contact.y = 0.75F;
    check(adapter.set_touchpad_contact(1, contact).ok(), "adapter contact update succeeds");
    check(adapter.state().buttons.test(vigem::GamepadButton::x), "adapter caches buttons across updates");
    check(adapter.state().right_trigger == 1.0F, "adapter caches triggers across updates");

    check_eq(adapter.gamepad()->submit_count(), 4, "every adapter update submits a full report");
    check_eq(adapter.gamepad()->last_input_report().size(), 63, "adapter reports use the profile layout");

    check_eq(static_cast<int>(adapter.set_touchpad_contact(5, contact).code()),
             static_cast<int>(vigem::ErrorCode::invalid_argument), "out-of-range contact index is rejected");

    check(adapter.clear_motion().ok(), "adapter clears motion data");
    check(!adapter.state().gyroscope.has_value(), "motion data is cleared");

    check(adapter.close().ok(), "adapter closes its gamepad");
    check(!adapter.is_open(), "adapter reports the closed gamepad");
    check_eq(runtime->active_device_count(), 0, "adapter close releases the device");
  }

  void test_runtime_lifecycle() {
    auto runtime = vigem::Runtime::create(fake_options());
    auto first = runtime->create_gamepad(vigem::profiles::xbox_360());
    auto second = runtime->create_gamepad(vigem::profiles::dualshock4());
    check(static_cast<bool>(first) && static_cast<bool>(second), "two devices are created");
    check_eq(runtime->active_device_count(), 2, "both devices are active");
    check(first.gamepad->device_id() != second.gamepad->device_id(), "device ids are distinct");

    runtime->close_all();
    check_eq(runtime->active_device_count(), 0, "close_all releases every device");
    check(!first.gamepad->is_open(), "close_all closes the first device");
    check(!second.gamepad->is_open(), "close_all closes the second device");

    // A custom identity is preserved on the effective profile.
    vigem::DeviceProfile custom = vigem::profiles::xbox_360();
    custom.vendor_id = 0x1234;
    custom.product_id = 0x5678;
    auto custom_device = runtime->create_gamepad(custom);
    check(static_cast<bool>(custom_device), "custom identity device is created");
    if (custom_device) {
      check_eq(custom_device.gamepad->profile().vendor_id, 0x1234, "custom vendor id is kept");
      check_eq(custom_device.gamepad->profile().product_id, 0x5678, "custom product id is kept");
    }

    // Runtime destruction closes devices that are still open.
    auto leftover = runtime->create_gamepad(vigem::profiles::xbox_360());
    check(static_cast<bool>(leftover), "leftover device is created");
    runtime.reset();
    check(!leftover.gamepad->is_open(), "runtime destruction closes open devices");
  }

  void test_platform_backend_reporting() {
    auto runtime = vigem::Runtime::create();
    check(runtime != nullptr, "platform runtime is created");
    check(runtime->backend_kind() == vigem::BackendKind::platform_default, "platform backend is selected");
    check(runtime->capabilities().requires_installed_driver, "platform backend requires the driver");

    if (!runtime->capabilities().available) {
      auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
      check(!created, "device creation fails without the driver");
      check_eq(static_cast<int>(created.status.code()), static_cast<int>(vigem::ErrorCode::backend_unavailable),
               "missing driver reports backend_unavailable");
      check(!created.status.message().empty(), "missing driver reports a reason");
    }
  }

  void test_platform_library_override() {
    // An explicit client library path that cannot be loaded must degrade to
    // backend_unavailable with a reason instead of failing device creation obscurely.
    vigem::RuntimeOptions options;
    options.backend = vigem::BackendKind::platform_default;
    options.client_library_path = "Z:\\vigem-missing\\ViGEmClient.dll";

    auto runtime = vigem::Runtime::create(options);
    check(runtime != nullptr, "runtime with an explicit client path is created");
    check(!runtime->capabilities().available, "a bogus client library path is unavailable");

    auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
    check(!created, "device creation fails with a bogus client library path");
    check_eq(static_cast<int>(created.status.code()), static_cast<int>(vigem::ErrorCode::backend_unavailable),
             "bogus client library path reports backend_unavailable");
    check(!created.status.message().empty(), "bogus client library path reports a reason");
  }

  void test_input_mapping() {
    vigem::GamepadMapping mapping;

    // Drift inside the deadzone is reported as centered, and leaving it starts on the
    // anti-deadzone floor instead of zero, the way DS4Windows exposes the two zones.
    mapping.left_stick.deadzone = 0.2F;
    mapping.left_stick.anti_deadzone = 0.4F;

    vigem::GamepadState drifting;
    drifting.left_stick = {0.1F, 0.1F};
    check(mapping.apply(drifting).left_stick.x == 0.0F, "stick drift inside the deadzone is centered");

    vigem::GamepadState edge;
    edge.left_stick = {0.21F, 0.0F};
    const auto near_edge = mapping.apply(edge).left_stick;
    check(near_edge.x > 0.39F && near_edge.x < 0.42F, "anti-deadzone lifts the first movement off zero");

    vigem::GamepadState full;
    full.left_stick = {1.0F, 0.0F};
    check(mapping.apply(full).left_stick.x > 0.99F, "full deflection stays at full output");

    // Saturation, response curve and inversion shape the remaining travel.
    vigem::GamepadMapping early;
    early.left_stick.saturation = 0.5F;
    vigem::GamepadState half;
    half.left_stick = {0.5F, 0.0F};
    check(early.apply(half).left_stick.x > 0.99F, "saturation reaches full output early");

    vigem::GamepadMapping curved;
    curved.right_stick.exponent = 2.0F;
    vigem::GamepadState half_up;
    half_up.right_stick = {0.0F, 0.5F};
    half_up.left_stick = {0.0F, 0.5F};
    const auto curved_y = curved.apply(half_up).right_stick.y;
    check(curved_y > 0.24F && curved_y < 0.26F, "response curve squares the travel");

    vigem::GamepadMapping inverted;
    inverted.left_stick.invert_y = true;
    check(inverted.apply(half_up).left_stick.y < -0.49F, "inverted vertical axis points down");

    // Triggers get their own deadzone and saturation.
    vigem::GamepadMapping triggers;
    triggers.left_trigger.deadzone = 0.3F;
    vigem::GamepadState resting;
    resting.left_trigger = 0.2F;
    check(triggers.apply(resting).left_trigger == 0.0F, "trigger inside the deadzone reads zero");

    vigem::GamepadState pressed;
    pressed.left_trigger = 0.65F;
    const auto shaped_trigger = triggers.apply(pressed).left_trigger;
    check(shaped_trigger > 0.49F && shaped_trigger < 0.51F, "trigger deadzone rescales the travel");

    // A stick can drive the directional pad, and stop being visible to the game.
    vigem::GamepadMapping dpad;
    dpad.dpad_from_left_stick.enabled = true;
    dpad.dpad_from_left_stick.threshold = 0.6F;
    dpad.dpad_from_left_stick.suppress_stick = true;

    vigem::GamepadState corner;
    corner.left_stick = {0.8F, -0.7F};
    const auto synthesized = dpad.apply(corner);
    check(synthesized.buttons.test(vigem::GamepadButton::dpad_right), "stick presses the right direction");
    check(synthesized.buttons.test(vigem::GamepadButton::dpad_down), "stick presses the down direction");
    check(!synthesized.buttons.test(vigem::GamepadButton::dpad_up), "opposite direction stays released");
    check(synthesized.left_stick.x == 0.0F, "suppressing the stick keeps the game from seeing it");

    // Buttons can be redirected or suppressed, as DS4Windows and x360ce allow.
    vigem::GamepadMapping remap;
    remap.buttons.push_back({vigem::GamepadButton::a, vigem::GamepadButton::b, true});
    remap.buttons.push_back({vigem::GamepadButton::x, vigem::GamepadButton::x, false});

    vigem::GamepadState physical;
    physical.buttons.set(vigem::GamepadButton::a, true);
    physical.buttons.set(vigem::GamepadButton::x, true);
    const auto remapped = remap.apply(physical);
    check(remapped.buttons.test(vigem::GamepadButton::b), "mapped button is pressed on the emulated pad");
    check(!remapped.buttons.test(vigem::GamepadButton::a), "source button is released after the remap");
    check(!remapped.buttons.test(vigem::GamepadButton::x), "disabled button is suppressed");

    // Everything the mapping does not touch is forwarded unchanged.
    vigem::GamepadState extras;
    extras.battery = vigem::GamepadBattery {42, true};
    extras.gyroscope = vigem::Vector3 {1.0F, 2.0F, 3.0F};
    extras.touchpad_contacts[0].active = true;
    extras.touchpad_contacts[0].x = 0.25F;
    const auto preserved = mapping.apply(extras);
    check(preserved.battery.has_value(), "battery state survives mapping");
    check(preserved.gyroscope.has_value(), "motion state survives mapping");
    check(preserved.touchpad_contacts[0].active, "touch state survives mapping");
  }

  void test_device_recovery_and_dedup() {
    auto runtime = vigem::Runtime::create(fake_options());
    auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
    if (!created) {
      check(false, "recovery device is created");
      return;
    }

    auto &gamepad = *created.gamepad;
    check(gamepad.is_attached(), "the fake device reports itself attached");
    check(gamepad.reattach().ok(), "reattaching an attached device succeeds without work");

    // Duplicate frames are dropped, but a keepalive flush still reaches the device.
    gamepad.set_submit_on_change(true, 50);
    vigem::GamepadState state;
    state.buttons.set(vigem::GamepadButton::a, true);
    check(gamepad.submit(state).ok(), "the first frame submits");
    const auto after_first = gamepad.submit_count();
    check(gamepad.submit(state).ok(), "a duplicate frame reports success");
    check_eq(gamepad.submit_count(), after_first, "a duplicate frame is suppressed");

    state.left_stick = {0.5F, 0.0F};
    check(gamepad.submit(state).ok(), "a changed frame submits");
    check(gamepad.submit_count() > after_first, "a changed frame reaches the device");
    const auto after_change = gamepad.submit_count();

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    check(gamepad.submit(state).ok(), "the keepalive frame submits");
    check(gamepad.submit_count() > after_change, "the keepalive flush reaches the device");
    gamepad.set_submit_on_change(false);

    check(gamepad.close().ok(), "the device closes");
    check(gamepad.submit(state).code() == vigem::ErrorCode::device_closed,
          "a submit after close reports device_closed");
    check(gamepad.reattach().code() == vigem::ErrorCode::device_closed,
          "reattaching a closed device reports device_closed");
    check(!gamepad.is_attached(), "a closed device reports itself detached");
  }

  void test_mapping_persistence() {
    vigem::GamepadMapping mapping;
    mapping.left_stick.deadzone = 0.25F;
    mapping.left_stick.center_x = 0.05F;
    mapping.left_stick.per_axis_deadzone = true;
    mapping.right_stick.exponent = 1.5F;
    mapping.left_trigger.deadzone = 0.3F;
    mapping.dpad_from_left_stick.enabled = true;
    mapping.dpad_from_left_stick.threshold = 0.7F;
    mapping.dpad_from_left_stick.suppress_stick = true;
    mapping.buttons.push_back({vigem::GamepadButton::a, vigem::GamepadButton::b, true});
    mapping.buttons.push_back({vigem::GamepadButton::x, vigem::GamepadButton::x, false});

    const auto text = vigem::serialize(mapping);
    std::string error;
    const auto parsed = vigem::deserialize(text, &error);
    check(parsed.has_value(), "a serialized mapping parses back");
    if (parsed) {
      check(std::fabs(parsed->left_stick.deadzone - 0.25F) < 1e-5F, "the deadzone round-trips");
      check(std::fabs(parsed->left_stick.center_x - 0.05F) < 1e-5F, "the centre offset round-trips");
      check(std::fabs(parsed->right_stick.exponent - 1.5F) < 1e-5F, "the response curve round-trips");
      check(parsed->left_stick.per_axis_deadzone, "the per-axis flag round-trips");
      check(parsed->dpad_from_left_stick.enabled, "the dpad flag round-trips");
      check(parsed->dpad_from_left_stick.suppress_stick, "the dpad suppression round-trips");
      check_eq(parsed->buttons.size(), 2, "button entries round-trip");
      if (parsed->buttons.size() == 2) {
        check(parsed->buttons[0].source == vigem::GamepadButton::a &&
                  parsed->buttons[0].target == vigem::GamepadButton::b,
              "a button redirect round-trips");
        check(!parsed->buttons[1].enabled, "a button suppression round-trips");
      }
    }

    check(!vigem::deserialize("not a mapping").has_value(), "invalid mapping text is rejected");

    // The adapter applies an installed mapping before packing.
    auto runtime = vigem::Runtime::create(fake_options());
    auto adapter = vigem::GamepadStateAdapter::create(*runtime, vigem::profiles::xbox_360());
    if (!adapter) {
      check(false, "the adapter is created");
      return;
    }

    vigem::GamepadMapping invert;
    invert.left_stick.invert_y = true;
    adapter.adapter->set_mapping(invert);
    check(adapter.adapter->mapping().has_value(), "the adapter stores the mapping");
    check(adapter.adapter->set_left_stick({0.0F, 1.0F}).ok(), "the mapped adapter submits");

    const auto report = adapter.adapter->gamepad()->last_input_report();
    if (report.size() == 12) {
      const auto raw = static_cast<std::int16_t>(report[6] | (report[7] << 8));
      check(raw < 0, "the adapter applied the inversion before packing");
    }
  }

  void test_status_and_logging() {
    std::vector<std::string> messages;
    vigem::RuntimeOptions options = fake_options();
    options.log_callback = [&messages](vigem::LogLevel level, const std::string &message) {
      messages.push_back(std::string(vigem::to_string(level)) + ": " + message);
    };

    auto runtime = vigem::Runtime::create(options);
    check(!messages.empty(), "runtime writes a log message");
    check(!runtime->capabilities().backend_name.empty(), "backend reports a name");

    // A throwing log callback must not escape into the API, and it is disabled after
    // its first failure so it cannot interrupt later operations.
    int log_throws = 0;
    vigem::RuntimeOptions throwing_options = fake_options();
    throwing_options.log_callback = [&log_throws](vigem::LogLevel, const std::string &) {
      ++log_throws;
      throw std::runtime_error("log boom");
    };
    auto throwing_runtime = vigem::Runtime::create(throwing_options);
    check(throwing_runtime != nullptr, "runtime survives a throwing log callback");
    check_eq(log_throws, 1, "throwing log callback is disabled after it throws");
    (void)throwing_runtime->create_gamepad(vigem::profiles::xbox_360());
    check_eq(log_throws, 1, "disabled log callback is not invoked again");
    check_eq(static_cast<int>(vigem::OperationStatus::failure(vigem::ErrorCode::no_free_slot, "x").code()),
             static_cast<int>(vigem::ErrorCode::no_free_slot), "failure status keeps its code");
    check(std::string(vigem::to_string(vigem::ErrorCode::device_closed)) == "device_closed", "error names are stable");
  }

}  // namespace

int main() {
  test_runtime_and_profiles();
  test_xbox_report_packing();
  test_dualshock4_report_packing();
  test_output_dispatch();
  test_adapter_incremental_updates();
  test_input_mapping();
  test_device_recovery_and_dedup();
  test_mapping_persistence();
  test_runtime_lifecycle();
  test_platform_backend_reporting();
  test_platform_library_override();
  test_status_and_logging();

  if (failures == 0) {
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
  }

  std::printf("%d check(s) failed\n", failures);
  return EXIT_FAILURE;
}
