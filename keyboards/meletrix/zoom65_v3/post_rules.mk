UART_DRIVER_REQUIRED = yes

SRC += wireless/module.c wireless/wireless.c wireless/sleep.c screen/screen.c idle.c

# Counters for the idle wait, read over raw HID with report 0x59
IDLE_STATS_ENABLE ?= no
ifeq ($(strip $(IDLE_STATS_ENABLE)), yes)
    OPT_DEFS += -DIDLE_STATS_ENABLE
endif

# Custom firmware on the stock screen module, built from zoom65v3-lcd-firmware
LCDFW_ENABLE ?= yes
ifeq ($(strip $(LCDFW_ENABLE)), yes)
    include keyboards/meletrix/zoom65_v3/lcdfw/lcdfw.mk
endif
