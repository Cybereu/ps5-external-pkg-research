PS5_PAYLOAD_SDK ?= /tmp/opencode/sdk/prebuilt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS += -Wall -O2
all: kstuff-diag.elf

kstuff-diag.elf: main.c ../ps5-kstuff/payload.bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c
