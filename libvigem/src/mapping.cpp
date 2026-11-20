// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libvigem/src/mapping.cpp
 * @brief Input shaping and remapping between physical controllers and emulated pads.
 */
#include <vigem/mapping.hpp>

// standard includes
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace vigem {

  namespace {

    constexpr float kFullScale = 1.0F;

    // Smallest D-pad threshold that still distinguishes a direction from a centered stick.
    constexpr float kMinimumDpadThreshold = 0.01F;

    float clamp_unit(float value) {
      // Written so a NaN input resolves to zero instead of propagating into the report.
      if (!(value > 0.0F)) {
        return 0.0F;
      }

      return value > kFullScale ? kFullScale : value;
    }

    /**
     * @brief Shape one stick according to the mapping.
     *
     * @param stick Input stick state.
     * @param mapping Stick mapping.
     * @return Shaped stick state.
     */
    /**
     * @brief Map a deadzone-relative travel distance onto the output range.
     *
     * @param magnitude Distance travelled from the centre.
     * @param mapping Stick mapping.
     * @return Output magnitude in `[0.0, 1.0]`.
     */
    float shaped_magnitude(float magnitude, const StickMapping &mapping) {
      const auto deadzone = clamp_unit(mapping.deadzone);
      if (!(magnitude > deadzone)) {
        return 0.0F;
      }

      // Rescale the travel that remains after the deadzone, then lift the result onto the
      // anti-deadzone floor so the smallest movement already clears a game's deadzone.
      const auto saturation = std::max(clamp_unit(mapping.saturation), deadzone);
      const auto span = saturation - deadzone;
      auto travel = span > 0.0F ? clamp_unit((magnitude - deadzone) / span) : kFullScale;

      const auto exponent = mapping.exponent > 0.0F ? mapping.exponent : 1.0F;
      if (exponent != 1.0F) {
        travel = std::pow(travel, exponent);
      }

      const auto anti_deadzone = clamp_unit(mapping.anti_deadzone);
      return anti_deadzone + travel * (kFullScale - anti_deadzone);
    }

    Stick shape_stick(Stick stick, const StickMapping &mapping) {
      // Centre correction comes first so drift is measured from the resting position.
      stick.x -= mapping.center_x;
      stick.y -= mapping.center_y;

      if (mapping.invert_x) {
        stick.x = -stick.x;
      }
      if (mapping.invert_y) {
        stick.y = -stick.y;
      }

      if (mapping.per_axis_deadzone) {
        const auto sign_x = stick.x < 0.0F ? -1.0F : 1.0F;
        const auto sign_y = stick.y < 0.0F ? -1.0F : 1.0F;
        return Stick {sign_x * shaped_magnitude(std::fabs(stick.x), mapping),
                      sign_y * shaped_magnitude(std::fabs(stick.y), mapping)};
      }

      const auto magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);
      const auto output = shaped_magnitude(magnitude, mapping);
      if (output <= 0.0F) {
        return {};
      }

      const auto scale = output / magnitude;
      return Stick {stick.x * scale, stick.y * scale};
    }

    /**
     * @brief Shape one trigger according to the mapping.
     *
     * @param value Input trigger value.
     * @param mapping Trigger mapping.
     * @return Shaped trigger value in `[0.0, 1.0]`.
     */
    float shape_trigger(float value, const TriggerMapping &mapping) {
      const auto input = clamp_unit(value);
      const auto deadzone = clamp_unit(mapping.deadzone);
      if (input <= deadzone) {
        return 0.0F;
      }

      const auto saturation = std::max(clamp_unit(mapping.saturation), deadzone);
      const auto span = saturation - deadzone;
      return span > 0.0F ? clamp_unit((input - deadzone) / span) : kFullScale;
    }

    /**
     * @brief Apply button redirections and suppressions.
     *
     * @param source Buttons reported by the physical controller.
     * @param mappings Button mappings.
     * @return Remapped button set.
     */
    ButtonSet remap_buttons(const ButtonSet &source, const std::vector<ButtonMapping> &mappings) {
      auto result = source;
      for (const auto &mapping : mappings) {
        if (!mapping.enabled) {
          result.set(mapping.source, false);
          continue;
        }

        if (source.test(mapping.source)) {
          result.set(mapping.source, false);
          result.set(mapping.target, true);
        }
      }

      return result;
    }

    /**
     * @brief Names used by the serialized form, kept stable across releases.
     */
    constexpr std::pair<GamepadButton, const char *> kButtonNames[] = {
        {GamepadButton::a, "a"},
        {GamepadButton::b, "b"},
        {GamepadButton::x, "x"},
        {GamepadButton::y, "y"},
        {GamepadButton::back, "back"},
        {GamepadButton::start, "start"},
        {GamepadButton::guide, "guide"},
        {GamepadButton::left_stick, "left_stick"},
        {GamepadButton::right_stick, "right_stick"},
        {GamepadButton::left_shoulder, "left_shoulder"},
        {GamepadButton::right_shoulder, "right_shoulder"},
        {GamepadButton::dpad_up, "dpad_up"},
        {GamepadButton::dpad_down, "dpad_down"},
        {GamepadButton::dpad_left, "dpad_left"},
        {GamepadButton::dpad_right, "dpad_right"},
        {GamepadButton::misc1, "misc1"},
        {GamepadButton::touchpad, "touchpad"},
        {GamepadButton::paddle1, "paddle1"},
        {GamepadButton::paddle2, "paddle2"},
        {GamepadButton::paddle3, "paddle3"},
        {GamepadButton::paddle4, "paddle4"},
    };

    const char *button_name(GamepadButton button) {
      for (const auto &entry : kButtonNames) {
        if (entry.first == button) {
          return entry.second;
        }
      }

      return "unknown";
    }

    std::optional<GamepadButton> button_from_name(std::string_view name) {
      for (const auto &entry : kButtonNames) {
        if (name == entry.second) {
          return entry.first;
        }
      }

      return std::nullopt;
    }

    bool parse_float(const std::string &text, float &value) {
      char *end = nullptr;
      const auto parsed = std::strtof(text.c_str(), &end);
      if (end == text.c_str() || *end != '\0') {
        return false;
      }

      value = parsed;
      return true;
    }

  }  // namespace

  GamepadState GamepadMapping::apply(const GamepadState &state) const {
    GamepadState mapped = state;

    mapped.buttons = remap_buttons(state.buttons, buttons);
    mapped.left_stick = shape_stick(state.left_stick, left_stick);
    mapped.right_stick = shape_stick(state.right_stick, right_stick);
    mapped.left_trigger = shape_trigger(state.left_trigger, left_trigger);
    mapped.right_trigger = shape_trigger(state.right_trigger, right_trigger);

    if (dpad_from_left_stick.enabled) {
      const auto threshold = std::max(clamp_unit(dpad_from_left_stick.threshold), kMinimumDpadThreshold);

      // Direction is taken from the physical stick so the D-pad threshold stays independent
      // of the stick response curve, matching how x360ce exposes "Axis To D-Pad".
      if (state.left_stick.x <= -threshold) {
        mapped.buttons.set(GamepadButton::dpad_left, true);
      }
      if (state.left_stick.x >= threshold) {
        mapped.buttons.set(GamepadButton::dpad_right, true);
      }
      if (state.left_stick.y >= threshold) {
        mapped.buttons.set(GamepadButton::dpad_up, true);
      }
      if (state.left_stick.y <= -threshold) {
        mapped.buttons.set(GamepadButton::dpad_down, true);
      }

      if (dpad_from_left_stick.suppress_stick) {
        mapped.left_stick = {};
      }
    }

    return mapped;
  }

  std::string serialize(const GamepadMapping &mapping) {
    std::ostringstream out;
    out << std::setprecision(9);
    out << "vigem.mapping=1\n";

    const auto write_stick = [&out](const char *prefix, const StickMapping &stick) {
      out << prefix << ".center_x=" << stick.center_x << '\n';
      out << prefix << ".center_y=" << stick.center_y << '\n';
      out << prefix << ".deadzone=" << stick.deadzone << '\n';
      out << prefix << ".anti_deadzone=" << stick.anti_deadzone << '\n';
      out << prefix << ".saturation=" << stick.saturation << '\n';
      out << prefix << ".exponent=" << stick.exponent << '\n';
      out << prefix << ".per_axis_deadzone=" << (stick.per_axis_deadzone ? 1 : 0) << '\n';
      out << prefix << ".invert_x=" << (stick.invert_x ? 1 : 0) << '\n';
      out << prefix << ".invert_y=" << (stick.invert_y ? 1 : 0) << '\n';
    };

    const auto write_trigger = [&out](const char *prefix, const TriggerMapping &trigger) {
      out << prefix << ".deadzone=" << trigger.deadzone << '\n';
      out << prefix << ".saturation=" << trigger.saturation << '\n';
    };

    write_stick("left_stick", mapping.left_stick);
    write_stick("right_stick", mapping.right_stick);
    write_trigger("left_trigger", mapping.left_trigger);
    write_trigger("right_trigger", mapping.right_trigger);

    out << "dpad.enabled=" << (mapping.dpad_from_left_stick.enabled ? 1 : 0) << '\n';
    out << "dpad.threshold=" << mapping.dpad_from_left_stick.threshold << '\n';
    out << "dpad.suppress_stick=" << (mapping.dpad_from_left_stick.suppress_stick ? 1 : 0) << '\n';

    out << "buttons=";
    for (std::size_t index = 0; index < mapping.buttons.size(); ++index) {
      if (index != 0) {
        out << ',';
      }

      const auto &entry = mapping.buttons[index];
      out << button_name(entry.source) << '>' << (entry.enabled ? button_name(entry.target) : "!");
    }
    out << '\n';

    return out.str();
  }

  std::optional<GamepadMapping> deserialize(std::string_view text, std::string *error) {
    GamepadMapping mapping;

    const auto fail = [error](std::string message) -> std::optional<GamepadMapping> {
      if (error != nullptr) {
        *error = std::move(message);
      }

      return std::nullopt;
    };

    std::istringstream input {std::string(text)};
    std::string line;

    while (std::getline(input, line)) {
      if (line.empty() || line.front() == '#') {
        continue;
      }

      const auto separator = line.find('=');
      if (separator == std::string::npos) {
        return fail("mapping line has no '=': " + line);
      }

      const auto key = line.substr(0, separator);
      const auto value = line.substr(separator + 1);
      if (key == "vigem.mapping") {
        continue;
      }

      if (key == "buttons") {
        mapping.buttons.clear();
        std::size_t start = 0;
        while (start <= value.size()) {
          const auto end = value.find(',', start);
          const auto entry = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
          if (!entry.empty()) {
            const auto arrow = entry.find('>');
            if (arrow == std::string::npos) {
              return fail("button entry has no '>': " + entry);
            }

            const auto source = button_from_name(entry.substr(0, arrow));
            if (!source) {
              return fail("unknown button name: " + entry.substr(0, arrow));
            }

            const auto target_text = entry.substr(arrow + 1);
            if (target_text == "!") {
              mapping.buttons.push_back({*source, *source, false});
            } else {
              const auto target = button_from_name(target_text);
              if (!target) {
                return fail("unknown button name: " + target_text);
              }

              mapping.buttons.push_back({*source, *target, true});
            }
          }

          if (end == std::string::npos) {
            break;
          }

          start = end + 1;
        }

        continue;
      }

      const auto dot = key.rfind('.');
      if (dot == std::string::npos) {
        return fail("unknown mapping key: " + key);
      }

      const auto prefix = key.substr(0, dot);
      const auto field = key.substr(dot + 1);

      StickMapping *stick = nullptr;
      TriggerMapping *trigger = nullptr;
      if (prefix == "left_stick") {
        stick = &mapping.left_stick;
      } else if (prefix == "right_stick") {
        stick = &mapping.right_stick;
      } else if (prefix == "left_trigger") {
        trigger = &mapping.left_trigger;
      } else if (prefix == "right_trigger") {
        trigger = &mapping.right_trigger;
      } else if (prefix == "dpad") {
        if (field == "enabled") {
          mapping.dpad_from_left_stick.enabled = value != "0";
        } else if (field == "suppress_stick") {
          mapping.dpad_from_left_stick.suppress_stick = value != "0";
        } else if (field == "threshold") {
          if (!parse_float(value, mapping.dpad_from_left_stick.threshold)) {
            return fail("invalid number for " + key + ": " + value);
          }
        } else {
          return fail("unknown mapping key: " + key);
        }

        continue;
      } else {
        return fail("unknown mapping key: " + key);
      }

      if (stick != nullptr) {
        if (field == "per_axis_deadzone") {
          stick->per_axis_deadzone = value != "0";
        } else if (field == "invert_x") {
          stick->invert_x = value != "0";
        } else if (field == "invert_y") {
          stick->invert_y = value != "0";
        } else {
          float *number = nullptr;
          if (field == "center_x") {
            number = &stick->center_x;
          } else if (field == "center_y") {
            number = &stick->center_y;
          } else if (field == "deadzone") {
            number = &stick->deadzone;
          } else if (field == "anti_deadzone") {
            number = &stick->anti_deadzone;
          } else if (field == "saturation") {
            number = &stick->saturation;
          } else if (field == "exponent") {
            number = &stick->exponent;
          }

          if (number == nullptr) {
            return fail("unknown mapping key: " + key);
          }

          if (!parse_float(value, *number)) {
            return fail("invalid number for " + key + ": " + value);
          }
        }

        continue;
      }

      if (field == "deadzone") {
        if (!parse_float(value, trigger->deadzone)) {
          return fail("invalid number for " + key + ": " + value);
        }
      } else if (field == "saturation") {
        if (!parse_float(value, trigger->saturation)) {
          return fail("invalid number for " + key + ": " + value);
        }
      } else {
        return fail("unknown mapping key: " + key);
      }
    }

    return mapping;
  }

}  // namespace vigem
