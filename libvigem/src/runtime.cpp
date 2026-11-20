/**
 * @file libvigem/src/runtime.cpp
 * @brief Runtime, device handles, the in-memory backend, and report packing.
 */
#include "detail.hpp"

// standard includes
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace vigem::detail {

  namespace {

    // Xbox 360 button bits, mirroring XUSB_BUTTON in ViGEm/Common.h.
    constexpr std::uint16_t kXusbDpadUp = 0x0001;
    constexpr std::uint16_t kXusbDpadDown = 0x0002;
    constexpr std::uint16_t kXusbDpadLeft = 0x0004;
    constexpr std::uint16_t kXusbDpadRight = 0x0008;
    constexpr std::uint16_t kXusbStart = 0x0010;
    constexpr std::uint16_t kXusbBack = 0x0020;
    constexpr std::uint16_t kXusbLeftThumb = 0x0040;
    constexpr std::uint16_t kXusbRightThumb = 0x0080;
    constexpr std::uint16_t kXusbLeftShoulder = 0x0100;
    constexpr std::uint16_t kXusbRightShoulder = 0x0200;
    constexpr std::uint16_t kXusbGuide = 0x0400;
    constexpr std::uint16_t kXusbA = 0x1000;
    constexpr std::uint16_t kXusbB = 0x2000;
    constexpr std::uint16_t kXusbX = 0x4000;
    constexpr std::uint16_t kXusbY = 0x8000;

    // DualShock 4 button bits, mirroring DS4_BUTTONS in ViGEm/Common.h.
    constexpr std::uint16_t kDs4ThumbRight = 1u << 15;
    constexpr std::uint16_t kDs4ThumbLeft = 1u << 14;
    constexpr std::uint16_t kDs4Options = 1u << 13;
    constexpr std::uint16_t kDs4Share = 1u << 12;
    constexpr std::uint16_t kDs4TriggerRight = 1u << 11;
    constexpr std::uint16_t kDs4TriggerLeft = 1u << 10;
    constexpr std::uint16_t kDs4ShoulderRight = 1u << 9;
    constexpr std::uint16_t kDs4ShoulderLeft = 1u << 8;
    constexpr std::uint16_t kDs4Triangle = 1u << 7;
    constexpr std::uint16_t kDs4Circle = 1u << 6;
    constexpr std::uint16_t kDs4Cross = 1u << 5;
    constexpr std::uint16_t kDs4Square = 1u << 4;

    // DualShock 4 special button bits, mirroring DS4_SPECIAL_BUTTONS.
    constexpr std::uint8_t kDs4SpecialPs = 1u << 0;
    constexpr std::uint8_t kDs4SpecialTouchpad = 1u << 1;

    // DualShock 4 directional hat values, mirroring DS4_DPAD_DIRECTIONS.
    constexpr std::uint8_t kDs4DpadNone = 0x8;
    constexpr std::uint8_t kDs4DpadNorthWest = 0x7;
    constexpr std::uint8_t kDs4DpadWest = 0x6;
    constexpr std::uint8_t kDs4DpadSouthWest = 0x5;
    constexpr std::uint8_t kDs4DpadSouth = 0x4;
    constexpr std::uint8_t kDs4DpadSouthEast = 0x3;
    constexpr std::uint8_t kDs4DpadEast = 0x2;
    constexpr std::uint8_t kDs4DpadNorthEast = 0x1;
    constexpr std::uint8_t kDs4DpadNorth = 0x0;

    // DualShock 4 touchpad resolution is 1920x943, so the addressable extremes are
    // 1919 and 942.
    constexpr float kDs4TouchMaxX = 1919.0F;
    constexpr float kDs4TouchMaxY = 942.0F;

    // DualShock 4 status byte (offset 29): bit 4 marks the attached USB cable and the
    // low nibble holds the battery capacity, where 11 is the driver's "full" marker.
    // 0x1B is the wired-and-full value the bus driver itself publishes in its default
    // report (sys/Ds4Pdo.cpp) and follows the encoding documented by Linux
    // hid-playstation (DS4_STATUS0_BATTERY_CAPACITY / DS4_STATUS0_CABLE_STATE).
    constexpr std::uint8_t kDs4StatusWiredFull = 0x1B;

    // Sensor temperature byte (offset 11). The value is what the bus driver's default
    // report carries; battery state lives in the status byte at offset 29.
    constexpr std::uint8_t kDs4TemperatureByte = 0x06;

    constexpr std::size_t kXusbReportSize = 12;
    constexpr std::size_t kDs4ReportSize = 63;

    float clamp_axis(float value) {
      if (!std::isfinite(value)) {
        return 0.0F;
      }
      return std::clamp(value, -1.0F, 1.0F);
    }

    float clamp_unit(float value) {
      if (!std::isfinite(value)) {
        return 0.0F;
      }
      return std::clamp(value, 0.0F, 1.0F);
    }

    std::int16_t to_xusb_axis(float value) {
      return static_cast<std::int16_t>(std::lround(clamp_axis(value) * 32767.0F));
    }

    std::uint8_t to_unsigned_axis(float value) {
      return static_cast<std::uint8_t>(std::lround((clamp_axis(value) + 1.0F) * 127.5F));
    }

    std::uint8_t to_trigger_byte(float value) {
      return static_cast<std::uint8_t>(std::lround(clamp_unit(value) * 255.0F));
    }

    std::int16_t to_sensor_axis(float value) {
      if (!std::isfinite(value)) {
        return 0;
      }
      const auto bounded = std::clamp(value, -32768.0F, 32767.0F);
      return static_cast<std::int16_t>(std::lround(bounded));
    }

    void write_le16(std::uint8_t *target, std::uint16_t value) {
      target[0] = static_cast<std::uint8_t>(value & 0xFFu);
      target[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    }

    void write_le16s(std::uint8_t *target, std::int16_t value) {
      write_le16(target, static_cast<std::uint16_t>(value));
    }

    std::uint16_t xusb_buttons(const ButtonSet &buttons) {
      std::uint16_t mask = 0;
      if (buttons.test(GamepadButton::dpad_up)) {
        mask |= kXusbDpadUp;
      }
      if (buttons.test(GamepadButton::dpad_down)) {
        mask |= kXusbDpadDown;
      }
      if (buttons.test(GamepadButton::dpad_left)) {
        mask |= kXusbDpadLeft;
      }
      if (buttons.test(GamepadButton::dpad_right)) {
        mask |= kXusbDpadRight;
      }
      if (buttons.test(GamepadButton::start)) {
        mask |= kXusbStart;
      }
      if (buttons.test(GamepadButton::back)) {
        mask |= kXusbBack;
      }
      if (buttons.test(GamepadButton::left_stick)) {
        mask |= kXusbLeftThumb;
      }
      if (buttons.test(GamepadButton::right_stick)) {
        mask |= kXusbRightThumb;
      }
      if (buttons.test(GamepadButton::left_shoulder)) {
        mask |= kXusbLeftShoulder;
      }
      if (buttons.test(GamepadButton::right_shoulder)) {
        mask |= kXusbRightShoulder;
      }
      if (buttons.test(GamepadButton::guide)) {
        mask |= kXusbGuide;
      }
      if (buttons.test(GamepadButton::a)) {
        mask |= kXusbA;
      }
      if (buttons.test(GamepadButton::b)) {
        mask |= kXusbB;
      }
      if (buttons.test(GamepadButton::x)) {
        mask |= kXusbX;
      }
      if (buttons.test(GamepadButton::y)) {
        mask |= kXusbY;
      }
      return mask;
    }

    std::uint16_t ds4_buttons(const ButtonSet &buttons, float left_trigger, float right_trigger) {
      std::uint16_t mask = 0;
      if (buttons.test(GamepadButton::left_stick)) {
        mask |= kDs4ThumbLeft;
      }
      if (buttons.test(GamepadButton::right_stick)) {
        mask |= kDs4ThumbRight;
      }
      if (buttons.test(GamepadButton::start)) {
        mask |= kDs4Options;
      }
      if (buttons.test(GamepadButton::back)) {
        mask |= kDs4Share;
      }
      // Real hardware sets the digital trigger bit as soon as the axis reports any
      // pressure, so a game binding L2/R2 as a button reacts immediately. Basing the
      // bit on the packed analog byte keeps the button and the axis consistent.
      if (to_trigger_byte(left_trigger) > 0) {
        mask |= kDs4TriggerLeft;
      }
      if (to_trigger_byte(right_trigger) > 0) {
        mask |= kDs4TriggerRight;
      }
      if (buttons.test(GamepadButton::left_shoulder)) {
        mask |= kDs4ShoulderLeft;
      }
      if (buttons.test(GamepadButton::right_shoulder)) {
        mask |= kDs4ShoulderRight;
      }
      if (buttons.test(GamepadButton::y)) {
        mask |= kDs4Triangle;
      }
      if (buttons.test(GamepadButton::b)) {
        mask |= kDs4Circle;
      }
      if (buttons.test(GamepadButton::a)) {
        mask |= kDs4Cross;
      }
      if (buttons.test(GamepadButton::x)) {
        mask |= kDs4Square;
      }
      return mask;
    }

    std::uint8_t ds4_dpad(const ButtonSet &buttons) {
      const bool up = buttons.test(GamepadButton::dpad_up);
      const bool down = buttons.test(GamepadButton::dpad_down);
      const bool left = buttons.test(GamepadButton::dpad_left);
      const bool right = buttons.test(GamepadButton::dpad_right);

      if (up && left) {
        return kDs4DpadNorthWest;
      }
      if (up && right) {
        return kDs4DpadNorthEast;
      }
      if (down && left) {
        return kDs4DpadSouthWest;
      }
      if (down && right) {
        return kDs4DpadSouthEast;
      }
      if (up) {
        return kDs4DpadNorth;
      }
      if (right) {
        return kDs4DpadEast;
      }
      if (down) {
        return kDs4DpadSouth;
      }
      if (left) {
        return kDs4DpadWest;
      }
      return kDs4DpadNone;
    }

    std::uint8_t ds4_special(const ButtonSet &buttons) {
      std::uint8_t mask = 0;
      if (buttons.test(GamepadButton::guide)) {
        mask |= kDs4SpecialPs;
      }
      if (buttons.test(GamepadButton::touchpad)) {
        mask |= kDs4SpecialTouchpad;
      }
      return mask;
    }

    void write_touch(std::uint8_t *target, const GamepadTouchContact &contact) {
      // bPacketCounter, bIsUpTrackingNum1, bTouchData1[3], bIsUpTrackingNum2, bTouchData2[3]
      std::memset(target, 0, 9);
      if (!contact.active) {
        // A lifted contact reports the "up" flag in the high bit of the tracking number.
        target[1] = 0x80;
        return;
      }

      target[1] = static_cast<std::uint8_t>(contact.id & 0x7Fu);
      const auto x = static_cast<std::uint16_t>(std::lround(clamp_unit(contact.x) * kDs4TouchMaxX));
      const auto y = static_cast<std::uint16_t>(std::lround(clamp_unit(contact.y) * kDs4TouchMaxY));
      target[2] = static_cast<std::uint8_t>(x & 0xFFu);
      target[3] = static_cast<std::uint8_t>(((x >> 8) & 0x0Fu) | ((y & 0x0Fu) << 4));
      target[4] = static_cast<std::uint8_t>((y >> 4) & 0xFFu);
      // The second contact of the same frame stays inactive.
      target[5] = 0x80;
    }

  }  // namespace

  PackedReport pack_report(const GamepadState &state, GamepadProfileKind kind) {
    PackedReport packed;

    if (kind == GamepadProfileKind::xbox_360) {
      packed.bytes.assign(kXusbReportSize, 0);
      write_le16(packed.bytes.data(), xusb_buttons(state.buttons));
      packed.bytes[2] = to_trigger_byte(state.left_trigger);
      packed.bytes[3] = to_trigger_byte(state.right_trigger);
      write_le16s(packed.bytes.data() + 4, to_xusb_axis(state.left_stick.x));
      write_le16s(packed.bytes.data() + 6, to_xusb_axis(state.left_stick.y));
      write_le16s(packed.bytes.data() + 8, to_xusb_axis(state.right_stick.x));
      write_le16s(packed.bytes.data() + 10, to_xusb_axis(state.right_stick.y));
      return packed;
    }

    packed.bytes.assign(kDs4ReportSize, 0);
    auto *bytes = packed.bytes.data();
    // DualShock 4 axis bytes count from the left and from the top of the stick range,
    // so the vertical axes are inverted relative to the normalized positive-is-up model
    // (the Xbox 360 report carries them as signed values where positive is up).
    bytes[0] = to_unsigned_axis(state.left_stick.x);
    bytes[1] = to_unsigned_axis(-state.left_stick.y);
    bytes[2] = to_unsigned_axis(state.right_stick.x);
    bytes[3] = to_unsigned_axis(-state.right_stick.y);

    auto buttons = ds4_buttons(state.buttons, state.left_trigger, state.right_trigger);
    buttons = static_cast<std::uint16_t>((buttons & ~0x000Fu) | ds4_dpad(state.buttons));
    write_le16(bytes + 4, buttons);

    bytes[6] = ds4_special(state.buttons);
    bytes[7] = to_trigger_byte(state.left_trigger);
    bytes[8] = to_trigger_byte(state.right_trigger);
    // bytes[9..10] hold the report timestamp and stay zero.
    bytes[11] = kDs4TemperatureByte;
    // bytes[24..28] are reserved and byte 29 carries the wired/full battery status.
    bytes[29] = kDs4StatusWiredFull;

    if (state.acceleration) {
      write_le16s(bytes + 18, to_sensor_axis(state.acceleration->x));
      write_le16s(bytes + 20, to_sensor_axis(state.acceleration->y));
      write_le16s(bytes + 22, to_sensor_axis(state.acceleration->z));
    }
    if (state.gyroscope) {
      write_le16s(bytes + 12, to_sensor_axis(state.gyroscope->x));
      write_le16s(bytes + 14, to_sensor_axis(state.gyroscope->y));
      write_le16s(bytes + 16, to_sensor_axis(state.gyroscope->z));
    }

    const auto &contacts = state.touchpad_contacts;
    const GamepadTouchContact *active = nullptr;
    for (const auto &contact : contacts) {
      if (contact.active) {
        active = &contact;
        break;
      }
    }

    // Publish one 9-byte touch packet per report, as the reference DualShock 4 emulation
    // in Sunshine and DS4Windows do. A lifted contact keeps the packet and sets the up
    // flag in the tracking number, so a consumer releases the previous contact instead
    // of receiving no touch update at all.
    bytes[32] = 1;
    write_touch(bytes + 33, active != nullptr ? *active : GamepadTouchContact {});
    return packed;
  }

  LogSink::LogSink(LogCallback callback): callback_(std::move(callback)) {}

  void LogSink::write(LogLevel level, const std::string &message) {
    // Copy the callback out of the lock before invoking it: consumer callbacks may
    // call back into the runtime (for example by creating a device), which would
    // self-deadlock on this mutex.
    LogCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (disabled_ || !callback_) {
        return;
      }
      callback = callback_;
    }

    try {
      callback(level, message);
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex_);
      disabled_ = true;
    }
  }

  GamepadImpl::GamepadImpl(DeviceId id, DeviceProfile profile, std::shared_ptr<LogSink> log):
      id_(id), profile_(std::move(profile)), log_(std::move(log)) {}

  DeviceId GamepadImpl::device_id() const {
    return id_;
  }

  const DeviceProfile &GamepadImpl::profile() const {
    return profile_;
  }

  bool GamepadImpl::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
  }

  OperationStatus GamepadImpl::close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!open_) {
        return OperationStatus::success();
      }
      open_ = false;
    }

    auto status = on_close();
    if (!status.ok()) {
      log(LogLevel::warning, "device close failed: " + status.message());
    }

    return status;
  }

  OperationStatus GamepadImpl::submit(const GamepadState &state) {
    if (!is_open()) {
      return OperationStatus::failure(ErrorCode::device_closed, "gamepad is closed");
    }

    auto packed = pack_report(state, profile_.kind);

    auto status = on_submit(state, packed.bytes);
    if (!status.ok()) {
      return status;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_state_ = state;
      last_report_ = std::move(packed.bytes);
      ++submit_count_;
    }

    return OperationStatus::success();
  }

  OperationStatus GamepadImpl::dispatch_output(const GamepadOutput &output) {
    OutputCallback callback;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback = callback_;
    }

    if (!callback) {
      return OperationStatus::success();
    }

    try {
      callback(output);
    } catch (...) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback_ = nullptr;
      log(LogLevel::warning, "output callback threw and was disabled");
    }

    return OperationStatus::success();
  }

  std::optional<GamepadOutput> GamepadImpl::poll_raw_output(std::uint32_t /*timeout_ms*/) {
    return std::nullopt;
  }

  long GamepadImpl::xinput_user_index() const {
    return -1;
  }

  GamepadState GamepadImpl::last_submitted_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_state_;
  }

  std::vector<std::uint8_t> GamepadImpl::last_input_report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_report_;
  }

  std::size_t GamepadImpl::submit_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return submit_count_;
  }

  void GamepadImpl::set_output_callback(const OutputCallback &callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = callback;
  }

  void GamepadImpl::log(LogLevel level, const std::string &message) const {
    if (log_) {
      log_->write(level, message);
    }
  }

  const BackendCapabilities &Backend::capabilities() const {
    return capabilities_;
  }

  std::size_t Backend::active_device_count() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    std::size_t count = 0;
    for (const auto &entry : devices_) {
      auto device = entry.lock();
      if (device && device->is_open()) {
        ++count;
      }
    }
    return count;
  }

  void Backend::close_all() {
    std::vector<std::shared_ptr<GamepadImpl>> devices;
    {
      std::lock_guard<std::mutex> lock(devices_mutex_);
      for (const auto &entry : devices_) {
        if (auto device = entry.lock()) {
          devices.push_back(std::move(device));
        }
      }
      devices_.clear();
    }

    for (auto &device : devices) {
      device->close();
    }
  }

  void Backend::track(const std::shared_ptr<GamepadImpl> &device) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    // Drop handles for devices the caller already destroyed so a long-lived runtime
    // does not accumulate an entry for every device it ever created.
    devices_.erase(std::remove_if(devices_.begin(), devices_.end(),
                                  [](const std::weak_ptr<GamepadImpl> &entry) { return entry.expired(); }),
                   devices_.end());
    devices_.push_back(device);
  }

  std::shared_ptr<LogSink> Backend::log() const {
    return log_;
  }

  void Backend::set_log(std::shared_ptr<LogSink> log) {
    log_ = std::move(log);
  }

  void Backend::set_capabilities(BackendCapabilities capabilities) {
    capabilities_ = std::move(capabilities);
  }

  namespace {

    /**
     * @brief In-memory device used by the fake backend.
     */
    class FakeGamepad final: public GamepadImpl {
    public:
      FakeGamepad(DeviceId id, DeviceProfile profile, std::shared_ptr<LogSink> log):
          GamepadImpl(id, profile, std::move(log)) {}

    private:
      OperationStatus on_submit(const GamepadState & /*state*/, const std::vector<std::uint8_t> & /*packed*/) override {
        return OperationStatus::success();
      }

      OperationStatus on_close() override {
        return OperationStatus::success();
      }
    };

    /**
     * @brief Backend that keeps devices in memory for tests and API validation.
     */
    class FakeBackend final: public Backend {
    public:
      explicit FakeBackend(std::shared_ptr<LogSink> log) {
        set_log(std::move(log));
        BackendCapabilities capabilities;
        capabilities.backend_name = "fake";
        capabilities.available = true;
        capabilities.supports_gamepad = true;
        capabilities.supports_output_reports = true;
        capabilities.supports_raw_output_reports = false;
        capabilities.requires_installed_driver = false;
        set_capabilities(std::move(capabilities));
      }

      GamepadCreation create_gamepad(DeviceId id, const DeviceProfile &profile) override {
        if (!profiles::gamepad_profile(profile.kind)) {
          return {OperationStatus::failure(ErrorCode::unsupported_profile, "unsupported gamepad profile"),
                  nullptr};
        }

        auto device = std::make_shared<FakeGamepad>(id, profile, log());
        track(device);
        log()->write(LogLevel::debug, "fake gamepad created");
        return {OperationStatus::success(), device};
      }
    };

    /**
     * @brief Backend that reports an unavailable platform stack.
     */
    class UnavailableBackend final: public Backend {
    public:
      UnavailableBackend(std::shared_ptr<LogSink> log, std::string reason): reason_(std::move(reason)) {
        set_log(std::move(log));
        BackendCapabilities capabilities;
        capabilities.backend_name = "unavailable";
        capabilities.available = false;
        capabilities.supports_gamepad = false;
        capabilities.requires_installed_driver = true;
        set_capabilities(std::move(capabilities));
      }

      GamepadCreation create_gamepad(DeviceId /*id*/, const DeviceProfile & /*profile*/) override {
        return {OperationStatus::failure(ErrorCode::backend_unavailable, reason_), nullptr};
      }

    private:
      std::string reason_;
    };

  }  // namespace

