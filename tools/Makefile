# p1-probe/Makefile — build the P1 probe payload OFFLINE (local artifact only).
# This ELF is NOT to be deployed by this task; the parent decides on execution.
#
# Usage:
#   PS5_PAYLOAD_SDK=/tmp/opencode/sdk/prebuilt/ps5-payload-sdk make
#
# Host parsing tests (no SDK needed):
#   make test
#   make clean

PS5_PAYLOAD_SDK ?= /tmp/opencode/sdk/prebuilt/ps5-payload-sdk

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := p1_tool.elf
SRC := p1_tool.c parse.c
HOSTCC ?= cc

.PHONY: all test clean

all: $(ELF)

$(ELF): $(SRC) parse.h lvd_types.h
	$(CC) -Wall -g -o $@ $(SRC)

# Host tests for the parsing helpers + LVD layout locks (do not touch the SDK).
test: test_parse
	./test_parse

test_parse: test_parse.c parse.c parse.h lvd_types.h
	$(HOSTCC) -Wall -o $@ test_parse.c parse.c

clean:
	rm -f $(ELF) test_parse
