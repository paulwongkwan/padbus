// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file libvigem/include/vigem/mapping.hpp
 * @brief Input shaping and remapping between physical controllers and emulated pads.
 */
#pragma once

// standard includes
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include <vigem/types.hpp>

namespace vigem {

  /**
   * @brief Response shaping for an analog stick.
   *
   * The defaults pass the input through unchanged. The settings mirror what DS4Windows and
   * x360ce expose per axis: a centre offset for sticks that rest off-centre, a deadzone that
   * swallows drift, an anti-deadzone floor that steps over a game's own deadzone, a
   * saturation point, a response curve, and inversion.
   */
  struct StickMapping {
    /**
     * @brief Input value subtracted before shaping, in `[-1.0, 1.0]`.
     *
     * Compensates a stick that does not rest at the centre of its range.
     */
    float center_x = 0.0F;

    /**
     * @brief Input value subtracted before shaping, in `[-1.0, 1.0]`.
     */
    float center_y = 0.0F;

    /**
     * @brief Radial input deadzone in the inclusive range `[0.0, 1.0]`.
     *
     * Input whose magnitude stays inside the deadzone is reported as centered.
     */
    float deadzone = 0.0F;

    /**
     * @brief Output floor in the inclusive range `[0.0, 1.0]`.
     *
     * Reached as soon as the stick leaves the deadzone, so games that apply their own
     * deadzone still move on small inputs.
     */
    float anti_deadzone = 0.0F;

    /**
     * @brief Input magnitude in `(0.0, 1.0]` that produces full output.
     */
    float saturation = 1.0F;

    /**
     * @brief Response curve exponent, where `1.0` is linear.
     *
     * Values above one soften the center and keep full deflection at the edge.
     */
    float exponent = 1.0F;

    /**
     * @brief Apply the deadzone to each axis instead of as a radius.
     *
     * Per-axis shaping square off the travel and match how many games treat sticks.
     */
    bool per_axis_deadzone = false;

    /**
     * @brief Invert the horizontal axis.
     */
    bool invert_x = false;

    /**
     * @brief Invert the vertical axis.
     */
    bool invert_y = false;
  };

  /**
   * @brief Response shaping for an analog trigger.
   */
  struct TriggerMapping {
    /**
     * @brief Input deadzone in the inclusive range `[0.0, 1.0]`.
     */
    float deadzone = 0.0F;

    /**
     * @brief Input value in `(0.0, 1.0]` that produces full output.
     */
    float saturation = 1.0F;
  };

  /**
   * @brief Redirection of one physical button.
   */
  struct ButtonMapping {
    /**
     * @brief Button reported by the physical controller.
     */
    GamepadButton source = GamepadButton::a;

    /**
     * @brief Button pressed on the emulated device.
     */
    GamepadButton target = GamepadButton::a;

    /**
     * @brief Whether the source is redirected; `false` suppresses it instead.
     */
    bool enabled = true;
  };

  /**
   * @brief Directional pad synthesis from the left stick.
   */
  struct DpadFromStickMapping {
    /**
     * @brief Whether stick movement also presses the directional pad.
     */
    bool enabled = false;

    /**
     * @brief Axis magnitude in `[0.0, 1.0]` that presses a direction.
     */
    float threshold = 0.5F;

    /**
     * @brief Zero the left stick after it was interpreted as a directional pad.
     */
    bool suppress_stick = false;
  };

  /**
   * @brief Input mapping applied to a normalized state before it is submitted.
   *
   * Physical controllers differ from the emulated devices in drift, travel and button
   * layout. This layer converts a normalized state read from any controller into the
   * state a game should see, so callers do not reimplement deadzones and remaps per host.
   *
   * Motion, touch, and battery fields are passed through untouched.
   */
  class GamepadMapping {
  public:
    /**
     * @brief Shaping for the left stick.
     */
    StickMapping left_stick;

    /**
     * @brief Shaping for the right stick.
     */
    StickMapping right_stick;

    /**
     * @brief Shaping for the left trigger.
     */
    TriggerMapping left_trigger;

    /**
     * @brief Shaping for the right trigger.
     */
    TriggerMapping right_trigger;

    /**
     * @brief Directional pad synthesis from the left stick.
     */
    DpadFromStickMapping dpad_from_left_stick;

    /**
     * @brief Button redirections, applied in order.
     */
    std::vector<ButtonMapping> buttons;

    /**
     * @brief Apply the mapping to a normalized state.
     *
     * @param state State read from the physical controller.
     * @return Mapped state suitable for submission.
     */
    GamepadState apply(const GamepadState &state) const;
  };

  /**
   * @brief Serialize a mapping into a stable `key=value` text form.
   *
   * The output is line based and intended for per-controller profile files, so a mapping
   * configured by one host can be reloaded by another.
   *
   * @param mapping Mapping to serialize.
   * @return Serialized mapping.
   */
  std::string serialize(const GamepadMapping &mapping);

  /**
   * @brief Parse a mapping produced by serialize().
   *
   * Missing keys keep their default values, so partial files are accepted.
   *
   * @param text Serialized mapping.
   * @param error Optional failure description when parsing fails.
   * @return Parsed mapping, or `std::nullopt` when the text is invalid.
   */
  std::optional<GamepadMapping> deserialize(std::string_view text, std::string *error = nullptr);

}  // namespace vigem
