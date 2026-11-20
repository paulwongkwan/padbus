// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libvigem/include/vigem/runtime.hpp
 * @brief Runtime and virtual gamepad handle declarations.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// local includes
#include <vigem/types.hpp>

namespace vigem {

  class Runtime;

  namespace detail {
    class GamepadImpl;
    class RuntimeState;

    /**
     * @brief Token used by Runtime to construct runtime-owned handles.
     */
    struct RuntimeConstructionToken {
    private:
      friend class ::vigem::Runtime;
      friend class RuntimeState;

      RuntimeConstructionToken() = default;
    };
  }  // namespace detail

  /**
   * @brief Common interface for virtual device handles.
   */
  class VirtualDevice {
  public:
    /**
     * @brief Destroy the virtual device handle.
     */
    virtual ~VirtualDevice() = default;

    /**
     * @brief Get the device identifier assigned by the runtime.
     *
     * @return Device identifier.
     */
    virtual DeviceId device_id() const = 0;

    /**
     * @brief Get the effective profile exposed by the backend for this device.
     *
     * @return Effective device profile.
     */
    virtual const DeviceProfile &profile() const = 0;

    /**
     * @brief Check whether the device is open.
     *
     * @return `true` when the device can accept operations.
     */
    virtual bool is_open() const = 0;

    /**
     * @brief Close the virtual device.
     *
     * Closing an already closed device is a no-op that reports success.
     *
     * @return Close operation status.
     */
    virtual OperationStatus close() = 0;
  };

  /**
   * @brief Virtual gamepad device handle.
   *
   * `submit()` and the output path may run on different threads: backends deliver
   * feedback from driver notification threads while the host submits input, and the
   * handle serializes access to the underlying device. State queries return copies.
   */
  class Gamepad final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    Gamepad(const Gamepad &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This gamepad handle.
     */
    Gamepad &operator=(const Gamepad &) = delete;

    /**
     * @brief Move construct a gamepad handle.
     *
     * @param other Handle to move from.
     */
    Gamepad(Gamepad &&other) noexcept;

    /**
     * @brief Move assign a gamepad handle.
     *
     * @param other Handle to move from.
     * @return This gamepad handle.
     */
    Gamepad &operator=(Gamepad &&other) noexcept;

    /**
     * @brief Construct a gamepad handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared gamepad state.
     */
    Gamepad(detail::RuntimeConstructionToken token, std::shared_ptr<detail::GamepadImpl> device);

    /**
     * @brief Destroy the gamepad handle and close the virtual device if it is still open.
     */
    ~Gamepad() override;

    /**
     * @copydoc VirtualDevice::device_id
     */
    DeviceId device_id() const override;

    /**
     * @copydoc VirtualDevice::profile
     */
    const DeviceProfile &profile() const override;

    /**
     * @copydoc VirtualDevice::is_open
     */
    bool is_open() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Check whether the device is still attached to the driver.
     *
     * A device can stay open after the driver detached its target, for example when
     * the bus service restarts or another process removes it. Hosts poll this to
     * decide whether the device has to be recreated.
     *
     * @return `true` when the device is open and the backend still reports it attached.
     */
    bool is_attached() const;

    /**
     * @brief Re-attach a device the driver dropped, for example after a bus restart.
     *
     * Hosts that keep a pad alive across a driver restart call this once
     * `is_attached()` reports `false`, instead of destroying and recreating the handle.
     * A device that is already attached reports success without doing any work.
     *
     * @return Re-attach operation status.
     */
    OperationStatus reattach();

    /**
     * @brief Submit the latest gamepad input state.
     *
     * @param state Gamepad input state.
     * @return Submit operation status.
     */
    OperationStatus submit(const GamepadState &state);

    /**
     * @brief Register a callback for backend output events.
     *
     * @param callback Output callback copied into the handle. Passing an empty callback clears it.
     */
    void set_output_callback(const OutputCallback &callback);

    /**
     * @brief Dispatch an output event to the registered callback.
     *
     * Backends call this from their notification threads. Consumers may also call
     * it directly to replay feedback while testing.
     *
     * @param output Output event.
     * @return Dispatch operation status.
     */
    OperationStatus dispatch_output(const GamepadOutput &output);

    /**
     * @brief Wait for a pending raw output report.
     *
     * Only profiles that publish raw output reports, currently the DualShock 4,
     * provide them. The call blocks for at most `timeout_ms` milliseconds.
     *
     * @param timeout_ms Maximum wait in milliseconds.
     * @return Raw report event, or `std::nullopt` when none is pending.
     */
    std::optional<GamepadOutput> poll_raw_output(std::uint32_t timeout_ms);

    /**
     * @brief Get the XInput user index of the emulated device.
     *
     * @return Zero-based XInput user index, or `-1` when the profile does not
     * publish one.
     */
    long xinput_user_index() const;

    /**
     * @brief Get the most recently submitted gamepad state.
     *
     * @return Last submitted state.
     */
    GamepadState last_submitted_state() const;

    /**
     * @brief Get the most recently packed input report.
     *
     * The byte layout matches the emulated device input report: a 12-byte Xbox
     * 360 report or the 63-byte DualShock 4 report body.
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
     * @brief Skip submissions that would repeat the previous report.
     *
     * Hosts polling faster than the device needs can drop duplicate frames, but a pad that
     * stops being written to goes stale, so a keepalive flush still submits after the given
     * interval. Timestamp bytes are ignored by the comparison, which keeps the DualShock 4
     * timestamp advancing whenever a frame is actually sent.
     *
     * @param enabled Whether duplicate reports are suppressed.
     * @param keepalive_ms Longest interval without a submission, in milliseconds.
     */
    void set_submit_on_change(bool enabled, std::uint32_t keepalive_ms = 100);

  private:
    std::shared_ptr<detail::GamepadImpl> device_;
  };

  /**
   * @brief Result returned by gamepad creation.
   */
  struct GamepadCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created gamepad handle when creation succeeds.
     */
    std::unique_ptr<Gamepad> gamepad;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && gamepad != nullptr;
    }
  };

  /**
   * @brief Runtime that owns backend state and creates virtual devices.
   */
  class Runtime final {
  public:
    /**
     * @brief Copy construction is disabled because the runtime owns backend state.
     */
    Runtime(const Runtime &) = delete;

    /**
     * @brief Copy assignment is disabled because the runtime owns backend state.
     *
     * @return This runtime.
     */
    Runtime &operator=(const Runtime &) = delete;

    /**
     * @brief Move construct a runtime.
     *
     * @param other Runtime to move from.
     */
    Runtime(Runtime &&other) noexcept;

    /**
     * @brief Move assign a runtime.
     *
     * @param other Runtime to move from.
     * @return This runtime.
     */
    Runtime &operator=(Runtime &&other) noexcept;

    /**
     * @brief Construct a runtime for Runtime::create.
     *
     * @param token Runtime construction token.
     * @param options Runtime configuration.
     */
    Runtime(detail::RuntimeConstructionToken token, RuntimeOptions options);

    /**
     * @brief Destroy the runtime and close any remaining devices.
     */
    ~Runtime();

    /**
     * @brief Get the library version as `major.minor.patch`.
     *
     * The public headers keep a stable layout within a minor series, so a host may keep
     * building against the API it was compiled for. Behaviour is otherwise additive across
     * minor releases and only changes incompatibly on a major bump.
     *
     * @return Version string.
     */
    static const char *version();

    /**
     * @brief Create a runtime with the requested options.
     *
     * @param options Runtime creation options.
     * @return Runtime instance. An unavailable backend still produces a runtime
     * that reports why device creation is impossible.
     */
    static std::unique_ptr<Runtime> create(RuntimeOptions options = {});

    /**
     * @brief Get capabilities for the selected backend.
     *
     * An unavailable backend still produces a runtime so callers can inspect
     * capabilities and report a precise error from the first device creation.
     *
     * @return Backend capabilities.
     */
    const BackendCapabilities &capabilities() const;

    /**
     * @brief Get the backend kind used by this runtime.
     *
     * @return Backend kind.
     */
    BackendKind backend_kind() const;

    /**
     * @brief Create a gamepad from a profile.
     *
     * Identity fields of the supplied profile are used as provided, while capability
     * flags always come from the built-in profile for that kind: callers cannot
     * advertise behavior the driver does not publish.
     *
     * @param profile Device profile.
     * @return Gamepad creation result.
     */
    GamepadCreationResult create_gamepad(const DeviceProfile &profile);

    /**
     * @brief Get the number of open devices owned by the runtime.
     *
     * @return Active device count.
     */
    std::size_t active_device_count() const;

    /**
     * @brief Close every device owned by the runtime.
     */
    void close_all();

  private:
    std::shared_ptr<detail::RuntimeState> state_;
  };

}  // namespace vigem
