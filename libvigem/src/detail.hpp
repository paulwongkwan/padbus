// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libvigem/src/detail.hpp
 * @brief Internal interfaces shared by the ViGEm client backends.
 */
#pragma once

// standard includes
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// local includes
#include <vigem/vigem.hpp>

namespace vigem::detail {

  /**
   * @brief Thread-safe diagnostic sink shared by a runtime and its devices.
   *
   * The consumer callback is copied out of the sink before it runs, so it may call
   * back into the API. Messages can arrive from several threads at once (including
   * driver notification threads), so overlapping invocations are possible.
   */
  class LogSink {
  public:
    /**
     * @brief Construct a sink for an optional consumer callback.
     *
     * @param callback Consumer callback, or an empty function.
     */
    explicit LogSink(LogCallback callback);

    /**
     * @brief Write one diagnostic message.
     *
     * @param level Message severity.
     * @param message Diagnostic message.
     */
    void write(LogLevel level, const std::string &message);

  private:
    std::mutex mutex_;
    LogCallback callback_;
    bool disabled_ = false;
  };

  /**
   * @brief Packed device input report produced from a normalized gamepad state.
   */
  struct PackedReport {
    /**
     * @brief Report bytes for the profile the state was packed for.
     */
    std::vector<std::uint8_t> bytes;
  };

  /**
   * @brief Pack a normalized gamepad state into a device input report.
   *
   * @param state Normalized gamepad state.
   * @param kind Target profile.
   * @param ds4_timestamp DualShock 4 report timestamp in 5.333 us units.
   * @return Packed report bytes and their length.
   */
  PackedReport pack_report(const GamepadState &state, GamepadProfileKind kind, std::uint16_t ds4_timestamp);

  /**
   * @brief Backend-independent bookkeeping for a single virtual gamepad.
   */
  class GamepadImpl {
  public:
    /**
     * @brief Construct the shared gamepad state.
     *
     * @param id Device identifier assigned by the runtime.
     * @param profile Effective device profile.
     * @param log Diagnostic sink.
     */
    GamepadImpl(DeviceId id, DeviceProfile profile, std::shared_ptr<LogSink> log);

    /**
     * @brief Destroy the shared gamepad state.
     */
    virtual ~GamepadImpl() = default;

    /**
     * @brief Copy construction is disabled because the instance owns device state.
     */
    GamepadImpl(const GamepadImpl &) = delete;

    /**
     * @brief Copy assignment is disabled because the instance owns device state.
     *
     * @return This instance.
     */
    GamepadImpl &operator=(const GamepadImpl &) = delete;

    /**
     * @brief Get the device identifier assigned by the runtime.
     *
     * @return Device identifier.
     */
    DeviceId device_id() const;

    /**
     * @brief Get the effective device profile.
     *
     * @return Effective device profile.
     */
    const DeviceProfile &profile() const;

    /**
     * @brief Check whether the device is open.
     *
     * @return `true` when the device accepts operations.
     */
    bool is_open() const;

    /**
     * @brief Enable duplicate report suppression.
     *
     * @param enabled Whether duplicate reports are suppressed.
     * @param keepalive_ms Longest interval without a submission, in milliseconds.
     */
    void set_submit_on_change(bool enabled, std::uint32_t keepalive_ms);

    /**
     * @brief Check whether the device is open and still attached to the backend.
     *
     * @return `true` when the device accepts operations and the backend reports it.
     */
    bool is_attached() const;

    /**
     * @brief Re-attach the device after the driver dropped it.
     *
     * @return Re-attach operation status.
     */
    OperationStatus reattach();

    /**
     * @brief Close the device once, running backend teardown on the first call.
     *
     * @return Close operation status.
     */
    OperationStatus close();

    /**
     * @brief Pack and submit a normalized gamepad state.
     *
     * @param state Normalized gamepad state.
     * @return Submit operation status.
     */
    OperationStatus submit(const GamepadState &state);

    /**
     * @brief Dispatch an output event to the registered consumer callback.
     *
     * @param output Output event.
     * @return Dispatch operation status.
     */
    OperationStatus dispatch_output(const GamepadOutput &output);

    /**
     * @brief Wait for a pending raw output report.
     *
     * @param timeout_ms Maximum wait in milliseconds.
     * @return Raw report event, or `std::nullopt` when none is pending.
     */
    virtual std::optional<GamepadOutput> poll_raw_output(std::uint32_t timeout_ms);

    /**
     * @brief Get the XInput user index of the emulated device.
     *
     * @return Zero-based user index, or `-1` when unavailable.
     */
    virtual long xinput_user_index() const;

    /**
     * @brief Get the most recently submitted gamepad state.
     *
     * @return Last submitted state.
     */
    GamepadState last_submitted_state() const;

    /**
     * @brief Get the most recently packed input report.
     *
     * @return Last input report bytes.
     */
    std::vector<std::uint8_t> last_input_report() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

    /**
     * @brief Register a callback for output events.
     *
     * @param callback Output callback, or an empty function to clear it.
     */
    void set_output_callback(const OutputCallback &callback);

  protected:
    /**
     * @brief Report whether the backend still has the device attached.
     *
     * @return `true` when the backend reports the device as attached.
     */
    virtual bool attached() const;

    /**
     * @brief Ask the backend to attach the device again after it was dropped.
     *
     * @return Re-attach operation status.
     */
    virtual OperationStatus on_reattach();

    /**
     * @brief Publish a packed report to the device backend.
     *
     * @param state Normalized state that produced the report.
     * @param packed Report bytes for the effective profile.
     * @return Submit operation status.
     */
    virtual OperationStatus on_submit(const GamepadState &state, const std::vector<std::uint8_t> &packed) = 0;

    /**
     * @brief Release backend resources for the device.
     *
     * @return Close operation status.
     */
    virtual OperationStatus on_close() = 0;

    /**
     * @brief Write a diagnostic message.
     *
     * @param level Message severity.
     * @param message Diagnostic message.
     */
    void log(LogLevel level, const std::string &message) const;

  private:
    /**
     * @brief Advance the DualShock 4 report timestamp for the next submission.
     *
     * @return Timestamp in DualShock 4 5.333 us units.
     */
    std::uint16_t advance_ds4_timestamp();

    /**
     * @brief Decide whether a packed report is worth sending to the backend.
     *
     * @param packed Packed report for the current state.
     * @return `true` when the report differs from the previous one or the keepalive expired.
     */
    bool should_submit(const std::vector<std::uint8_t> &packed) const;

    DeviceId id_;
    DeviceProfile profile_;
    std::shared_ptr<LogSink> log_;

    mutable std::mutex mutex_;
    bool open_ = true;
    GamepadState last_state_;
    std::vector<std::uint8_t> last_report_;
    std::size_t submit_count_ = 0;

    bool submit_on_change_ = false;
    std::uint32_t keepalive_ms_ = 100;
    std::chrono::steady_clock::time_point last_submit_ts_ {std::chrono::steady_clock::now()};

    std::uint16_t ds4_timestamp_ = 0;
    std::chrono::steady_clock::time_point last_timestamp_ts_ {std::chrono::steady_clock::now()};

    std::mutex callback_mutex_;
    OutputCallback callback_;
  };

  /**
   * @brief Result returned by a backend gamepad creation request.
   */
  struct GamepadCreation {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created device when creation succeeds.
     */
    std::shared_ptr<GamepadImpl> device;
  };

  /**
   * @brief Device backend owned by a runtime.
   */
  class Backend {
  public:
    /**
     * @brief Destroy the backend.
     */
    virtual ~Backend() = default;

    /**
     * @brief Get backend capabilities.
     *
     * @return Backend capabilities.
     */
    const BackendCapabilities &capabilities() const;

    /**
     * @brief Create a virtual gamepad.
     *
     * @param id Device identifier assigned by the runtime.
     * @param profile Requested device profile.
     * @return Creation result.
     */
    virtual GamepadCreation create_gamepad(DeviceId id, const DeviceProfile &profile) = 0;

    /**
     * @brief Get the number of open devices created by the backend.
     *
     * @return Active device count.
     */
    std::size_t active_device_count() const;

    /**
     * @brief Close every open device created by the backend.
     */
    virtual void close_all();

  protected:
    /**
     * @brief Register a successfully created device.
     *
     * @param device Created device.
     */
    void track(const std::shared_ptr<GamepadImpl> &device);

    /**
     * @brief Get the diagnostic sink.
     *
     * @return Diagnostic sink.
     */
    std::shared_ptr<LogSink> log() const;

    /**
     * @brief Install the diagnostic sink.
     *
     * @param log Diagnostic sink.
     */
    void set_log(std::shared_ptr<LogSink> log);

    /**
     * @brief Install backend capabilities.
     *
     * @param capabilities Backend capabilities.
     */
    void set_capabilities(BackendCapabilities capabilities);

  private:
    mutable std::mutex devices_mutex_;
    std::vector<std::weak_ptr<GamepadImpl>> devices_;
    std::shared_ptr<LogSink> log_;
    BackendCapabilities capabilities_;
  };

  /**
   * @brief Create the ViGEmBus driver backend selected by the runtime options.
   *
   * The returned backend reports `available == false` with a reason when the
   * client library or the bus driver is missing, so hosts can explain the
   * failure instead of encountering a load error.
   *
   * @param options Runtime options.
   * @param log Diagnostic sink.
   * @return Backend instance.
   */
  std::unique_ptr<Backend> make_platform_backend(const RuntimeOptions &options, std::shared_ptr<LogSink> log);

  /**
   * @brief Runtime-owned backend state.
   */
  class RuntimeState {
  public:
    /**
     * @brief Construct runtime state for the requested options.
     *
     * @param options Runtime options.
     */
    explicit RuntimeState(RuntimeOptions options);

    /**
     * @brief Destroy runtime state and close remaining devices.
     */
    ~RuntimeState();

    /**
     * @brief Copy construction is disabled because the state owns the backend.
     */
    RuntimeState(const RuntimeState &) = delete;

    /**
     * @brief Copy assignment is disabled because the state owns the backend.
     *
     * @return This state.
     */
    RuntimeState &operator=(const RuntimeState &) = delete;

    /**
     * @brief Get backend capabilities.
     *
     * @return Backend capabilities.
     */
    const BackendCapabilities &capabilities() const;

    /**
     * @brief Get the backend kind used by this state.
     *
     * @return Backend kind.
     */
    BackendKind backend_kind() const;

    /**
     * @brief Create a virtual gamepad.
     *
     * @param profile Requested device profile.
     * @return Gamepad creation result.
     */
    GamepadCreationResult create_gamepad(const DeviceProfile &profile);

    /**
     * @brief Get the number of open devices.
     *
     * @return Active device count.
     */
    std::size_t active_device_count() const;

    /**
     * @brief Close every open device.
     */
    void close_all();

  private:
    BackendKind kind_;
    std::unique_ptr<Backend> backend_;
    std::atomic<DeviceId> next_id_ {1};
  };

}  // namespace vigem::detail
