UART_DRIVER_REQUIRED = yes

SRC += wireless/module.c wireless/wireless.c wireless/sleep.c screen/screen.c

# Custom firmware on the stock screen module, see zoom65v3-lcd-firmware
LCDFW_ENABLE ?= no
ifeq ($(strip $(LCDFW_ENABLE)), yes)
    include keyboards/meletrix/zoom65_v3/lcdfw/lcdfw.mk
    SRC += screen_probe/screen_probe.c
endif

SCREEN_PROBE_ENABLE ?= no
ifeq ($(strip $(SCREEN_PROBE_ENABLE)), yes)
    OPT_DEFS += -DSCREEN_PROBE_ENABLE
    ifneq ($(strip $(LCDFW_ENABLE)), yes)
        SRC += screen_probe/screen_probe.c
    endif
endif
