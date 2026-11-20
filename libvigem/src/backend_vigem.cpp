// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libvigem/src/backend_vigem.cpp
 * @brief ViGEmBus driver backend that loads the client library at runtime.
 *
 * The backend resolves the `ViGEmClient` C API dynamically so the library keeps
 * building and loading without a link-time dependency on the client SDK, and so
 * a host without the bus driver receives a precise `backend_unavailable` status
 * instead of a load failure.
 */
#include "detail.hpp"

// standard includes
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// platform includes
// Keep the Windows headers from defining min/max macros and unused subsystems that
// would collide with the standard library used above.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace vigem::detail {

  namespace {

    constexpr std::uint32_t kVigemErrorNone = 0x20000000;
    constexpr std::uint32_t kVigemErrorBusNotFound = 0xE0000001;
    constexpr std::uint32_t kVigemErrorNoFreeSlot = 0xE0000002;
    constexpr std::uint32_t kVigemErrorInvalidTarget = 0xE0000003;
    constexpr std::uint32_t kVigemErrorAlreadyConnected = 0xE0000005;
    constexpr std::uint32_t kVigemErrorTargetUninitialized = 0xE0000006;
    constexpr std::uint32_t kVigemErrorTargetNotPluggedIn = 0xE0000007;
    constexpr std::uint32_t kVigemErrorBusAccessFailed = 0xE0000009;
    constexpr std::uint32_t kVigemErrorBusInvalidHandle = 0xE0000013;
    constexpr std::uint32_t kVigemErrorXusbUserIndexOutOfRange = 0xE0000014;
    constexpr std::uint32_t kVigemErrorInvalidParameter = 0xE0000015;
    constexpr std::uint32_t kVigemErrorNotSupported = 0xE0000016;
    constexpr std::uint32_t kVigemErrorTimedOut = 0xE0000018;

    /**
     * @brief XINPUT_GAMEPAD-compatible report layout used by the driver.
     */
    struct XusbReport {
      std::uint16_t wButtons;
      std::uint8_t bLeftTrigger;
      std::uint8_t bRightTrigger;
      std::int16_t sThumbLX;
      std::int16_t sThumbLY;
      std::int16_t sThumbRX;
      std::int16_t sThumbRY;
    };

    /**
     * @brief DualShock 4 Lightbar color value passed to notification callbacks.
     */
    struct Ds4LightbarColor {
      std::uint8_t red;
      std::uint8_t green;
      std::uint8_t blue;
    };

    /**
     * @brief Packed DualShock 4 input report body.
     */
    struct alignas(1) Ds4ReportEx {
      std::uint8_t buffer[63];
    };

    /**
     * @brief Packed DualShock 4 raw output report buffer.
     */
    struct Ds4OutputBuffer {
      std::uint8_t buffer[64];
    };

    static_assert(sizeof(XusbReport) == 12, "XUSB report layout must match the driver ABI");
    static_assert(sizeof(Ds4ReportEx) == 63, "DualShock 4 report layout must match the driver ABI");
    static_assert(sizeof(Ds4LightbarColor) == 3, "Lightbar color layout must match the driver ABI");
    static_assert(sizeof(Ds4OutputBuffer) == 64, "DualShock 4 output buffer must match the driver ABI");

    using X360Notification = VOID(CALLBACK *)(void *client, void *target, UCHAR large_motor, UCHAR small_motor,
                                             UCHAR led_number, void *user_data);
    using Ds4Notification = VOID(CALLBACK *)(void *client, void *target, UCHAR large_motor, UCHAR small_motor,
                                            Ds4LightbarColor lightbar, void *user_data);

    /**
     * @brief Resolved entry points of the ViGEm client library.
     */
    struct ClientApi {
      HMODULE module = nullptr;

      void *(*vigem_alloc)() = nullptr;
      void (*vigem_free)(void *client) = nullptr;
      std::uint32_t (*vigem_connect)(void *client) = nullptr;
      void (*vigem_disconnect)(void *client) = nullptr;

      void *(*vigem_target_x360_alloc)() = nullptr;
      void *(*vigem_target_ds4_alloc)() = nullptr;
      void (*vigem_target_free)(void *target) = nullptr;
      std::uint32_t (*vigem_target_add)(void *client, void *target) = nullptr;
      std::uint32_t (*vigem_target_remove)(void *client, void *target) = nullptr;
      bool (*vigem_target_is_attached)(void *target) = nullptr;
      std::uint32_t (*vigem_target_x360_update)(void *client, void *target, XusbReport report) = nullptr;
      std::uint32_t (*vigem_target_ds4_update_ex)(void *client, void *target, Ds4ReportEx report) = nullptr;
      std::uint32_t (*vigem_target_x360_register_notification)(void *client, void *target,
                                                               X360Notification notification, void *user_data) = nullptr;
      std::uint32_t (*vigem_target_ds4_register_notification)(void *client, void *target,
                                                              Ds4Notification notification, void *user_data) = nullptr;
      void (*vigem_target_x360_unregister_notification)(void *target) = nullptr;
      void (*vigem_target_ds4_unregister_notification)(void *target) = nullptr;
      void (*vigem_target_set_vid)(void *target, std::uint16_t vid) = nullptr;
      void (*vigem_target_set_pid)(void *target, std::uint16_t pid) = nullptr;
      std::uint32_t (*vigem_target_x360_get_user_index)(void *client, void *target, unsigned long *index) = nullptr;
      std::uint32_t (*vigem_target_ds4_await_output_report_timeout)(void *client, void *target,
                                                                    unsigned long milliseconds,
                                                                    Ds4OutputBuffer *buffer) = nullptr;

      bool resolve() {
    // The intermediate void pointer keeps function-pointer casts warning-free on
    // compilers that diagnose direct function type changes.
#define VIGEM_RESOLVE(member)                                                          \
  this->member = reinterpret_cast<decltype(this->member)>(                              \
      reinterpret_cast<void *>(GetProcAddress(module, #member)));                      \
  if (this->member == nullptr) {                                                       \
    return false;                                                                      \
  }

        VIGEM_RESOLVE(vigem_alloc);
        VIGEM_RESOLVE(vigem_free);
        VIGEM_RESOLVE(vigem_connect);
        VIGEM_RESOLVE(vigem_disconnect);
        VIGEM_RESOLVE(vigem_target_x360_alloc);
        VIGEM_RESOLVE(vigem_target_ds4_alloc);
        VIGEM_RESOLVE(vigem_target_free);
        VIGEM_RESOLVE(vigem_target_add);
        VIGEM_RESOLVE(vigem_target_remove);
        VIGEM_RESOLVE(vigem_target_x360_update);
        VIGEM_RESOLVE(vigem_target_ds4_update_ex);
        VIGEM_RESOLVE(vigem_target_x360_register_notification);
        VIGEM_RESOLVE(vigem_target_ds4_register_notification);
        VIGEM_RESOLVE(vigem_target_x360_unregister_notification);
        VIGEM_RESOLVE(vigem_target_ds4_unregister_notification);
        VIGEM_RESOLVE(vigem_target_set_vid);
        VIGEM_RESOLVE(vigem_target_set_pid);

#undef VIGEM_RESOLVE

        // Optional entry points; older client libraries do not export them.
        vigem_target_x360_get_user_index = reinterpret_cast<decltype(vigem_target_x360_get_user_index)>(
            reinterpret_cast<void *>(GetProcAddress(module, "vigem_target_x360_get_user_index")));
        vigem_target_ds4_await_output_report_timeout =
            reinterpret_cast<decltype(vigem_target_ds4_await_output_report_timeout)>(
                reinterpret_cast<void *>(GetProcAddress(module, "vigem_target_ds4_await_output_report_timeout")));
        vigem_target_is_attached = reinterpret_cast<decltype(vigem_target_is_attached)>(
            reinterpret_cast<void *>(GetProcAddress(module, "vigem_target_is_attached")));

        return true;
      }
    };

    std::wstring to_wide(const std::string &value) {
      if (value.empty()) {
        return {};
      }

      const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
      if (length <= 0) {
        return {};
      }

      std::wstring wide(static_cast<std::size_t>(length), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), length);
      // Drop the terminator returned by the conversion.
      wide.resize(static_cast<std::size_t>(length) - 1);
      return wide;
    }

    const char *profile_name(GamepadProfileKind kind) {
      switch (kind) {
        case GamepadProfileKind::xbox_360:
          return "xbox_360";
        case GamepadProfileKind::dualshock4:
          return "dualshock4";
      }

      return "unknown";
    }

    OperationStatus map_error(std::uint32_t error, const char *operation) {
      if (error == kVigemErrorNone) {
        return OperationStatus::success();
      }

      auto failure = [operation, error](ErrorCode code) {
        char message[160] {};
        std::snprintf(message, sizeof(message), "%s failed (ViGEm error 0x%08X)", operation, error);
        // Keep the raw driver code so callers can tell a restarted bus service from a full
        // slot table without parsing the message.
        return OperationStatus::failure(code, message, error);
      };

      switch (error) {
        case kVigemErrorBusNotFound:
        case kVigemErrorBusAccessFailed:
        case kVigemErrorBusInvalidHandle:
        case kVigemErrorAlreadyConnected:
          return failure(ErrorCode::backend_unavailable);
        case kVigemErrorNoFreeSlot:
          return failure(ErrorCode::no_free_slot);
        case kVigemErrorInvalidTarget:
        case kVigemErrorTargetUninitialized:
        case kVigemErrorInvalidParameter:
        case kVigemErrorXusbUserIndexOutOfRange:
          return failure(ErrorCode::invalid_argument);
        case kVigemErrorTargetNotPluggedIn:
          return failure(ErrorCode::device_detached);
        case kVigemErrorNotSupported:
        case kVigemErrorTimedOut:
          return failure(ErrorCode::not_supported);
        default:
          return failure(ErrorCode::backend_failure);
      }
    }

    /**
     * @brief State handed to driver-owned notification threads.
     */
    struct NotificationContext {
      std::weak_ptr<GamepadImpl> device;
    };

    VOID CALLBACK x360_notification(void * /*client*/, void * /*target*/, UCHAR large_motor, UCHAR small_motor,
                                    UCHAR led_number, void *user_data) {
      auto *context = static_cast<NotificationContext *>(user_data);
      if (context == nullptr) {
        return;
      }

      const auto device = context->device.lock();
      if (!device) {
        return;
      }

      GamepadOutput rumble;
      rumble.kind = GamepadOutputKind::rumble;
      rumble.low_frequency_rumble = static_cast<std::uint16_t>(large_motor) * 257u;
      rumble.high_frequency_rumble = static_cast<std::uint16_t>(small_motor) * 257u;
      device->dispatch_output(rumble);

      if (led_number < 4) {
        GamepadOutput leds;
        leds.kind = GamepadOutputKind::player_leds;
        leds.player_leds[led_number] = true;
        device->dispatch_output(leds);
      }
    }

    VOID CALLBACK ds4_notification(void * /*client*/, void * /*target*/, UCHAR large_motor, UCHAR small_motor,
                                   Ds4LightbarColor lightbar, void *user_data) {
      auto *context = static_cast<NotificationContext *>(user_data);
      if (context == nullptr) {
        return;
      }

      const auto device = context->device.lock();
      if (!device) {
        return;
      }

      GamepadOutput rumble;
      rumble.kind = GamepadOutputKind::rumble;
      rumble.low_frequency_rumble = static_cast<std::uint16_t>(large_motor) * 257u;
      rumble.high_frequency_rumble = static_cast<std::uint16_t>(small_motor) * 257u;
      device->dispatch_output(rumble);

      // The lightbar is reported as its own event so consumers can filter on the kind
      // they care about, matching how the Xbox 360 player LEDs are published.
      GamepadOutput led;
      led.kind = GamepadOutputKind::rgb_led;
      led.red = lightbar.red;
      led.green = lightbar.green;
      led.blue = lightbar.blue;
      device->dispatch_output(led);
    }

    /**
     * @brief Driver-backed virtual gamepad.
     */
    class VigemGamepad final: public GamepadImpl {
    public:
      VigemGamepad(DeviceId id, DeviceProfile profile, std::shared_ptr<LogSink> log, ClientApi *api, void *client):
          GamepadImpl(id, profile, std::move(log)), api_(api), client_(client) {}

      ~VigemGamepad() override {
        close();
      }

      /**
       * @brief Allocate, plug in, and subscribe to feedback for the device.
       *
       * @param context Backend-owned notification context that carries the device's
       * weak handle; the device never frees it.
       * @return Attach operation status.
       */
      OperationStatus attach(NotificationContext *context) {
        std::lock_guard<std::mutex> lock(target_mutex_);

        if (profile().kind == GamepadProfileKind::xbox_360) {
          target_ = api_->vigem_target_x360_alloc();
        } else {
          target_ = api_->vigem_target_ds4_alloc();
        }

        if (target_ == nullptr) {
          return OperationStatus::failure(ErrorCode::backend_failure, "target allocation failed");
        }

        if (profile().vendor_id != 0) {
          api_->vigem_target_set_vid(target_, profile().vendor_id);
        }
        if (profile().product_id != 0) {
          api_->vigem_target_set_pid(target_, profile().product_id);
        }

        auto status = map_error(api_->vigem_target_add(client_, target_), "target add");
        if (!status.ok()) {
          return status;
        }

        attached_ = true;
        context_ = context;

        if (profile().kind == GamepadProfileKind::xbox_360) {
          status = map_error(
              api_->vigem_target_x360_register_notification(client_, target_, x360_notification, context_),
              "x360 notification registration");
        } else {
          status = map_error(
              api_->vigem_target_ds4_register_notification(client_, target_, ds4_notification, context_),
              "ds4 notification registration");
        }

        if (!status.ok()) {
          log(LogLevel::warning, "gamepad feedback is unavailable: " + status.message());
          unregister_notification();
        }

        return OperationStatus::success();
      }

      std::optional<GamepadOutput> poll_raw_output(std::uint32_t timeout_ms) override {
        // Check liveness before taking the target lock so the lock order stays
        // consistent with close(), which takes the device lock first.
        if (profile().kind != GamepadProfileKind::dualshock4 || !is_open()) {
          return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(target_mutex_);
        if (target_ == nullptr || api_->vigem_target_ds4_await_output_report_timeout == nullptr) {
          return std::nullopt;
        }

        Ds4OutputBuffer buffer {};
        const auto error = api_->vigem_target_ds4_await_output_report_timeout(client_, target_, timeout_ms, &buffer);
        if (error != kVigemErrorNone) {
          return std::nullopt;
        }

        GamepadOutput output;
        output.kind = GamepadOutputKind::raw_report;
        output.raw_report.assign(buffer.buffer, buffer.buffer + sizeof(buffer.buffer));
        return output;
      }

      long xinput_user_index() const override {
        if (profile().kind != GamepadProfileKind::xbox_360 || !is_open()) {
          return -1;
        }

        std::lock_guard<std::mutex> lock(target_mutex_);
        if (target_ == nullptr || api_->vigem_target_x360_get_user_index == nullptr) {
          return -1;
        }

        unsigned long index = 0;
        if (api_->vigem_target_x360_get_user_index(client_, target_, &index) != kVigemErrorNone) {
          return -1;
        }

        return static_cast<long>(index);
      }

    private:
      bool attached() const override {
        std::lock_guard<std::mutex> lock(target_mutex_);
        if (!attached_ || target_ == nullptr) {
          return false;
        }

        if (api_->vigem_target_is_attached == nullptr) {
          return true;
        }

        return api_->vigem_target_is_attached(target_) != FALSE;
      }

      OperationStatus on_reattach() override {
        std::lock_guard<std::mutex> lock(target_mutex_);
        if (target_ == nullptr) {
          return OperationStatus::failure(ErrorCode::device_closed, "gamepad target is not allocated");
        }

        auto status = map_error(api_->vigem_target_add(client_, target_), "target re-add");

        // A restarted bus service invalidates the client connection as well, so reconnect
        // once and retry before reporting the device as lost.
        if (status.code() == ErrorCode::backend_unavailable && api_->vigem_disconnect != nullptr) {
          api_->vigem_disconnect(client_);
          if (map_error(api_->vigem_connect(client_), "reconnect").ok()) {
            status = map_error(api_->vigem_target_add(client_, target_), "target re-add");
          }
        }

        if (!status.ok()) {
          return status;
        }

        attached_ = true;
        log(LogLevel::info, "gamepad re-attached to the driver");
        return OperationStatus::success();
      }

      OperationStatus on_submit(const GamepadState & /*state*/, const std::vector<std::uint8_t> &packed) override {
        // Serialized against close(): a submit that raced a close must not call into
        // the driver with a target that is being freed.
        std::lock_guard<std::mutex> lock(target_mutex_);
        if (target_ == nullptr || !attached_) {
          return OperationStatus::failure(ErrorCode::device_detached, "gamepad target is not attached");
        }

        if (profile().kind == GamepadProfileKind::xbox_360) {
          if (packed.size() != sizeof(XusbReport)) {
            return OperationStatus::failure(ErrorCode::backend_failure, "unexpected Xbox 360 report size");
          }

          XusbReport report {};
          std::memcpy(&report, packed.data(), sizeof(report));
          return map_error(api_->vigem_target_x360_update(client_, target_, report), "x360 update");
        }

        if (packed.size() != sizeof(Ds4ReportEx)) {
          return OperationStatus::failure(ErrorCode::backend_failure, "unexpected DualShock 4 report size");
        }

        Ds4ReportEx report {};
        std::memcpy(&report, packed.data(), sizeof(report));
        return map_error(api_->vigem_target_ds4_update_ex(client_, target_, report), "ds4 update");
      }

      OperationStatus on_close() override {
        std::lock_guard<std::mutex> lock(target_mutex_);
        unregister_notification();

        if (target_ != nullptr) {
          if (attached_ && api_->vigem_target_remove != nullptr) {
            const auto status = map_error(api_->vigem_target_remove(client_, target_), "target remove");
            if (!status.ok()) {
              log(LogLevel::warning, "gamepad detach failed: " + status.message());
            }
          }
          api_->vigem_target_free(target_);
          target_ = nullptr;
          attached_ = false;
        }

        return OperationStatus::success();
      }

      /**
       * @brief Stop driver feedback delivery. The caller holds target_mutex_.
       */
      void unregister_notification() {
        if (context_ != nullptr) {
          // The client library does not wait for driver notification threads to
          // finish, so the context stays alive in the backend and is only released
          // once the backend is torn down. Freeing it here could race a callback
          // that is still inside the driver thread.
          if (profile().kind == GamepadProfileKind::xbox_360) {
            api_->vigem_target_x360_unregister_notification(target_);
          } else {
            api_->vigem_target_ds4_unregister_notification(target_);
          }
          context_ = nullptr;
        }
      }

      ClientApi *api_ = nullptr;
      void *client_ = nullptr;
      // Guards target_ against a close() racing an in-flight submit or poll.
      mutable std::mutex target_mutex_;
      void *target_ = nullptr;
      bool attached_ = false;
      NotificationContext *context_ = nullptr;
    };

    /**
     * @brief ViGEmBus driver backend.
     */
    class VigemBackend final: public Backend {
    public:
      VigemBackend(const RuntimeOptions &options, std::shared_ptr<LogSink> log) {
        set_log(log);

        BackendCapabilities capabilities;
        capabilities.backend_name = "vigem-driver";
        capabilities.requires_installed_driver = true;

        const auto library = options.client_library_path.empty() ? std::wstring(L"ViGEmClient.dll")
                                                                : to_wide(options.client_library_path);
        api_.module = LoadLibraryW(library.c_str());
        if (api_.module == nullptr) {
          unavailable_reason_ = "ViGEmClient.dll could not be loaded";
          set_capabilities(std::move(capabilities));
          log->write(LogLevel::warning, unavailable_reason_);
          return;
        }

        if (!api_.resolve()) {
          unavailable_reason_ = "ViGEmClient.dll does not export the expected API";
          set_capabilities(std::move(capabilities));
          log->write(LogLevel::error, unavailable_reason_);
          return;
        }

        client_ = api_.vigem_alloc();
        if (client_ == nullptr) {
          unavailable_reason_ = "ViGEm client allocation failed";
          set_capabilities(std::move(capabilities));
          log->write(LogLevel::error, unavailable_reason_);
          return;
        }

        const auto error = api_.vigem_connect(client_);
        if (error != kVigemErrorNone) {
          unavailable_reason_ = map_error(error, "bus connect").message();
          set_capabilities(std::move(capabilities));
          log->write(LogLevel::warning, "ViGEm bus driver unavailable: " + unavailable_reason_);
          return;
        }

        connected_ = true;
        capabilities.available = true;
        capabilities.supports_gamepad = true;
        capabilities.supports_output_reports = true;
        // Older client libraries do not export the raw output report API.
        capabilities.supports_raw_output_reports = api_.vigem_target_ds4_await_output_report_timeout != nullptr;
        set_capabilities(std::move(capabilities));
      }

      ~VigemBackend() override {
        close_all();

        if (client_ != nullptr) {
          if (connected_) {
            api_.vigem_disconnect(client_);
            connected_ = false;
          }
          api_.vigem_free(client_);
          client_ = nullptr;
        }

        if (api_.module != nullptr) {
          FreeLibrary(api_.module);
          api_.module = nullptr;
        }

        // Released last: notification threads can only be gone once the client
        // connection and every target are gone.
        std::lock_guard<std::mutex> lock(context_mutex_);
        notification_contexts_.clear();
      }

      GamepadCreation create_gamepad(DeviceId id, const DeviceProfile &profile) override {
        // A client object exists even when the bus connection failed, so the
        // connection state, not the pointer, decides whether devices can be added.
        if (!connected_ || client_ == nullptr) {
          return {OperationStatus::failure(ErrorCode::backend_unavailable, unavailable_reason_), nullptr};
        }

        auto context = std::make_unique<NotificationContext>();
        auto device = std::make_shared<VigemGamepad>(id, profile, log(), &api_, client_);
        context->device = device;

        NotificationContext *raw_context = context.get();
        {
          std::lock_guard<std::mutex> lock(context_mutex_);
          notification_contexts_.push_back(std::move(context));
        }

        auto status = device->attach(raw_context);
        if (!status.ok()) {
          device->close();
          return {status, nullptr};
        }

        track(device);
        log()->write(LogLevel::debug, std::string("gamepad attached with profile ") + profile_name(profile.kind));
        return {OperationStatus::success(), device};
      }

    private:
      ClientApi api_;
      void *client_ = nullptr;
      bool connected_ = false;
      std::string unavailable_reason_ = "ViGEm bus driver unavailable";
      std::mutex context_mutex_;
      std::vector<std::unique_ptr<NotificationContext>> notification_contexts_;
    };

  }  // namespace

  std::unique_ptr<Backend> make_platform_backend(const RuntimeOptions &options, std::shared_ptr<LogSink> log) {
    return std::make_unique<VigemBackend>(options, std::move(log));
  }

}  // namespace vigem::detail