#if !defined(_WIN32)
  std::unique_ptr<Backend> make_platform_backend(const RuntimeOptions & /*options*/, std::shared_ptr<LogSink> log) {
    return std::make_unique<UnavailableBackend>(std::move(log), "the ViGEmBus driver backend requires Windows");
  }
#endif

  RuntimeState::RuntimeState(RuntimeOptions options): kind_(options.backend) {
    auto log = std::make_shared<LogSink>(options.log_callback);

    if (kind_ == BackendKind::fake) {
      backend_ = std::make_unique<FakeBackend>(log);
    } else {
      backend_ = make_platform_backend(options, log);
    }

    if (!backend_) {
      backend_ = std::make_unique<UnavailableBackend>(log, "no backend is available");
    }

    log->write(LogLevel::info, std::string("runtime ready: ") + backend_->capabilities().backend_name);
  }

  RuntimeState::~RuntimeState() {
    close_all();
  }

  const BackendCapabilities &RuntimeState::capabilities() const {
    return backend_->capabilities();
  }

  BackendKind RuntimeState::backend_kind() const {
    return kind_;
  }

  GamepadCreationResult RuntimeState::create_gamepad(const DeviceProfile &requested) {
    auto profile = profiles::gamepad_profile(requested.kind);
    if (!profile) {
      return {OperationStatus::failure(ErrorCode::unsupported_profile, "unsupported gamepad profile"), nullptr};
    }

    // Keep the requested identity while using the effective capability set.
    profile->vendor_id = requested.vendor_id != 0 ? requested.vendor_id : profile->vendor_id;
    profile->product_id = requested.product_id != 0 ? requested.product_id : profile->product_id;

    auto creation = backend_->create_gamepad(next_id_.fetch_add(1), *profile);
    if (!creation.device) {
      if (creation.status.ok()) {
        return {OperationStatus::failure(ErrorCode::backend_failure, "backend produced no device"), nullptr};
      }
      return {creation.status, nullptr};
    }

    GamepadCreationResult result;
    result.status = creation.status;
    result.gamepad = std::unique_ptr<Gamepad>(new Gamepad(RuntimeConstructionToken {}, creation.device));
    return result;
  }

  std::size_t RuntimeState::active_device_count() const {
    return backend_->active_device_count();
  }

  void RuntimeState::close_all() {
    if (backend_) {
      backend_->close_all();
    }
  }

}  // namespace vigem::detail

