# OpenWS3: open firmware for the WhoopStor V3.
#   make          build build/fw.bin (SDCC >= 4.x, mcs51)
#   make test     host unit tests (plain C, run on the PC)
#   make clean
SDCC   ?= sdcc
SRCS    = src/main.c src/hal.c src/adc.c src/cal.c src/ctl.c src/fmt.c src/font.c src/lcd.c src/ui.c src/cfg.c src/cfg_pure.c src/input_pure.c src/buzz_pure.c src/chg.c src/sup.c src/app.c src/ports.c src/usb.c src/dbgscr.c src/rowbuf.c src/limits.c src/menu.c src/theme.c
# large model on purpose: locals live in XRAM, IRAM is left for the stack.  NOT medium: MOVX @Ri would use P2
# (a power-path port on this board) as the high address byte.
BUILD  ?= build
CFLAGS  = -mmcs51 --model-large --code-loc 0x0000 --xram-loc 0x0000 --xram-size 8192 --iram-size 256 --Werror
HOSTCC ?= gcc
PURE    = src/theme.c src/rowbuf.c src/limits.c src/dbgscr.c src/cal.c src/ctl.c src/fmt.c src/cfg_pure.c src/input_pure.c src/buzz_pure.c src/chg.c src/sup.c

all: $(BUILD)/fw.bin

RELS    = $(SRCS:src/%.c=$(BUILD)/%.rel)

$(BUILD)/flags: FORCE
	@mkdir -p $(BUILD)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

$(BUILD)/%.rel: src/%.c $(wildcard src/*.h) $(BUILD)/flags
	@mkdir -p $(BUILD)
	$(SDCC) $(CFLAGS) -c $< -o $@

src/font.c: tools/font5x7.txt tools/gen_font.py
	python3 tools/gen_font.py

# main.rel must come first: it carries the reset vector area
$(BUILD)/fw.bin: $(RELS)
	$(SDCC) $(CFLAGS) $(RELS) -o $(BUILD)/fw.ihx
	makebin -p -s 65536 $(BUILD)/fw.ihx $(BUILD)/fw.bin   # the flash is 64 KB; makebin defaults to 32 KB
	@echo "image: $$(stat -c %s $(BUILD)/fw.bin) bytes"

build/test_pure: test/test_pure.c $(PURE) $(wildcard src/*.h) test/check.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -Isrc -o $@ test/test_pure.c $(PURE)

build/test_chg: test/test_chg.c src/chg.c src/ctl.c src/sup.c $(wildcard src/*.h) test/check.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -Isrc -o $@ test/test_chg.c src/chg.c src/ctl.c src/sup.c -lm

build/test_limits: test/test_limits.c src/limits.c src/cal.c src/chg.c src/ctl.c src/sup.c $(wildcard src/*.h) test/check.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -Isrc -o $@ test/test_limits.c src/limits.c src/cal.c src/chg.c src/ctl.c src/sup.c

build/test_menu: test/test_menu.c src/menu.c src/rowbuf.c src/limits.c src/cal.c src/fmt.c src/theme.c src/cfg_pure.c $(wildcard src/*.h) test/check.h
	@mkdir -p build
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -Isrc -o $@ test/test_menu.c src/menu.c src/rowbuf.c src/limits.c src/cal.c src/fmt.c src/theme.c src/cfg_pure.c

test: build/test_pure build/test_chg build/test_limits build/test_menu
	./build/test_pure
	./build/test_chg
	./build/test_limits
	./build/test_menu

clean:
	rm -rf build
.PHONY: all test clean FORCE
FORCE:
