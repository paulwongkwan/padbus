// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2020, Nefarius Software Solutions e.U.
/**
 * @file tests/stress.cpp
 * @brief Concurrency check: submissions racing a close and feedback dispatch.
 *
 * A host submits from its input thread while the driver delivers feedback on its own
 * thread and shutdown closes the device from another. The API must stay consistent under
 * that overlap instead of tearing the device down underneath an in-flight report.
 */
#include <vigem/vigem.hpp>

// standard includes
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

  std::atomic<int> failures {0};

  void check(bool condition, const char *what) {
    if (!condition) {
      std::printf("FAIL: %s\n", what);
      failures.fetch_add(1);
    }
  }

  constexpr int kSubmitThreads = 4;
  constexpr int kFramesPerThread = 500;

}  // namespace

int main() {
  for (int round = 0; round < 5; ++round) {
    vigem::RuntimeOptions options;
    options.backend = vigem::BackendKind::fake;

    auto runtime = vigem::Runtime::create(options);
    check(runtime != nullptr, "runtime is created");
    if (!runtime) {
      break;
    }

    auto created = runtime->create_gamepad(vigem::profiles::dualshock4());
    if (!created) {
      std::printf("FAIL: gamepad is created\n");
      break;
    }

    auto &gamepad = *created.gamepad;
    std::atomic<int> feedback {0};
    gamepad.set_output_callback([&feedback](const vigem::GamepadOutput &) { feedback.fetch_add(1); });

    std::atomic<bool> stop {false};
    std::vector<std::thread> workers;
    workers.reserve(kSubmitThreads);

    for (int index = 0; index < kSubmitThreads; ++index) {
      workers.emplace_back([&gamepad, &stop, index] {
        for (int frame = 0; frame < kFramesPerThread && !stop.load(); ++frame) {
          vigem::GamepadState state;
          state.left_stick = {static_cast<float>(frame % 10) / 10.0F, 0.0F};
          state.buttons.set(vigem::GamepadButton::a, frame % 2 == 0);
          const auto status = gamepad.submit(state);
          // Either the frame landed or the device was already closed underneath it.
          if (!status.ok() && status.code() != vigem::ErrorCode::device_closed &&
              status.code() != vigem::ErrorCode::device_detached) {
            std::printf("FAIL: unexpected submit status in worker %d\n", index);
            ++failures;
          }
        }
      });
    }

    // A driver notification thread delivering feedback while the workers submit.
    std::thread notifier([&gamepad, &feedback, &stop] {
      vigem::GamepadOutput rumble;
      rumble.kind = vigem::GamepadOutputKind::rumble;
      rumble.low_frequency_rumble = 0x4000;
      while (!stop.load()) {
        (void)gamepad.dispatch_output(rumble);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
      (void)feedback.load();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(gamepad.close().ok(), "the device closes while submissions are in flight");

    stop.store(true);
    for (auto &worker : workers) {
      worker.join();
    }
    notifier.join();

    check(!gamepad.is_attached(), "the closed device reports itself detached");
    check(gamepad.submit(vigem::GamepadState {}).code() == vigem::ErrorCode::device_closed,
          "submissions after the close report a closed device");
    check(runtime->active_device_count() == 0, "no device is left open");
  }

  if (failures.load() != 0) {
    std::printf("%d check(s) failed\n", failures.load());
    return 1;
  }

  std::printf("all checks passed\n");
  return 0;
}
