// SPDX-License-Identifier: GPL-2.0-or-later

/* The images come from a zoom65v3-lcd-firmware build, and the ROM bootloader only takes whole words */
__asm__(
    ".section .rodata\n"
    ".balign 4\n"
    ".global lcd_image, lcd_image_size, lcd_stage1, lcd_stage1_size\n"
    "lcd_image:\n"
    ".incbin \"" LCDFW_PAYLOAD_PATH "\"\n"
    ".balign 4, 0xFF\n"
    "lcd_image_end:\n"
    "lcd_stage1:\n"
    ".incbin \"" LCDFW_STAGE1_PATH "\"\n"
    ".balign 4, 0xFF\n"
    "lcd_stage1_end:\n"
    "lcd_image_size:\n"
    ".word lcd_image_end - lcd_image\n"
    "lcd_stage1_size:\n"
    ".word lcd_stage1_end - lcd_stage1\n"
    ".text\n");
