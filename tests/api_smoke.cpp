/**
 * @file tests/api_smoke.cpp
 * @brief Behavioural checks for the modern ViGEm client API.
 *
 * The checks run against the in-memory backend so report packing, status
 * handling, callbacks, and the adapter are exercised without the bus driver.
 */
#include <vigem/vigem.hpp>

// standard includes
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
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

    check(!gamepad.poll_raw_output(0).has_value(), "fake backend publishes no raw reports");
    check(gamepad.xinput_user_index() == -1, "dualshock 4 has no xinput index");
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
#if defined(_WIN32)
    options.client_library_path = "Z:\\vigem-missing\\ViGEmClient.dll";
#else
    options.client_library_path = "/nonexistent/ViGEmClient.so";
#endif

    auto runtime = vigem::Runtime::create(options);
    check(runtime != nullptr, "runtime with an explicit client path is created");
    check(!runtime->capabilities().available, "a bogus client library path is unavailable");

    auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
    check(!created, "device creation fails with a bogus client library path");
    check_eq(static_cast<int>(created.status.code()), static_cast<int>(vigem::ErrorCode::backend_unavailable),
             "bogus client library path reports backend_unavailable");
    check(!created.status.message().empty(), "bogus client library path reports a reason");
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
