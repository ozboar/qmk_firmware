# Custom firmware on the stock screen module, see zoom65v3-lcd-firmware
OPT_DEFS += -DLCDFW_ENABLE
OPT_DEFS += -DLCDFW_QMK_VERSION=\"$(shell git describe --tags --abbrev=0 --match '[0-9]*.[0-9]*.[0-9]*' 2>/dev/null)\"
OPT_DEFS += -DLCDFW_GIT_HASH=\"$(shell git rev-parse --short=7 HEAD 2>/dev/null)\"

WPM_ENABLE := yes

SRC += lcdfw/lcdfw.c lcdfw/lcdfw_load.c lcdfw/lcd_image.c
