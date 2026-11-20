// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libpadbus/src/profiles.cpp
 * @brief Built-in virtual device profiles and profile capability helpers.
 */
#include <padbus/profiles.hpp>

// standard includes
#include <vector>

// local includes
#include <padbus/gamepad_adapter.hpp>

namespace padbus::profiles {

  DeviceProfile xbox_360() {
    DeviceProfile profile;
    profile.kind = GamepadProfileKind::xbox_360;
    profile.vendor_id = 0x045E;
    profile.product_id = 0x028E;
    profile.capabilities.supports_rumble = true;
    profile.capabilities.supports_player_leds = true;
    return profile;
  }

  DeviceProfile dualshock4() {
    DeviceProfile profile;
    profile.kind = GamepadProfileKind::dualshock4;
    profile.vendor_id = 0x054C;
    profile.product_id = 0x05C4;
    profile.capabilities.supports_rumble = true;
    profile.capabilities.supports_rgb_led = true;
    profile.capabilities.supports_touchpad = true;
    profile.capabilities.supports_motion = true;
    return profile;
  }

  std::optional<DeviceProfile> gamepad_profile(GamepadProfileKind kind) {
    switch (kind) {
      case GamepadProfileKind::xbox_360:
        return xbox_360();
      case GamepadProfileKind::dualshock4:
        return dualshock4();
    }

    return std::nullopt;
  }

  std::vector<DeviceProfile> built_in_gamepad_profiles() {
    return {xbox_360(), dualshock4()};
  }

}  // namespace padbus::profiles

namespace padbus {

  GamepadProfileSupport gamepad_profile_support(const DeviceProfile &profile) {
    GamepadProfileSupport support;
    support.supports_rumble = profile.capabilities.supports_rumble;
    support.supports_trigger_rumble = profile.capabilities.supports_trigger_rumble;
    support.supports_rgb_led = profile.capabilities.supports_rgb_led;
    support.supports_motion = profile.capabilities.supports_motion;
    support.supports_touchpad = profile.capabilities.supports_touchpad;
    support.supports_player_leds = profile.capabilities.supports_player_leds;
    support.supports_misc1_button = false;
    support.supports_touchpad_button = profile.capabilities.supports_touchpad;
    support.supported_rear_paddle_count = 0;
    return support;
  }

  bool supports_gamepad_button(const DeviceProfile &profile, GamepadButton button) {
    switch (button) {
      case GamepadButton::touchpad:
        return profile.capabilities.supports_touchpad;
      case GamepadButton::misc1:
      case GamepadButton::paddle1:
      case GamepadButton::paddle2:
      case GamepadButton::paddle3:
      case GamepadButton::paddle4:
        return false;
      case GamepadButton::a:
      case GamepadButton::b:
      case GamepadButton::x:
      case GamepadButton::y:
      case GamepadButton::back:
      case GamepadButton::start:
      case GamepadButton::guide:
      case GamepadButton::left_stick:
      case GamepadButton::right_stick:
      case GamepadButton::left_shoulder:
      case GamepadButton::right_shoulder:
      case GamepadButton::dpad_up:
      case GamepadButton::dpad_down:
      case GamepadButton::dpad_left:
      case GamepadButton::dpad_right:
        return true;
    }

    return false;
  }

  bool supports_gamepad_output(const DeviceProfile &profile, GamepadOutputKind output_kind) {
    switch (output_kind) {
      case GamepadOutputKind::rumble:
        return profile.capabilities.supports_rumble;
      case GamepadOutputKind::rgb_led:
        return profile.capabilities.supports_rgb_led;
      case GamepadOutputKind::player_leds:
        return profile.capabilities.supports_player_leds;
      case GamepadOutputKind::raw_report:
        return profile.kind == GamepadProfileKind::dualshock4;
    }

    return false;
  }

}  // namespace padbus