namespace vigem {

  Gamepad::Gamepad(detail::RuntimeConstructionToken /*token*/, std::shared_ptr<detail::GamepadImpl> device):
      device_(std::move(device)) {}

  Gamepad::Gamepad(Gamepad &&other) noexcept: device_(std::move(other.device_)) {}

  Gamepad &Gamepad::operator=(Gamepad &&other) noexcept {
    if (this != &other) {
      if (device_) {
        device_->close();
      }
      device_ = std::move(other.device_);
    }
    return *this;
  }

  Gamepad::~Gamepad() {
    if (device_) {
      device_->close();
    }
  }

  DeviceId Gamepad::device_id() const {
    return device_ ? device_->device_id() : 0;
  }

  const DeviceProfile &Gamepad::profile() const {
    static const DeviceProfile empty;
    return device_ ? device_->profile() : empty;
  }

  bool Gamepad::is_open() const {
    return device_ && device_->is_open();
  }

  OperationStatus Gamepad::close() {
    if (!device_) {
      return OperationStatus::success();
    }
    return device_->close();
  }

  OperationStatus Gamepad::submit(const GamepadState &state) {
    if (!device_) {
      return OperationStatus::failure(ErrorCode::device_closed, "gamepad handle is empty");
    }
    return device_->submit(state);
  }

