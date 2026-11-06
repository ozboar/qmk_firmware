# Zoom65 v3

The most customizable 65% keyboard.

* Keyboard Maintainer: [@ozwaldorf](https://github.com/ozwaldorf)
* Hardware Supported: Zoom65 v3, WB32FQ95. Cases with taillights use the `_tl` revisions.
  * 1.2mm or 1.6mm ANSI-only PCB: `ansi`
  * 1.6mm ISO/ANSI PCB: `iso`
* Hardware Availability: [Meletrix](https://meletrix.com/)

Make examples:

    make meletrix/zoom65_v3/ansi:via
    make meletrix/zoom65_v3/iso:via
    make meletrix/zoom65_v3/ansi_tl:via
    make meletrix/zoom65_v3/iso_tl:via

**Reset Key**: Hold Esc while plugging in the keyboard.

## Keycodes

Their order matches the stock firmware, so stock VIA definitions still line up.

| Keycode | Function |
|---|---|
| `WL_BT1`, `WL_BT2`, `WL_BT3`, `WL_2G4` | Select a wireless device, hold 3 seconds to pair |
| `BT_PRF4`, `BT_PRF5` | Select Bluetooth 4 or 5, hold 3 seconds to pair |
| `WL_USB` | Select USB |
| `SC_TOGG` | Screen on or off |
| `SC_SWCH`, `SC_DOWN`, `SC_UP` | Next page, move down, move up |
| `SC_RST` | Restart the screen |
| `SC_BOOT` | Put the screen in its ROM bootloader |
| `WL_BATT` | Show the battery on the number row while held |
| `SC_FRMW` | Switch between the custom and stock screen firmware |

## Wireless

The driver lives in `wireless/` and plugs into QMK as a custom Bluetooth driver. Raw HID also goes over the 2.4 GHz link, so VIA and `zoom-sync` can reach the keyboard through the dongle.

The keyboard goes to sleep after 3 minutes without input, or after pairing or reconnecting times out. A key press wakes it.

## Custom screen firmware

Keeps the stock screen module, and can run custom firmware from the `zoom65v3-lcd-firmware` repo instead of the stock UI. Turn it on or off with the Custom screen firmware toggle in VIA, or with `SC_FRMW` (Fn+/ on the VIA keymap). The change is immediate and saved, and the stock UI is the default.

The keyboard loads the firmware into the screen's SRAM, at every boot while it is on. The screen's flash is never written, so turning it off resets the screen back to stock.

Add `LCDFW_ENABLE=no` to build without it.

Add `SCREEN_PROBE_ENABLE=yes` to load test builds from the host and read the boot load result.
