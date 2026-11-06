UART_DRIVER_REQUIRED = yes

SRC += wireless/module.c wireless/wireless.c wireless/sleep.c screen/screen.c

# Custom firmware on the stock screen module, built from zoom65v3-lcd-firmware
ifneq ($(strip $(LCDFW_STAGE1)$(LCDFW_PAYLOAD)),)
    include keyboards/meletrix/zoom65_v3/lcdfw/lcdfw.mk
endif
