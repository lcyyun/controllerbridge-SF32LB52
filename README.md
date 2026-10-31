# controllerbridge-SF32LB52

Standalone SF32LB52 controller receiver. The default board is the
**SF32LB52-DevKit-Nano**, SDK target `sf32lb52-nano_n16r16`.

## Features

- DualSense Bluetooth Classic HIDP and NS2Pro BLE inputs, with one active input.
- Xbox 360/XInput, DualSense, DualSense Edge and NS2Pro USB personalities.
- Button/stick/trigger mapping, motion fields, rumble routing and saved settings.
- CherryUSB HID/Feature management and DualSense USB audio/Opus paths.
- Project-local MUSB fixes in `usb/cherryusb_sifli/`.

Xbox means wired Xbox 360, not GIP or console authentication. Cross-controller
rumble is an approximation; adaptive-trigger translation is not implemented.
USB role switching, pairing/reconnect, audio and Edge compatibility need
hardware regression on the exact newly built image. Host tests are not pairing
or device-validation results.

## 0.8.0-preview.1

This preview continues the published `v0.7.0-dev.1` six-route mapping source,
including its older saved-map migrations. DualSense and DualSense Edge share
the Sony mapping output; Xbox and NS2Pro retain independent maps for each
physical input. All 25 canonical controls, including paddles, function keys,
touchpad, mute, Capture and C, remain available. NS2 Minus defaults to Sony
touchpad click like BL616; explicitly changing Back or Touchpad overrides this.

`bridge status` now includes firmware/board identity, `unique_input_reports`,
`duplicate_input_reports`, `input_epoch`, `input_rate_supported`,
`input_rate_time_ms` and `input_battery_percent`. Counts use physical NS2 FD2
timestamps (legacy packet counter) or DualSense timestamp/sequence. Repeated
packets do not refresh the input's 250 ms expiry; stationary fresh packets
count. USB resend and manager polling do not count. Reconnecting or switching
source changes the epoch. `bridge input` returns neutral invalid state on
disconnect or expiry. Physical battery is -1/invalid when unknown or stale;
the USB persona's compatibility battery byte is never manager telemetry.
NS2 battery percentage remains unknown because this decoder does not have a
validated percentage conversion.

BLE wake recovery now accepts an empty directed advertisement from the saved
NS2 peer, ignores canceled or foreign candidates, resumes scanning after a
stopped/timed-out connect, and clears candidates during Forget. All GAP/GATT
work remains deferred outside the Bluetooth callback. Automatic Classic paging
is suspended while the saved NS2 is being recovered. These changes have adapter
tests with a fake SDK; radio timing and controller sleep/wake still require
the Nano hardware sequence below.

For hardware acceptance, pair/save each controller and power-cycle the Nano;
verify only the selected input controls output. For NS2, press Connect to power
it off, wait until input clears, then wake with an ordinary button and repeat
at least ten times, including a failed connection and a receiver restart.
Switch through all four USB personas and verify Windows re-enumeration, mapped
extended buttons, trigger ranges, motion, rumble and Sony audio/haptics.
Save different maps in all six routes, restart and recheck; Forget must prevent
the forgotten peer from reconnecting until paired again. Confirm a stationary
controller shows fresh input rate and disconnect/expiry clears controls and
battery. No attached RP2350 serial helper is a Nano/Pico receiver test target.

## Build on Windows

Install the SiFli SDK at the commit in `sdk.lock.json`, initialize its pinned
submodules, and install the SDK's default GCC tools profile. This project uses
SDK v2.5.0, Arm GCC 14.2.1, Python 3.13 and SCons 4.10.1.
The helper uses those installed tools without downloading or modifying the SDK.

From this repository root:

```powershell
.\tools\build_flash.ps1 -SdkRoot E:\SDKs\SiFli-SDK -ToolsPath E:\SDKs\.sifli-tools
```

Use your own absolute installation paths. `-PythonExe` can select the SDK Python
environment explicitly. `-Jobs` defaults to 8. `-DryRun` previews build commands
(SCons still generates configuration files). No option is needed for build-only;
the old `-NoFlash` option remains accepted for this safe default.

Outputs are under `build_sf32lb52-nano_n16r16_hcpu/`:

- `output/main.bin`
- `bootloader/output/bootloader.bin`
- `ftab.bin`
- `sftool_param.json`, containing the generated flash addresses

SDK object files are redirected into this repository's build directory, not
reused from or written beside SDK sources. SCons is the firmware build system;
`CMakeLists.txt` is a historical scaffold, not a supported build entrypoint.
`boards/SF32LB52_LCD_N16R8/` holds existing local constants; it does not change
the default Nano SDK target.

## Flashing

Flashing requires **both** `-Flash` and `-Port` with the actual connected UART
download port, followed by confirmation. The helper checks the generated image
files and uses the pinned installed sftool 0.1.16. Passing only `-Port` fails.
The CH340 download connection and native USB DP/DM host connection are distinct.
Flashing is never part of host testing.

## Host Tests

With Python and a host GCC or Clang on `PATH`:

```powershell
python .\tools\validate_descriptors.py
Get-ChildItem .\tools\test_*.py | ForEach-Object {
    python $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "Failed: $($_.Name)" }
}
```

The suites cover protocol conversions, runtime arbitration, USB descriptors and
queues, manager framing, audio-haptics, mapping, and fake-NVDS persistence.
These scripts compile and run synthetic inputs; they do not open a device.
`tools/webhid_smoke.html` and `tools/hid_feature_smoke.ps1` are separate,
manual hardware tools.

The standalone Nano build, descriptor validator and all 12 host-test scripts
have passed using the pinned SDK and MinGW GCC. The build still emits SDK
Bluetooth/Opus compiler warnings and RWX-segment linker warnings. This exact
image has not been flashed or tested for pairing.

## Dependencies

See `NOTICE.md`, `LICENSES/` and `sdk.lock.json` for attribution and dependency
pins. The SDK supplies RT-Thread, Bluetooth libraries, CherryUSB and Opus 1.4;
it is not vendored here. `SOURCE-SNAPSHOT.json` records the unchanged import
provenance, not hashes of subsequent standalone-repository edits.
