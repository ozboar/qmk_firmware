# Zoom65v3 Jailbroken QMK/VIA

> :warning: Experimental software, use at your own risk :warning:

## Features

- VIA Compatible
- Tri-mode
  - Full support for USB, Bluetooth and 2.4Ghz
  - Auto-resume last connected device
  - Low power sleep timer
- Enabled all 40+ lighting modes
- Enabled all 5 bluetooth profiles
- Increased LED brightness (safe levels for regular and taillight editions) 
- Screen module support
- Onekey module support
- Rotary encoder module support (with proper resolution)
- No battery indicator lights when wired-only (switch under right shift)

## Known Issues / Caveats

- Missing flashing device indicator lights when searching/pairing
- Screen does not (pseudo) turn off when the keyboard sleeps

## Installation Instructions

1. [Build from source](keyboards/meletrix/zoom65_v3) or download the jailbroken firmware from [the releases page](https://github.com/ozboar/qmk_firmware/releases/latest) for your pcb revision (ANSI-only or ISO-ANSI) and your case (taillight or not)
2. Hold down ESC and plug keyboard in to enter DFU boot mode
3. Flash jailbroken firmware with qmk cli (Linux, Windows, Mac) or qmk toolbox (Windows, Mac)
4. Import via json sideload in the design tab of https://usevia.app
5. Connect and customize device in via

## Uninstallation

Stock firmware can be restored at any point by holding the esc key while plugging in the device and flashing the stock firmware. In the absolute worst case of a bricked keyboard, hold pin 64 to pin 60 while plugging in the device.

## Special Thanks

To the collaborators on the QMK discord; Drashna, Dusky, honorless, sigprof, zvecr, and anyone else who helped figure this board out

## ORIGINAL README ---

[![Current Version](https://img.shields.io/github/tag/qmk/qmk_firmware.svg)](https://github.com/qmk/qmk_firmware/tags)
[![Discord](https://img.shields.io/discord/440868230475677696.svg)](https://discord.gg/qmk)
[![Docs Status](https://img.shields.io/badge/docs-ready-orange.svg)](https://docs.qmk.fm)
[![GitHub contributors](https://img.shields.io/github/contributors/qmk/qmk_firmware.svg)](https://github.com/qmk/qmk_firmware/pulse/monthly)
[![GitHub forks](https://img.shields.io/github/forks/qmk/qmk_firmware.svg?style=social&label=Fork)](https://github.com/qmk/qmk_firmware/)

This is a keyboard firmware based on the [tmk\_keyboard firmware](https://github.com/tmk/tmk_keyboard) with some useful features for Atmel AVR and ARM controllers, and more specifically, the [OLKB product line](https://olkb.com), the [ErgoDox EZ](https://ergodox-ez.com) keyboard, and the Clueboard product line.

### Documentation

* [See the official documentation on docs.qmk.fm](https://docs.qmk.fm)

The docs are powered by [VitePress](https://vitepress.dev/). They are also viewable offline; see [Previewing the Documentation](https://docs.qmk.fm/#/contributing?id=previewing-the-documentation) for more details.

You can request changes by making a fork and opening a [pull request](https://github.com/qmk/qmk_firmware/pulls).

### Supported Keyboards

* [Planck](/keyboards/planck/)
* [Preonic](/keyboards/preonic/)
* [ErgoDox EZ](/keyboards/ergodox_ez/)
* [Clueboard](/keyboards/clueboard/)
* [Cluepad](/keyboards/clueboard/17/)
* [Atreus](/keyboards/atreus/)

The project also includes community support for [lots of other keyboards](/keyboards/).

### Maintainers

QMK is developed and maintained by Jack Humbert of OLKB with contributions from the community, and of course, [Hasu](https://github.com/tmk). The OLKB product firmwares are maintained by [Jack Humbert](https://github.com/jackhumbert), the Ergodox EZ by [ZSA Technology Labs](https://github.com/zsa), the Clueboard by [Zach White](https://github.com/skullydazed), and the Atreus by [Phil Hagelberg](https://github.com/technomancy).

### Official Website

[qmk.fm](https://qmk.m) is the official website of QMK, where you can find links to this page, the documentation, and the keyboards supported by QMK.