  void Gamepad::set_output_callback(const OutputCallback &callback) {
    if (device_) {
      device_->set_output_callback(callback);
    }
  }

  OperationStatus Gamepad::dispatch_output(const GamepadOutput &output) {
    if (!device_) {
      return OperationStatus::failure(ErrorCode::device_closed, "gamepad handle is empty");
    }
    return device_->dispatch_output(output);
  }

  std::optional<GamepadOutput> Gamepad::poll_raw_output(std::uint32_t timeout_ms) {
    if (!device_) {
      return std::nullopt;
    }
    return device_->poll_raw_output(timeout_ms);
  }

  long Gamepad::xinput_user_index() const {
    return device_ ? device_->xinput_user_index() : -1;
  }

  GamepadState Gamepad::last_submitted_state() const {
    return device_ ? device_->last_submitted_state() : GamepadState {};
  }

  std::vector<std::uint8_t> Gamepad::last_input_report() const {
    return device_ ? device_->last_input_report() : std::vector<std::uint8_t> {};
  }

  std::size_t Gamepad::submit_count() const {
    return device_ ? device_->submit_count() : 0;
  }

  Runtime::Runtime(detail::RuntimeConstructionToken /*token*/, RuntimeOptions options):
      state_(std::make_shared<detail::RuntimeState>(std::move(options))) {}

