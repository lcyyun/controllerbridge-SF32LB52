# Notices and Attribution

controllerbridge-SF32LB52 contains the SF32LB52 firmware exported from the
controller bridge working tree. The following source and protocol attributions
are retained from that tree.

## Main Code Base

The original controller bridge repository was created from `DS5Dongle`:

- Project: DS5Dongle
- Upstream: https://github.com/awalol/DS5Dongle
- Local reference revision: `8760ee3 fix: ci artifact path`
- License: MIT License
- License copy: `LICENSES/DS5Dongle-MIT.txt`

The SF32 firmware follows its DualSense report and HIDP protocol behavior.
This standalone repository does not contain the Pico SDK/TinyUSB/BTstack
runtime or the Pico UF2 build.

## NS2Pro / Switch 2 Pro Protocol Reference

The NS2Pro / Switch 2 Pro controller protocol work in this repository uses
`y700-switch2-pro-bridge` as an important reference:

- Project: y700-switch2-pro-bridge
- Upstream: https://github.com/LeonChrome/y700-switch2-pro-bridge
- Local reference revision: `3697227 Make README bilingual`
- License: Apache License 2.0
- License copy: `LICENSES/y700-switch2-pro-bridge-Apache-2.0.txt`

The bridge uses protocol knowledge and design references from that project,
especially:

- BLE service/characteristic UUIDs and controller discovery heuristics.
- NS2Pro initialization command sequence.
- FD2 input report layout, including stick packing and motion offset notes.
- Nintendo-style USB HID report identity and report ID usage.
- HID OUT to BLE rumble forwarding strategy.
- Report-rate/status concepts used by the local WebHID tuner.

Source files with protocol-derived implementation notes include:

- `protocol/sf32lb52_ns2_protocol.c`
- `src/ble_gatt_sifli.c`
- `src/ns2_profile.c`

The isolated SF32LB52 target also follows DS5Dongle's documented DualSense HID
report and Bluetooth HIDP behavior while using SiFli SDK/CherryUSB APIs rather
than copying the Pico SDK, TinyUSB, or BTstack runtime architecture.  Relevant
files are under `src/ds5_classic_sifli.c`,
`protocol/sf32lb52_bridge_protocol.c`, and
`usb/sf32lb52_usb_device.c`.

## Additional Design References

### Current SF32LB52 Audio-Haptics and Role-Mapping References

The current SF32LB52 audio-haptics conversion and cross-role mapping were
independently implemented after comparing several actively maintained public
projects at pinned revisions:

- Switch2Connect, revision `688f8149ff5441efad713997def484c3cc5e90cc`
  (2026-08-23): DualSense 4-channel UAC capture, channel 3/4 spectral analysis,
  independent ordinary/audio rumble state, and NS2Pro HD-rumble scheduling.
  License file at this revision: GNU GPL v3.
  https://github.com/TommyWabg/Switch2Connect/tree/688f8149ff5441efad713997def484c3cc5e90cc
- S2P-XInput-Lite, revision `1fd759bdcabdfb265bf00c84b3b83e6f205e9694`
  (2026-08-20): audio-haptics activity gating and saturating soft mixing with
  ordinary rumble. License file at this revision: GNU GPL v3.
  https://github.com/duoduo-88/S2P-XInput-Lite/tree/1fd759bdcabdfb265bf00c84b3b83e6f205e9694
- VIIPER, revision `88f66f1ed0c3716c78f810d92b1924112093f896`:
  current DualSense and NS2Pro report packing and cross-role axis conventions.
  License file at this revision: GNU GPL v3.
  https://github.com/Alia5/VIIPER/tree/88f66f1ed0c3716c78f810d92b1924112093f896

No source file from these projects is vendored or copied into this repository.
The SF32 implementation is a fixed-point, allocation-free implementation for
the board's RT-Thread/CherryUSB data path. The older Y700 project remains a
historical NS2 protocol reference for the Pico-era implementation; it is not
the implementation basis for the SF32 audio-haptics converter.

The SF32LB52 bridge architecture and compatibility checklist were also
compared against these public projects supplied as design references:

- https://github.com/AizawaHikaru233/DS5_NS2Pro_Dongle
- https://github.com/lcyyun/ns2pro-bridge
- https://github.com/lcyyun/pico-controller-bridge

They informed interoperability checks such as single-active controller
selection, parsed/repacked USB reports, role management, rumble translation,
and motion preservation. No source file from those three repositories is
vendored into the isolated SF32LB52 target; their own licenses and notices
remain authoritative for their code.

## SDK Components

The build uses SiFli SDK v2.5.0 at commit
`cbe6cd61e6ca310bd045cfab0368ee5759fe1f61`; exact submodule and tool pins are
recorded in `sdk.lock.json`.

- CherryUSB 1.6.0: the linked SDK stack and the modified MUSB port in
  `usb/cherryusb_sifli/`. License copy: `LICENSES/CherryUSB-Apache-2.0.txt`,
  copied byte-for-byte from SDK `external/cherryusb/LICENSE`.
- Opus 1.4: the SDK codec used by the audio paths. License copy:
  `LICENSES/Opus-1.4-COPYING.txt`, copied byte-for-byte from SDK
  `external/opus-1.4/COPYING`. This is not the Pico tree's separate Opus copy.
- RT-Thread, FlashDB, SiFli BSP and Bluetooth libraries are supplied by the SDK
  and retain their own upstream license terms and notices.

The copied license SHA256 values and SDK-relative provenance are in
`sdk.lock.json`. The SDK and its binary tools are not vendored in this source
repository. Firmware release packaging must retain the notices for linked SDK
components as well as these source attributions.

## Trademarks

This project is not affiliated with, endorsed by, or sponsored by Nintendo,
Sony, Valve, Raspberry Pi, or any other hardware/software vendor. Nintendo
Switch, Switch Pro Controller, DualSense, Steam, Raspberry Pi, and related
names are trademarks of their respective owners.
