# meletrix/zoom75/tri_mode

![meletrix/zoom75/tri_mode](imgur.com image replace me!)

*A short description of the keyboard/project*

* Keyboard Maintainer: [Mark Hooijberg](https://github.com/MHooijberg)
* Hardware Supported: *The PCBs, controllers supported*
* Hardware Availability: [Official Website Store Page](https://meletrix.com/collections/zoom75-collection)

Make example for this keyboard (after setting up your build environment):

    make meletrix/zoom75/tri_mode:default

Flashing example for this keyboard:

    make meletrix/zoom75/tri_mode:default:flash

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the key at (0,0) in the matrix (usually the top left key or Escape) and plug in the keyboard
* **Default firmware**: The default firmware can be put in DFU mode by using the 'switch to DFU' in the official flashing program provided by Meletrix.
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available
* **Physical reset button**: Hold the reset-button on the back of the PCB while plugging in the USB cable.
