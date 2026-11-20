/**
 * @file libvigem/src/types.cpp
 * @brief Core public type implementations for the modern ViGEm client API.
 */
#include <vigem/types.hpp>

// standard includes
#include <utility>

namespace vigem {

  namespace {

    std::uint32_t button_mask(GamepadButton button) {
      return 1u << static_cast<std::uint32_t>(button);
    }

  }  // namespace

  const char *to_string(ErrorCode code) {
    switch (code) {
      case ErrorCode::ok:
        return "ok";
      case ErrorCode::invalid_argument:
        return "invalid_argument";
      case ErrorCode::backend_unavailable:
        return "backend_unavailable";
      case ErrorCode::unsupported_profile:
        return "unsupported_profile";
      case ErrorCode::device_closed:
        return "device_closed";
      case ErrorCode::no_free_slot:
        return "no_free_slot";
      case ErrorCode::not_supported:
        return "not_supported";
      case ErrorCode::backend_failure:
        return "backend_failure";
    }

    return "unknown";
  }

  const char *to_string(LogLevel level) {
    switch (level) {
      case LogLevel::debug:
        return "debug";
      case LogLevel::info:
        return "info";
      case LogLevel::warning:
        return "warning";
      case LogLevel::error:
        return "error";
    }

    return "unknown";
  }

  OperationStatus::OperationStatus(): code_(ErrorCode::ok) {}

  OperationStatus::OperationStatus(ErrorCode code, std::string message):
      code_(code), message_(std::move(message)) {}

  OperationStatus OperationStatus::success() {
    return OperationStatus();
  }

  OperationStatus OperationStatus::failure(ErrorCode code, std::string message) {
    return OperationStatus(code, std::move(message));
  }

  bool OperationStatus::ok() const {
    return code_ == ErrorCode::ok;
  }

  ErrorCode OperationStatus::code() const {
    return code_;
  }

  const std::string &OperationStatus::message() const {
    return message_;
  }

  void ButtonSet::set(GamepadButton button, bool pressed) {
    if (pressed) {
      bits_ |= button_mask(button);
    } else {
      bits_ &= ~button_mask(button);
    }
  }

  void ButtonSet::reset(GamepadButton button) {
    bits_ &= ~button_mask(button);
  }

  void ButtonSet::clear() {
    bits_ = 0;
  }

  bool ButtonSet::test(GamepadButton button) const {
    return (bits_ & button_mask(button)) != 0;
  }

  std::uint32_t ButtonSet::raw_bits() const {
    return bits_;
  }

}  // namespace vigem
