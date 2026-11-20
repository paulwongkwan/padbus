# ViGEm Bus Driver

Windows kernel-mode driver emulating well-known USB game controllers.

[![Build status](https://ci.appveyor.com/api/projects/status/rv74ufluwib52dq2?svg=true)](https://ci.appveyor.com/project/nefarius/vigembus) [![GitHub All Releases](https://img.shields.io/github/downloads/ViGEm/ViGEmBus/total)](https://somsubhra.github.io/github-release-stats/?username=nefarius&repository=ViGEmBus) [![Discord](https://img.shields.io/discord/346756263763378176.svg)](https://discord.nefarius.at) [![Website](https://img.shields.io/website-up-down-green-red/https/vigem.org.svg?label=docs.nefarius.at)](https://docs.nefarius.at/) [![GitHub followers](https://img.shields.io/github/followers/nefarius.svg?style=social&label=Follow)](https://github.com/nefarius) [![Mastodon Follow](https://img.shields.io/mastodon/follow/109321120351128938?domain=https%3A%2F%2Ffosstodon.org%2F&style=social)](https://fosstodon.org/@Nefarius)

---

## 🧟 THIS PROJECT HAS BEEN RETIRED 🧟

Users of this software are encouraged to [read the end-of-life statement](https://docs.nefarius.at/projects/ViGEm/End-of-Life/). So long, cheers 🖖

---

## About

The `ViGEmBus` driver and `ViGEmClient` libraries represent the core of the Virtual Gamepad Emulation Framework (or `ViGEm` , for short). `ViGEm` aims for a 100% accurate [emulation](<https://en.wikipedia.org/wiki/Emulator>) of well-known gaming peripherals as pure software-based devices at kernel level. As it mimics "the real thing" games and other processes require no additional modification whatsoever to detect `ViGEm`-based devices (no Proxy-DLLs or API-Hooking) and simply work out of the box. While the (now obsolete) [Scarlett.Crush Productions Virtual Bus Driver](<https://github.com/nefarius/ScpVBus>) is the spiritual father of this project, `ViGEm` has been designed and written from the ground up utilizing Microsoft's [Kernel-Mode Driver Framework](https://en.wikipedia.org/wiki/Kernel-Mode_Driver_Framework).

### Emulated devices

Emulation of the following USB Gamepads is supported:

- [Microsoft Xbox 360 Controller](https://en.wikipedia.org/wiki/Xbox_360_controller)
- [Sony DualShock 4 Controller](https://en.wikipedia.org/wiki/DualShock#DualShock_4)

## Use cases

A few examples of the most common use cases for `ViGEm` are:

- You have an unsupported input device you'd like to use within games without modifying said game.
- You want the freedom to use a different controller of your choice in [PS4 Remote Play](<https://remoteplay.dl.playstation.net/remoteplay/>).
- You encountered a game not compatible with [x360ce](<https://www.x360ce.com/>) (prior to version 4.x).
- You want to extend the reach of your input device (like send traffic to a different machine over a network).
- You want to test/benchmark your game and need a replay mechanism for your user inputs.
- You want to work around player slot assignment order issues in `XInput`.

## Modern C++ client API

The `libvigem/` library adds a modern C++17 client API to the bus driver. It is modelled on
[`libvirtualhid`](https://github.com/LizardByte/libvirtualhid): consumers describe what they want in
portable terms — a runtime, a device profile, a normalized gamepad state, output callbacks — and the
library translates that into the personalities the driver actually publishes.

```cpp
#include <vigem/vigem.hpp>

auto runtime = vigem::Runtime::create();
auto created = runtime->create_gamepad(vigem::profiles::xbox_360());
if (!created) {
  return;
}

auto &gamepad = *created.gamepad;
gamepad.set_output_callback([](const vigem::GamepadOutput &output) {
  if (output.kind == vigem::GamepadOutputKind::rumble) {
    // Forward feedback to the client controller.
  }
});

vigem::GamepadState state;
state.buttons.set(vigem::GamepadButton::a, true);
state.left_stick = {0.25F, -0.5F};
state.right_trigger = 1.0F;
gamepad.submit(state);
```

Features adopted from `libvirtualhid`:

- **Runtime and profile model** — `vigem::Runtime::create()` plus `vigem::profiles::xbox_360()` and
  `vigem::profiles::dualshock4()` replace the manual alloc/connect/add sequence, and profiles carry
  their own vendor/product identity and capability flags.
- **Normalized gamepad state** — sticks use `[-1.0, 1.0]`, triggers use `[0.0, 1.0]`, buttons use the
  logical `vigem::GamepadButton` set. The library clamps out-of-range input and packs the profile
  specific report, so callers never build `XUSB_REPORT` or `DS4_REPORT_EX` by hand.
- **Unified output callbacks** — rumble, lightbar color, and player LED feedback from both profiles
  arrive through a single `vigem::GamepadOutput` callback instead of two driver-specific ones.
- **Capability queries** — `gamepad_profile_support()`, `supports_gamepad_button()`,
  `supports_gamepad_output()`, and `Runtime::capabilities()` let hosts hide inputs a profile cannot carry.
- **Status model** — `vigem::OperationStatus` and `vigem::ErrorCode` replace raw `VIGEM_ERROR` values and
  carry a human-readable reason.
- **Incremental adapter** — `vigem::GamepadStateAdapter` caches the full state and resubmits it after
  partial updates such as `set_button()` or `set_touchpad_contact()`.
- **Diagnostics hook** — `RuntimeOptions::log_callback` routes runtime and device lifecycle messages into
  the host's logging system.
- **In-memory backend** — `vigem::BackendKind::fake` creates devices without the driver, for tests and
  API validation.
- **Full DualShock 4 reports** — touch contacts, gyroscope, and accelerometer samples are packed into the
  full `DS4_REPORT_EX` report, and raw output reports are available through `Gamepad::poll_raw_output()`.
- **Driver-accurate DualShock 4 details** — the report timestamp advances in the driver's 5.333 µs units
  (hosts such as Sunshine repeat reports specifically to keep it moving), and `GamepadState::battery`
  publishes charge and cable state in both battery bytes ViGEm exposes.
- **Attachment queries** — `Gamepad::is_attached()` reports whether the driver still holds the target, so a
  bus restart is detectable instead of surfacing as an endless run of failed submits.
- **Input mapping** — `vigem::GamepadMapping` shapes a physical controller's state before submission: radial
  or per-axis deadzone, centre offset for sticks that rest off-centre, anti-deadzone floor, saturation,
  response curve, axis inversion, trigger zones, stick-to-D-pad synthesis, and button redirection or
  suppression, matching what DS4Windows and x360ce expose per axis. `vigem::serialize()` and
  `vigem::deserialize()` store a mapping as stable `key=value` text for per-controller profile files.
- **Recovery and duplicate suppression** — `Gamepad::reattach()` revives a device the driver dropped (a bus
  restart invalidates both the target and the client connection), and `set_submit_on_change()` drops frames
  that repeat the previous report while a keepalive flush keeps the device from going stale. Failures carry
  the raw `VIGEM_ERROR` value through `OperationStatus::native_code()`.

### Migrating from the ViGEmClient C API

| ViGEmClient | libvigem |
| --- | --- |
| `vigem_alloc` + `vigem_connect` | `vigem::Runtime::create()` |
| `vigem_target_x360_alloc` / `_ds4_alloc` + `vigem_target_add` | `Runtime::create_gamepad(vigem::profiles::xbox_360())` / `dualshock4()` |
| `vigem_target_x360_update` / `vigem_target_ds4_update_ex` | `Gamepad::submit(vigem::GamepadState{…})` |
| `vigem_target_is_attached` | `Gamepad::is_attached()`, then `Gamepad::reattach()` |
| `vigem_target_remove` + `vigem_target_free` | `Gamepad::close()`, or handle destruction |
| `vigem_target_x360_register_notification` / `_ds4_` | `Gamepad::set_output_callback(…)` |
| `vigem_target_ds4_await_output_report_timeout` | `Gamepad::poll_raw_output(timeout_ms)` |
| `vigem_target_x360_get_user_index` | `Gamepad::xinput_user_index()` |
| `vigem_disconnect` + `vigem_free` | runtime destruction |

Consume the library with `add_subdirectory(ViGEmBus)` and link `vigem::vigem`, or install it and use
`find_package(vigem REQUIRED)`. The headers are C++17 and compile unchanged as C++23.

### DualShock 4 report layout

`Gamepad::last_input_report()` returns the 63-byte report body, so struct offset *N* is hardware byte *N*+1:

| Offset | Content |
| --- | --- |
| 0-3 | Sticks: 0 is left/top, 255 is right/bottom, so vertical axes are inverted against the normalized positive-is-up model |
| 4-5 | Button word, little endian; low nibble carries the direction pad |
| 6 | Special buttons (PS, touchpad click) |
| 7-8 | Trigger values in `0-255`; the digital trigger bits live in the button word |
| 9-10 | Timestamp in 5.333 µs units, advanced on every submission |
| 11 | Battery level byte (`percent * 255 / 100`) |
| 12-17 | Gyroscope, 1024 units per 64 °/s |
| 18-23 | Accelerometer, 8192 units per g |
| 29 | Battery status: low nibble in 10 % steps, bit 4 set when a cable is connected |
| 32 | Touch packet count, always 1 |
| 33-41 | Touch packet; the panel is 1920 × 943 with addressable maxima 1919 / 942 |

Scope: the driver still publishes exactly two personalities. Profiles for DualSense, Switch Pro,
Xbox One/Series, generic HID, keyboard, mouse, or pen devices are not offered because the driver cannot
create them; adding those requires driver work rather than client code.

## Supported Systems

🛑 **Windows Server** might work but is **not supported** 🛑

Bug reports/support requests regarding running on a Server OS will be discarded.

### Version 1.16 and below

The driver is built for Windows 7/8.1/10 (x86 and amd64).

### Version 1.17 and above

The driver is built for Windows 10/11 only (x86, amd64 and ARM64).

## License

The ViGEm Bus Driver is licensed under the **BSD-3-Clause**, see [LICENSE](./LICENSE.md) for more information.

## How to build

### Prerequisites

- [Step 1: Install Visual Studio 2019](https://docs.microsoft.com/en-us/windows-hardware/drivers/other-wdk-downloads#step-1-install-visual-studio)
- [Step 2: Install WDK for Windows 10, version 2004](https://docs.microsoft.com/en-us/windows-hardware/drivers/other-wdk-downloads#step-2-install-the-wdk)
- [Step 3: Clone the Driver Module Framework (DMF)](https://github.com/microsoft/DMF) into the same parent directory.
  - Build the `DmfK` project with Release and Debug configurations for all architectures (x64 and Win32).

You can build directly within Visual Studio.

The modern C++ client API in `libvigem/` targets **Windows 11** and needs a C++17 compiler:

```sh
cmake -S . -B out -DVIGEM_BUILD_TESTS=ON
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

The Windows backend loads `ViGEmClient.dll` at runtime, so `libvigem` has no link-time dependency on the
client SDK and reports `backend_unavailable` when the driver or library is missing.

Do bear in mind that you'll need to **sign** the driver to use it without [test mode](https://docs.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option#enable-or-disable-use-of-test-signed-code).

## Contribute

### Bugs & Features

Found a bug and want it fixed? Open a detailed issue on the [GitHub issue tracker](../../issues)!

Have an idea for a new feature? Let's have a chat about your request on [Discord](https://discord.nefarius.at).

### Questions & Support

Please respect that the GitHub issue tracker isn't a helpdesk. We offer a [range of support resources](https://docs.nefarius.at/Community-Support/) you're welcome to check out!

## Installation

Pre-built production-signed binaries **for Windows 10/11** are provided by `Nefarius Software Solutions e.U.` [as an all-in-one setup](../../releases/latest).

## Sponsors

Sponsors listed here have helped the project flourish by either financial support or by gifting licenses:

- [3dRudder](https://www.3drudder.com/)
- [Parsec](https://parsec.app/)
- [JetBrains](https://www.jetbrains.com/resharper/)
- [Advanced Installer](https://www.advancedinstaller.com/)
- [ICAROS](https://www.icaros.com/)

## Known users of ViGEm

A brief listing of projects/companies/vendors known to build upon the powers of ViGEm.

This list is non-exhaustive, if you'd like to see your project included, contact us!

- [3dRudder](https://www.3drudder.com/)
- [Parsec](https://parsec.app/)
- [GloSC](https://github.com/Alia5/GloSC)
- [UCR](https://github.com/Snoothy/UCR)
- [InputMapper](https://inputmapper.com/)
- [Oculus VR, LLC.](https://www.oculus.com/)
- [WiimoteHook](https://forum.cemu.info/showthread.php/140-WiimoteHook-Nintendo-Wii-Remote-with-Motion-Rumble-and-Nunchuk-support)
- [XJoy](https://github.com/sam0x17/XJoy)
- [HP](https://www8.hp.com/us/en/gaming/omen.html)
- [DS4Windows](https://ryochan7.github.io/ds4windows-site/)
- [XOutput](https://github.com/csutorasa/XOutput)
- [RdpGamepad](https://github.com/microsoft/RdpGamepad)
- [Touchmote](https://github.com/Ryochan7/Touchmote/tree/ryochan7)
- [Mi-ViGEm](https://github.com/grayver/mi-vigem)
- [BetterJoy](https://github.com/Davidobot/BetterJoy)
- [Regame](https://github.com/ksyun-kenc/liuguang)
- [NetInput](https://github.com/usertoroot/NetInput)
- [NetJoy](https://github.com/Qcent/NetJoy/)
