/**
 * @file libvigem/include/vigem/profiles.hpp
 * @brief Built-in virtual device profile declarations.
 */
#pragma once

// standard includes
#include <optional>
#include <vector>

// local includes
#include <vigem/types.hpp>

namespace vigem::profiles {

  /**
   * @brief Create the Xbox 360-compatible gamepad profile.
   *
   * Uses the Microsoft vendor and wired controller product identifiers, matching
   * the personality the bus driver publishes through the XUSB stack.
   *
   * @return Xbox 360-compatible device profile.
   */
  DeviceProfile xbox_360();

  /**
   * @brief Create the PlayStation DualShock 4-compatible gamepad profile.
   *
   * The bus driver publishes the wired DualShock 4 personality, so this profile
   * uses USB identity and full-size input reports with touch and motion data.
   *
   * @return DualShock 4-compatible device profile.
   */
  DeviceProfile dualshock4();

  /**
   * @brief Look up a built-in gamepad profile by kind.
   *
   * @param kind Built-in gamepad profile kind.
   * @return Matching profile, or `std::nullopt` when the kind is unknown.
   */
  std::optional<DeviceProfile> gamepad_profile(GamepadProfileKind kind);

  /**
   * @brief Get the default built-in gamepad profile set.
   *
   * @return Default built-in gamepad profiles.
   */
  std::vector<DeviceProfile> built_in_gamepad_profiles();

}  // namespace vigem::profiles
