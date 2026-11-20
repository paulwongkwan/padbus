# PadBus

Windows 11 kernel-mode bus driver publishing virtual Xbox 360 and DualShock 4 controllers.

---

## Lineage

`PadBus` continues the retired [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver. Upstream
ended in November 2023 after a trademark conflict, not for a technical reason, so this fork keeps
the code, the BSD-3-Clause licence and the driver identity that existing hosts depend on, and drops
the name that caused the conflict. It targets Windows 11 only.

## Name and compatibility

Everything a user sees is PadBus: the repository, the product, the installer, the C++ library
(`libpadbus`, namespace `padbus`) and the Device Manager description. These keep their ViGEm
identities on purpose, because Sunshine, DS4Windows and every other ViGEmClient host match on them:

- the `Nefarius\ViGEmBus\Gen1` hardware ID and the device interface GUID the bus publishes,
- the `ViGEmBus` service name, which the driver package no longer shares: it ships as
  `PadBus.sys`, `PadBus.inf` and `PadBus.cat`,
- the `ViGEmClient.dll` C API the driver is driven through.

An existing ViGEmBus installation therefore keeps working, and `libpadbus` drives it unchanged.

---

## About

`PadBus` publishes virtual Xbox 360 and DualShock 4 controllers as real devices on Windows 11. Games
and other processes need no modification to see them — no proxy DLLs, no API hooking — because the
driver presents the same kernel-level interfaces the hardware does: XInput for the Xbox 360 pad and
HID for the DualShock 4. Like its predecessor it is built on Microsoft's
[Kernel-Mode Driver Framework](<https://en.wikipedia.org/wiki/Kernel-Mode_Driver_Framework>), and it
traces its lineage to the (now obsolete)
[Scarlett.Crush Productions Virtual Bus Driver](<https://github.com/nefarius/ScpVBus>).

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

The `libpadbus/` library adds a modern C++17 client API to the bus driver. It is modelled on
[`libvirtualhid`](https://github.com/LizardByte/libvirtualhid): consumers describe what they want in
portable terms — a runtime, a device profile, a normalized gamepad state, output callbacks — and the
library translates that into the personalities the driver actually publishes.

```cpp
#include <padbus/padbus.hpp>

auto runtime = padbus::Runtime::create();
auto created = runtime->create_gamepad(padbus::profiles::xbox_360());
if (!created) {
  return;
}

auto &gamepad = *created.gamepad;
gamepad.set_output_callback([](const padbus::GamepadOutput &output) {
  if (output.kind == padbus::GamepadOutputKind::rumble) {
    // Forward feedback to the client controller.
  }
});

padbus::GamepadState state;
state.buttons.set(padbus::GamepadButton::a, true);
state.left_stick = {0.25F, -0.5F};
state.right_trigger = 1.0F;
gamepad.submit(state);
```

Features adopted from `libvirtualhid`:

- **Runtime and profile model** — `padbus::Runtime::create()` plus `padbus::profiles::xbox_360()` and
  `padbus::profiles::dualshock4()` replace the manual alloc/connect/add sequence, and profiles carry
  their own vendor/product identity and capability flags.
- **Normalized gamepad state** — sticks use `[-1.0, 1.0]`, triggers use `[0.0, 1.0]`, buttons use the
  logical `padbus::GamepadButton` set. The library clamps out-of-range input and packs the profile
  specific report, so callers never build `XUSB_REPORT` or `DS4_REPORT_EX` by hand.
- **Unified output callbacks** — rumble, lightbar color, and player LED feedback from both profiles
  arrive through a single `padbus::GamepadOutput` callback instead of two driver-specific ones.
- **Capability queries** — `gamepad_profile_support()`, `supports_gamepad_button()`,
  `supports_gamepad_output()`, and `Runtime::capabilities()` let hosts hide inputs a profile cannot carry.
- **Status model** — `padbus::OperationStatus` and `padbus::ErrorCode` replace raw `VIGEM_ERROR` values and
  carry a human-readable reason.
- **Incremental adapter** — `padbus::GamepadStateAdapter` caches the full state and resubmits it after
  partial updates such as `set_button()` or `set_touchpad_contact()`.
- **Diagnostics hook** — `RuntimeOptions::log_callback` routes runtime and device lifecycle messages into
  the host's logging system.
- **In-memory backend** — `padbus::BackendKind::fake` creates devices without the driver, for tests and
  API validation.
- **Full DualShock 4 reports** — touch contacts, gyroscope, and accelerometer samples are packed into the
  full `DS4_REPORT_EX` report, and raw output reports are available through `Gamepad::poll_raw_output()`.
- **Driver-accurate DualShock 4 details** — the report timestamp advances in the driver's 5.333 µs units
  (hosts such as Sunshine repeat reports specifically to keep it moving), and `GamepadState::battery`
  publishes charge and cable state in both battery bytes ViGEm exposes.
- **Attachment queries** — `Gamepad::is_attached()` reports whether the driver still holds the target, so a
  bus restart is detectable instead of surfacing as an endless run of failed submits.
- **Input mapping** — `padbus::GamepadMapping` shapes a physical controller's state before submission: radial
  or per-axis deadzone, centre offset for sticks that rest off-centre, anti-deadzone floor, saturation,
  response curve, axis inversion, trigger zones, stick-to-D-pad synthesis, and button redirection or
  suppression, matching what DS4Windows and x360ce expose per axis. `padbus::serialize()` and
  `padbus::deserialize()` store a mapping as stable `key=value` text for per-controller profile files.
- **Recovery and duplicate suppression** — `Gamepad::reattach()` revives a device the driver dropped (a bus
  restart invalidates both the target and the client connection), and `set_submit_on_change()` drops frames
  that repeat the previous report while a keepalive flush keeps the device from going stale. Failures carry
  the raw `VIGEM_ERROR` value through `OperationStatus::native_code()`.

### Migrating from the ViGEmClient C API

| ViGEmClient C API | PadBus C++ API |
| --- | --- |
| `vigem_alloc` + `vigem_connect` | `padbus::Runtime::create()` |
| `vigem_target_x360_alloc` / `_ds4_alloc` + `vigem_target_add` | `Runtime::create_gamepad(padbus::profiles::xbox_360())` / `dualshock4()` |
| `vigem_target_x360_update` / `vigem_target_ds4_update_ex` | `Gamepad::submit(padbus::GamepadState{…})` |
| `vigem_target_is_attached` | `Gamepad::is_attached()`, then `Gamepad::reattach()` |
| `vigem_target_remove` + `vigem_target_free` | `Gamepad::close()`, or handle destruction |
| `vigem_target_x360_register_notification` / `_ds4_` | `Gamepad::set_output_callback(…)` |
| `vigem_target_ds4_await_output_report_timeout` | `Gamepad::poll_raw_output(timeout_ms)` |
| `vigem_target_x360_get_user_index` | `Gamepad::xinput_user_index()` |
| `vigem_disconnect` + `vigem_free` | runtime destruction |

Consume the library with `add_subdirectory(padbus)` and link `padbus::padbus`, or install it and use
`find_package(padbus REQUIRED)`. The headers are C++17 and compile unchanged as C++23.

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

### Building, testing and packaging

```sh
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out
ctest --test-dir out --output-on-failure
cmake --install out --prefix dist/libpadbus-2.0.0
cmake --build out --target package      # portable archive via CPack
```

Three checks run under CTest:

| Target | What it covers |
| --- | --- |
| `padbus_api_smoke` | report packing for both profiles, error paths, logging, mapping and the adapter |
| `padbus_sunshine_host` | the call sequence a Sunshine-style host performs, built as C++23 |
| `padbus_stress` | submissions racing a close and feedback dispatch on four threads |

Set `PADBUS_REQUIRE_DRIVER=1` before running `padbus_sunshine_host` to make a machine without the
bus driver fail instead of skipping the live pass.

### Versioning

`padbus::Runtime::version()` reports the library version, and the CMake package and CPack archive
carry the same number. Headers keep a stable layout within a minor series, behaviour is additive
across minor releases, and incompatibilities only arrive with a major bump.

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

The modern C++ client API in `libpadbus/` targets **Windows 11** and needs a C++17 compiler:

```sh
cmake -S . -B out -DPADBUS_BUILD_TESTS=ON
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

The Windows backend loads `ViGEmClient.dll` at runtime, so `libpadbus` has no link-time dependency on the
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
