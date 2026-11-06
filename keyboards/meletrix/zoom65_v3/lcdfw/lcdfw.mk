# Custom firmware on the stock screen module, see zoom65v3-lcd-firmware
OPT_DEFS += -DLCDFW_ENABLE
OPT_DEFS += -DLCDFW_QMK_VERSION=\"$(shell git describe --tags --abbrev=0 --match '[0-9]*.[0-9]*.[0-9]*' 2>/dev/null)\"

# The release tag when building one, otherwise the git revision
LCDFW_BUILD_ID := $(shell git describe --tags --exact-match --match 'meletrix_zoom65_v3-v*' 2>/dev/null | sed 's/^meletrix_zoom65_v3-//')
ifeq ($(LCDFW_BUILD_ID),)
    LCDFW_BUILD_ID := $(shell git rev-parse --short=7 HEAD 2>/dev/null)
endif
OPT_DEFS += -DLCDFW_BUILD_ID=\"$(LCDFW_BUILD_ID)\"

WPM_ENABLE := yes

SRC += lcdfw/lcdfw.c lcdfw/lcdfw_load.c lcdfw/lcd_image.c
