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

To pair the 2.4 GHz receiver, hold `WL_2G4` until its key blinks fast, then unplug the receiver and plug it back in while the key is still blinking. The receiver keeps its generic `2.4G Dongle` name once paired, and Bluetooth pairings are kept.

On battery in a wireless mode, 1 minute without input turns the RGB off and dims the screen to a quarter. Any input restores them. The keyboard goes to sleep after 3 minutes without input, or after pairing or reconnecting times out, and a key press or turning the knob wakes it. With the cable connected it stays awake.

On battery at 10% or below, the number row pulses red every 30 seconds while RGB is on, and the custom screen firmware shows a red battery icon. Both stop when the cable is connected, and the pulse does not turn the LEDs back on when they are off.

Between events the CPU sleeps, and the system timer runs tickless on TIM2. With no key down the matrix is not scanned until a row or the knob changes. Build with `-e IDLE_STATS_ENABLE=yes` to read the CPU and wait counters over raw HID with report `0x59`.

The custom screen firmware follows the RGB brightness setting, down to a 5% floor, and its MCU sleeps along with the keyboard. The LED power stays off whenever RGB is off, in any mode. Pressing an RGB key shows its RGB page for 800 ms, then it returns to the page it was on.

## Custom screen firmware

Keeps the stock screen module, and can run custom firmware from [`zoom65v3-lcd-firmware`](https://github.com/ozboar/zoom65v3-lcd-firmware) instead of the stock UI. Turn it on or off with the Custom screen firmware toggle in VIA, or with `SC_FRMW` (Fn+/ on the VIA keymap). The change is immediate and saved, and the custom firmware is the default.

The keyboard loads the firmware into the screen's SRAM, at every boot while it is on. The screen's flash is never written, so turning it off resets the screen back to stock.

The firmware isn't in this repo. The build downloads the newest patch [release](https://github.com/ozboar/zoom65v3-lcd-firmware/releases) of the minor version set by `LCDFW_VERSION` in `lcdfw/lcdfw.mk` with `curl`, checks it against the release's checksums, and keeps it in `.build/lcdfw`. To use your own build of it instead, give either or both paths:

```sh
qmk compile -kb meletrix/zoom65_v3/ansi -km via \
    -e LCDFW_STAGE1=/path/to/build/stage1/stage1.bin \
    -e LCDFW_PAYLOAD=/path/to/build/lcd.bin
```

Add `-e LCDFW_ENABLE=no` to build for the stock UI only, with no download.
