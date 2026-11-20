// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libvigem/include/vigem/types.hpp
 * @brief Core public types for the modern ViGEm client API.
 */
#pragma once

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Public ViGEm client API namespace.
 */
namespace vigem {

  /**
   * @brief Stable identifier assigned to a virtual device instance.
   */
  using DeviceId = std::uint64_t;

  /**
   * @brief Error categories returned by ViGEm client operations.
   */
  enum class ErrorCode {
    ok,  ///< Operation completed successfully.
    invalid_argument,  ///< Caller supplied invalid input.
    backend_unavailable,  ///< The ViGEm bus driver or its client library is unusable on this host.
    unsupported_profile,  ///< The backend cannot create the requested device profile.
    device_closed,  ///< Device operation was requested after the device closed.
    no_free_slot,  ///< Every emulated device slot provided by the driver is occupied.
    not_supported,  ///< The driver does not implement the requested operation.
    device_detached,  ///< The driver no longer holds the target; call reattach().
    backend_failure,  ///< Backend-specific operation failed.
  };

  /**
   * @brief Get a stable human-readable name for an error category.
   *
   * @param code Error category.
   * @return Error category name.
   */
  const char *to_string(ErrorCode code);

  /**
   * @brief Result status with an error category and human-readable message.
   */
  class OperationStatus {
  public:
    /**
     * @brief Construct a successful status.
     */
    OperationStatus();

    /**
     * @brief Construct a status with an explicit error code and message.
     *
     * @param code Error category.
     * @param message Human-readable status message.
     */
    OperationStatus(ErrorCode code, std::string message);

    /**
     * @brief Create a successful status.
     *
     * @return Successful status object.
     */
    static OperationStatus success();

    /**
     * @brief Create a failing status.
     *
     * @param code Error category.
     * @param message Human-readable failure message.
     * @return Failing status object.
     */
    static OperationStatus failure(ErrorCode code, std::string message);

    /**
     * @brief Check whether the operation succeeded.
     *
     * @return `true` when the status is successful.
     */
    bool ok() const;

    /**
     * @brief Get the status error category.
     *
     * @return Error category.
     */
    ErrorCode code() const;

    /**
     * @brief Get the human-readable status message.
     *
     * @return Human-readable status message.
     */
    const std::string &message() const;

    /**
     * @brief Check whether the status carries a native driver error code.
     *
     * @return `true` when a raw `VIGEM_ERROR` value was recorded.
     */
    bool has_native_code() const;

    /**
     * @brief Get the raw driver error code, when one was recorded.
     *
     * Callers use this to distinguish recoverable conditions the category alone cannot
     * express, such as a restarted bus service from a full device slot table.
     *
     * @return Native `VIGEM_ERROR` value, or `0` when none was recorded.
     */
    std::uint32_t native_code() const;

    /**
     * @brief Create a failing status that records the raw driver error.
     *
     * @param code Error category.
     * @param message Human-readable failure message.
     * @param native_code Raw `VIGEM_ERROR` value.
     * @return Failing status object.
     */
    static OperationStatus failure(ErrorCode code, std::string message, std::uint32_t native_code);

  private:
    ErrorCode code_;
    std::string message_;
    std::uint32_t native_code_ = 0;
    bool has_native_code_ = false;
  };

  /**
   * @brief Backend implementation selection.
   */
  enum class BackendKind {
    platform_default,  ///< The ViGEmBus driver backend, which targets Windows 11.
    fake,  ///< In-memory backend for tests and API validation.
  };

  /**
   * @brief Diagnostic message severity.
   */
  enum class LogLevel : std::uint8_t {
    debug,  ///< Detailed diagnostic information.
    info,  ///< Normal runtime lifecycle information.
    warning,  ///< Recoverable problem or fallback.
    error,  ///< Operation failure.
  };

  /**
   * @brief Get a stable human-readable name for a log level.
   *
   * @param level Log level.
   * @return Log level name.
   */
  const char *to_string(LogLevel level);

  /**
   * @brief Consumer callback that receives ViGEm client diagnostic messages.
   *
   * The callback is invoked synchronously from the thread performing the
   * operation, which includes driver-owned notification threads. If the callback
   * throws, it is disabled for subsequent messages.
   */
  using LogCallback = std::function<void(LogLevel level, const std::string &message)>;

  /**
   * @brief Runtime creation options.
   */
  struct RuntimeOptions {
    /**
     * @brief Backend implementation requested by the caller.
     */
    BackendKind backend = BackendKind::platform_default;

    /**
     * @brief Optional callback for diagnostic messages.
     */
    LogCallback log_callback;

    /**
     * @brief Optional explicit path to the ViGEm client library.
     *
     * An empty value uses the standard Windows library search order and resolves
     * `ViGEmClient.dll` from the application directory first.
     */
    std::string client_library_path;
  };

