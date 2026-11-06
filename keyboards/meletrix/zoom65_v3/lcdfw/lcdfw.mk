# Custom firmware on the stock screen module, built from zoom65v3-lcd-firmware.
# LCDFW_STAGE1 and LCDFW_PAYLOAD use local files, and otherwise it comes from the latest release
ifeq ($(and $(strip $(LCDFW_STAGE1)),$(strip $(LCDFW_PAYLOAD))),)
    LCDFW_RELEASE := $(shell sh keyboards/meletrix/zoom65_v3/lcdfw/fetch_release.sh $(BUILD_DIR)/lcdfw)
    ifeq ($(strip $(LCDFW_RELEASE)),)
        $(error Could not get the LCD firmware. Set LCDFW_STAGE1 and LCDFW_PAYLOAD to local files, or LCDFW_ENABLE=no to build without it)
    endif
    LCDFW_STAGE1 := $(if $(strip $(LCDFW_STAGE1)),$(LCDFW_STAGE1),$(word 1,$(LCDFW_RELEASE)))
    LCDFW_PAYLOAD := $(if $(strip $(LCDFW_PAYLOAD)),$(LCDFW_PAYLOAD),$(word 2,$(LCDFW_RELEASE)))
endif

LCDFW_STAGE1_PATH := $(abspath $(LCDFW_STAGE1))
LCDFW_PAYLOAD_PATH := $(abspath $(LCDFW_PAYLOAD))
ifeq ($(wildcard $(LCDFW_STAGE1_PATH)),)
    $(error LCDFW_STAGE1 must be the path of the first stage loader, got "$(LCDFW_STAGE1)")
endif
ifeq ($(wildcard $(LCDFW_PAYLOAD_PATH)),)
    $(error LCDFW_PAYLOAD must be the path of the firmware, got "$(LCDFW_PAYLOAD)")
endif

OPT_DEFS += -DLCDFW_ENABLE
OPT_DEFS += -DLCDFW_STAGE1_PATH=\"$(LCDFW_STAGE1_PATH)\"
OPT_DEFS += -DLCDFW_PAYLOAD_PATH=\"$(LCDFW_PAYLOAD_PATH)\"
OPT_DEFS += -DLCDFW_QMK_VERSION=\"$(shell git describe --tags --abbrev=0 --match '[0-9]*.[0-9]*.[0-9]*' 2>/dev/null)\"

# The release tag when building one, otherwise the git revision
LCDFW_BUILD_ID := $(shell git describe --tags --exact-match --match 'meletrix_zoom65_v3-v*' 2>/dev/null | sed 's/^meletrix_zoom65_v3-//')
ifeq ($(LCDFW_BUILD_ID),)
    LCDFW_BUILD_ID := $(shell git rev-parse --short=7 HEAD 2>/dev/null)
endif
OPT_DEFS += -DLCDFW_BUILD_ID=\"$(LCDFW_BUILD_ID)\"

WPM_ENABLE := yes

SRC += lcdfw/lcdfw.c lcdfw/lcdfw_load.c lcdfw/lcd_image.c

# The compiler does not track files pulled in by the assembler. The empty rule keeps QMK's
# own %.bin rule from trying to rebuild them
$(BUILD_DIR)/obj_$(TARGET)/lcdfw/lcd_image.o: $(LCDFW_STAGE1_PATH) $(LCDFW_PAYLOAD_PATH)
$(LCDFW_STAGE1_PATH) $(LCDFW_PAYLOAD_PATH): ;