  Runtime::Runtime(Runtime &&other) noexcept: state_(std::move(other.state_)) {}

  Runtime &Runtime::operator=(Runtime &&other) noexcept {
    if (this != &other) {
      state_ = std::move(other.state_);
    }
    return *this;
  }

  Runtime::~Runtime() = default;

  std::unique_ptr<Runtime> Runtime::create(RuntimeOptions options) {
    return std::unique_ptr<Runtime>(new Runtime(detail::RuntimeConstructionToken {}, std::move(options)));
  }

  const BackendCapabilities &Runtime::capabilities() const {
    return state_->capabilities();
  }

  BackendKind Runtime::backend_kind() const {
    return state_->backend_kind();
  }

  GamepadCreationResult Runtime::create_gamepad(const DeviceProfile &profile) {
    return state_->create_gamepad(profile);
  }

  std::size_t Runtime::active_device_count() const {
    return state_->active_device_count();
  }

  void Runtime::close_all() {
    state_->close_all();
  }

  GamepadStateAdapter::GamepadStateAdapter(std::unique_ptr<Gamepad> gamepad):
      gamepad_(std::move(gamepad)), support_(gamepad_ ? gamepad_profile_support(gamepad_->profile())
                                                      : GamepadProfileSupport {}) {}

  GamepadStateAdapter::GamepadStateAdapter(GamepadStateAdapter &&other) noexcept:
      gamepad_(std::move(other.gamepad_)), state_(other.state_), support_(other.support_) {}

