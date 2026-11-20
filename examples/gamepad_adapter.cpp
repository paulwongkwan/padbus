// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file examples/gamepad_adapter.cpp
 * @brief Create a virtual gamepad, forward client input, and print feedback.
 *
 * The example drives a DualShock 4 profile for a few frames so the touch and
 * motion fields are exercised, then closes the device. Replace the synthetic
 * frames with the input a streaming host receives from its client.
 */
#include <vigem/vigem.hpp>

// standard includes
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
  auto runtime = vigem::Runtime::create();

  if (!runtime->capabilities().available) {
    std::printf("ViGEm backend unavailable (%s)\n", runtime->capabilities().backend_name.c_str());
    std::printf("Install the ViGEmBus driver and make ViGEmClient.dll available to run this example.\n");
    return 1;
  }

  auto created = vigem::GamepadStateAdapter::create(*runtime, vigem::profiles::dualshock4());
  if (!created) {
    std::printf("Gamepad creation failed: %s\n", created.status.message().c_str());
    return 1;
  }

  auto &adapter = *created.adapter;
  adapter.set_output_callback([](const vigem::GamepadOutput &output) {
    switch (output.kind) {
      case vigem::GamepadOutputKind::rumble:
        std::printf("rumble: low=%u high=%u\n", output.low_frequency_rumble, output.high_frequency_rumble);
        break;
      case vigem::GamepadOutputKind::rgb_led:
        std::printf("lightbar: (%u,%u,%u)\n", output.red, output.green, output.blue);
        break;
      case vigem::GamepadOutputKind::player_leds:
        std::printf("player leds: %d%d%d%d\n", output.player_leds[0] ? 1 : 0, output.player_leds[1] ? 1 : 0,
                    output.player_leds[2] ? 1 : 0, output.player_leds[3] ? 1 : 0);
        break;
      case vigem::GamepadOutputKind::raw_report:
        std::printf("raw output report: %zu bytes\n", output.raw_report.size());
        break;
    }
  });

  std::printf("touchpad and motion support: %d/%d\n", adapter.support().supports_touchpad ? 1 : 0,
              adapter.support().supports_motion ? 1 : 0);

  for (int frame = 0; frame < 60; ++frame) {
    const float progress = static_cast<float>(frame) / 60.0F;

    vigem::GamepadTouchContact contact;
    contact.id = 1;
    contact.active = true;
    contact.x = progress;
    contact.y = 0.5F;

    adapter.set_left_stick({progress, 0.0F});
    adapter.set_button(vigem::GamepadButton::a, (frame / 10) % 2 == 0);
    adapter.set_touchpad_contact(0, contact);
    adapter.set_motion(vigem::Vector3 {0.0F, 0.0F, 981.0F}, vigem::Vector3 {progress, 0.0F, 0.0F});
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  std::printf("submitted %zu reports, last report %zu bytes\n", adapter.gamepad()->submit_count(),
              adapter.gamepad()->last_input_report().size());

  adapter.close();
  return 0;
}
