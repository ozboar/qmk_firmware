UART_DRIVER_REQUIRED = yes

SRC += wireless/module.c wireless/wireless.c wireless/sleep.c screen/screen.c

# Custom firmware on the stock screen module, built from zoom65v3-lcd-firmware
LCDFW_ENABLE ?= yes
ifeq ($(strip $(LCDFW_ENABLE)), yes)
    include keyboards/meletrix/zoom65_v3/lcdfw/lcdfw.mk
endif