  GamepadStateAdapter &GamepadStateAdapter::operator=(GamepadStateAdapter &&other) noexcept {
    if (this != &other) {
      gamepad_ = std::move(other.gamepad_);
      state_ = other.state_;
      support_ = other.support_;
    }
    return *this;
  }

  GamepadStateAdapter::~GamepadStateAdapter() {
    if (gamepad_) {
      gamepad_->close();
    }
  }

  GamepadAdapterCreationResult GamepadStateAdapter::create(Runtime &runtime, const DeviceProfile &profile) {
    auto created = runtime.create_gamepad(profile);
    if (!created) {
      return {created.status, nullptr};
    }

    GamepadAdapterCreationResult result;
    result.status = created.status;
    result.adapter = std::make_unique<GamepadStateAdapter>(std::move(created.gamepad));
    return result;
  }

  Gamepad *GamepadStateAdapter::gamepad() {
    return gamepad_.get();
  }

  const Gamepad *GamepadStateAdapter::gamepad() const {
    return gamepad_.get();
  }

  const GamepadProfileSupport &GamepadStateAdapter::support() const {
    return support_;
  }

  const GamepadState &GamepadStateAdapter::state() const {
    return state_;
  }

  bool GamepadStateAdapter::is_open() const {
    return gamepad_ && gamepad_->is_open();
  }