  /**
   * @brief Feature set exposed by the selected backend.
   */
  struct BackendCapabilities {
    /**
     * @brief Human-readable backend name.
     */
    std::string backend_name;

    /**
     * @brief Whether the backend can currently create devices.
     */
    bool available = false;

    /**
     * @brief Whether the backend can create gamepad devices.
     */
    bool supports_gamepad = false;

    /**
     * @brief Whether the backend can deliver gamepad feedback to callers.
     */
    bool supports_output_reports = false;

    /**
     * @brief Whether the backend can deliver raw output reports to callers.
     */
    bool supports_raw_output_reports = false;

    /**
     * @brief Whether the backend requires an installed driver package.
     */
    bool requires_installed_driver = false;
  };

  /**
   * @brief Built-in gamepad profile identifiers.
   */
  enum class GamepadProfileKind {
    xbox_360,  ///< Xbox 360-compatible profile published through the XUSB stack.
    dualshock4,  ///< PlayStation DualShock 4-compatible profile.
  };

  /**
   * @brief Optional behavior advertised by a gamepad profile.
   */
  struct GamepadProfileCapabilities {
    /**
     * @brief Whether the profile supports rumble output.
     */
    bool supports_rumble = false;

    /**
     * @brief Whether the profile supports an RGB LED output.
     */
    bool supports_rgb_led = false;

    /**
     * @brief Whether the profile supports player indicator LED output.
     */
    bool supports_player_leds = false;

    /**
     * @brief Whether the profile exposes touchpad input.
     */
    bool supports_touchpad = false;

    /**
     * @brief Whether the profile exposes motion sensors.
     */
    bool supports_motion = false;

    /**
     * @brief Whether the profile supports independent trigger rumble output.
     */
    bool supports_trigger_rumble = false;
  };

  /**
   * @brief Identity and feature data used to create a virtual device.
   */
  struct DeviceProfile {
    /**
     * @brief Built-in gamepad profile identifier.
     */
    GamepadProfileKind kind = GamepadProfileKind::xbox_360;

    /**
     * @brief Vendor identifier reported by the emulated device.
     */
    std::uint16_t vendor_id = 0;

    /**
     * @brief Product identifier reported by the emulated device.
     */
    std::uint16_t product_id = 0;

    /**
     * @brief Profile feature flags.
     */
    GamepadProfileCapabilities capabilities;
  };

  /**
   * @brief Logical gamepad buttons accepted by the common gamepad state model.
   */
  enum class GamepadButton : std::uint8_t {
    a = 0,  ///< South face button.
    b,  ///< East face button.
    x,  ///< West face button.
    y,  ///< North face button.
    back,  ///< Back, select, or share button.
    start,  ///< Start or options button.
    guide,  ///< System guide button.
    left_stick,  ///< Left stick press.
    right_stick,  ///< Right stick press.
    left_shoulder,  ///< Left shoulder button.
    right_shoulder,  ///< Right shoulder button.
    dpad_up,  ///< Directional pad up.
    dpad_down,  ///< Directional pad down.
    dpad_left,  ///< Directional pad left.
    dpad_right,  ///< Directional pad right.
    misc1,  ///< Profile-specific miscellaneous button.
    touchpad,  ///< Touchpad click button.
    paddle1,  ///< First rear paddle button.
    paddle2,  ///< Second rear paddle button.
    paddle3,  ///< Third rear paddle button.
    paddle4,  ///< Fourth rear paddle button.
  };

  /**
   * @brief Compact set of pressed gamepad buttons.
   */
  class ButtonSet {
  public:
    /**
     * @brief Set or clear a button.
     *
     * @param button Button to update.
     * @param pressed Whether the button is pressed.
     */
    void set(GamepadButton button, bool pressed = true);

    /**
     * @brief Clear a button.
     *
     * @param button Button to clear.
     */
    void reset(GamepadButton button);

    /**
     * @brief Clear all buttons.
     */
    void clear();

    /**
     * @brief Check whether a button is pressed.
     *
     * @param button Button to test.
     * @return `true` when the button is pressed.
     */
    bool test(GamepadButton button) const;

    /**
     * @brief Get the raw bitset value.
     *
     * @return Raw button bits.
     */
    std::uint32_t raw_bits() const;

  private:
    std::uint32_t bits_ = 0;
  };

  /**
   * @brief Normalized two-axis stick state.
   */
  struct Stick {
    /**
     * @brief Horizontal axis in the inclusive range `[-1.0, 1.0]`.
     */
    float x = 0.0F;

    /**
     * @brief Vertical axis in the inclusive range `[-1.0, 1.0]`.
     *
     * Positive values point up, matching the Xbox 360 and DualShock 4 axis
     * orientation exposed to applications.
     */
    float y = 0.0F;
  };

  /**
   * @brief Three-axis sensor sample.
   */
  struct Vector3 {
    /**
     * @brief X-axis value.
     */
    float x = 0.0F;

    /**
     * @brief Y-axis value.
     */
    float y = 0.0F;

    /**
     * @brief Z-axis value.
     */
    float z = 0.0F;
  };

  /**
   * @brief Touchpad contact carried by a gamepad report.
   */
  struct GamepadTouchContact {
    /**
     * @brief Consumer-stable contact identifier.
     */
    std::uint8_t id = 0;

    /**
     * @brief Whether this contact is active.
     */
    bool active = false;

    /**
     * @brief Normalized X coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float x = 0.0F;

    /**
     * @brief Normalized Y coordinate in the inclusive range `[0.0, 1.0]`.
     *
     * Zero is the top edge of the touchpad.
     */
    float y = 0.0F;
  };

  /**
   * @brief Battery state of a virtual gamepad.
   */
  struct GamepadBattery {
    /**
     * @brief Remaining charge as a percentage in the inclusive range `[0, 100]`.
     */
    std::uint8_t percent_remaining = 100;

    /**
     * @brief Whether the device reports itself as connected to power.
     */
    bool charging = false;
  };

  /**
   * @brief Common gamepad input state accepted by the ViGEm client API.
   *
   * Values are normalized so consumers can forward client input without
   * knowing how each emulated device encodes its report.
   */
  struct GamepadState {
    /**
     * @brief Pressed button set.
     */
    ButtonSet buttons;

    /**
     * @brief Left stick state.
     */
    Stick left_stick;

    /**
     * @brief Right stick state.
     */
    Stick right_stick;

    /**
     * @brief Left trigger value in the inclusive range `[0.0, 1.0]`.
     */
    float left_trigger = 0.0F;

    /**
     * @brief Right trigger value in the inclusive range `[0.0, 1.0]`.
     */
    float right_trigger = 0.0F;

    /**
     * @brief Battery state to publish, when available.
     *
     * Profiles that report battery status encode it, and profiles without a battery
     * ignore it. Leaving this unset keeps the wired, fully charged default that the
     * driver itself publishes.
     */
    std::optional<GamepadBattery> battery;

    /**
     * @brief Accelerometer data in raw DualShock 4 units, when available.
     *
     * Values are forwarded verbatim into the emulated device report, so they use
     * the DualShock 4 sensor scale rather than SI units: one gravity is 8192 units,
     * that is `m / s^2 / 9.80665 * 8192`. Profiles without motion sensors ignore
     * this field.
     */
    std::optional<Vector3> acceleration;

    /**
     * @brief Gyroscope data in raw DualShock 4 units, when available.
     *
     * Values are forwarded verbatim into the emulated device report. Profiles
     * without motion sensors ignore this field.
     */
    std::optional<Vector3> gyroscope;

    /**
     * @brief Gamepad touchpad contacts.
     *
     * The first active contact is published as the current touch point. Profiles
     * without a touchpad ignore this field.
     */
    std::array<GamepadTouchContact, 2> touchpad_contacts {};
  };

  /**
   * @brief Output report categories delivered by a gamepad backend.
   */
  enum class GamepadOutputKind {
    rumble,  ///< Rumble motor output.
    rgb_led,  ///< RGB LED color output.
    player_leds,  ///< Player indicator LED output.
    raw_report,  ///< Raw output report bytes.
  };

  /**
   * @brief Normalized gamepad output event delivered to the consumer.
   */
  struct GamepadOutput {
    /**
     * @brief Output event category.
     */
    GamepadOutputKind kind = GamepadOutputKind::raw_report;

    /**
     * @brief Low-frequency rumble motor strength.
     */
    std::uint16_t low_frequency_rumble = 0;

    /**
     * @brief High-frequency rumble motor strength.
     */
    std::uint16_t high_frequency_rumble = 0;

    /**
     * @brief Red LED channel value.
     */
    std::uint8_t red = 0;

    /**
     * @brief Green LED channel value.
     */
    std::uint8_t green = 0;

    /**
     * @brief Blue LED channel value.
     */
    std::uint8_t blue = 0;

    /**
     * @brief Solid player indicator LED states, ordered from player one through four.
     */
    std::array<bool, 4> player_leds {};

    /**
     * @brief Raw output report payload.
     */
    std::vector<std::uint8_t> raw_report;
  };

  /**
   * @brief Callback invoked when a gamepad receives output from the backend.
   */
  using OutputCallback = std::function<void(const GamepadOutput &)>;

}  // namespace vigem