  OperationStatus GamepadStateAdapter::submit() {
    if (!gamepad_) {
      return OperationStatus::failure(ErrorCode::device_closed, "adapter owns no gamepad");
    }
    return gamepad_->submit(state_);
  }

  OperationStatus GamepadStateAdapter::set_state(const GamepadState &state) {
    state_ = state;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_button(GamepadButton button, bool pressed) {
    state_.buttons.set(button, pressed);
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_left_stick(Stick stick) {
    state_.left_stick = stick;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_right_stick(Stick stick) {
    state_.right_stick = stick;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_left_trigger(float value) {
    state_.left_trigger = value;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_right_trigger(float value) {
    state_.right_trigger = value;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_acceleration(std::optional<Vector3> acceleration) {
    state_.acceleration = acceleration;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_gyroscope(std::optional<Vector3> gyroscope) {
    state_.gyroscope = gyroscope;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_motion(Vector3 acceleration, Vector3 gyroscope) {
    state_.acceleration = acceleration;
    state_.gyroscope = gyroscope;
    return submit();
  }

  OperationStatus GamepadStateAdapter::clear_motion() {
    state_.acceleration.reset();
    state_.gyroscope.reset();
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_touchpad_contact(std::size_t index, GamepadTouchContact contact) {
    if (index >= state_.touchpad_contacts.size()) {
      return OperationStatus::failure(ErrorCode::invalid_argument, "touchpad contact index is out of range");
    }
    state_.touchpad_contacts[index] = contact;
    return submit();
  }

  OperationStatus GamepadStateAdapter::clear_touchpad_contact(std::size_t index) {
    if (index >= state_.touchpad_contacts.size()) {
      return OperationStatus::failure(ErrorCode::invalid_argument, "touchpad contact index is out of range");
    }
    state_.touchpad_contacts[index] = GamepadTouchContact {};
    return submit();
  }

  void GamepadStateAdapter::set_output_callback(const OutputCallback &callback) {
    if (gamepad_) {
      gamepad_->set_output_callback(callback);
    }
  }

  OperationStatus GamepadStateAdapter::dispatch_output(const GamepadOutput &output) {
    if (!gamepad_) {
      return OperationStatus::failure(ErrorCode::device_closed, "adapter owns no gamepad");
    }
    return gamepad_->dispatch_output(output);
  }

  OperationStatus GamepadStateAdapter::close() {
    if (!gamepad_) {
      return OperationStatus::success();
    }
    return gamepad_->close();
  }

}  // namespace vigem
