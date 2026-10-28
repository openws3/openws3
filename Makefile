# OpenWS3: open firmware for the WhoopStor V3.
#   make          build build/fw.bin (SDCC >= 4.x, mcs51) and build/openws3.html, the web tool with the firmware inside
#   make test     host unit tests (plain C, run on the PC)
#   make screens  render the screens of the user guide into docs/screens (host build of the real screen code)
#   make clean
SDCC   ?= sdcc
SRCS    = src/main.c src/hal.c src/adc.c src/cal.c src/ctl.c src/fmt.c src/font.c src/lcd.c src/ui.c src/cfg.c src/cfg_pure.c src/input_pure.c src/buzz_pure.c src/chg.c src/sup.c src/app.c src/ports.c src/usb.c src/dbgscr.c src/rowbuf.c src/limits.c src/menu.c src/portset.c src/theme.c src/dev.c
# large model on purpose: locals live in XRAM, IRAM is left for the stack.  NOT medium: MOVX @Ri would use P2
# (a power-path port on this board) as the high address byte.
BUILD  ?= build
CFLAGS  = -I$(BUILD) -mmcs51 --model-large --code-loc 0x0000 --xram-loc 0x0000 --xram-size 8192 --iram-size 256 --Werror
HOSTCC ?= gcc
PURE    = src/ports.c src/menu.c src/portset.c src/theme.c src/rowbuf.c src/limits.c src/dbgscr.c src/cal.c src/ctl.c src/fmt.c src/cfg_pure.c src/input_pure.c src/buzz_pure.c src/chg.c src/sup.c

all: $(BUILD)/fw.bin $(BUILD)/openws3.html

RELS    = $(SRCS:src/%.c=$(BUILD)/%.rel)

$(BUILD)/flags: FORCE
	@mkdir -p $(BUILD)
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

# the build id (git commit, and whether the tree has changes) for the USB telemetry; rewritten only when it changes
$(BUILD)/version_build.h: FORCE
	@mkdir -p $(BUILD)
	@h=$$(git rev-parse --short=8 HEAD 2>/dev/null || echo 00000000); d=$$(git diff --quiet HEAD 2>/dev/null && echo 0 || echo 1); \
	    printf '#define FW_BUILD 0x%sUL\n#define FW_DIRTY %d\n#define FW_BUILD_STR "%s%s"\n' $$h $$d "$$(echo $$h | tr a-f A-F)" "$$([ $$d = 1 ] && echo -DIRTY)" > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@

$(BUILD)/app.rel $(BUILD)/ui.rel: $(BUILD)/version_build.h

$(BUILD)/%.rel: src/%.c $(wildcard src/*.h) $(BUILD)/flags
	@mkdir -p $(BUILD)
	$(SDCC) $(CFLAGS) -c $< -o $@

src/font.c: tools/font5x7.txt tools/gen_font.py
	python3 tools/gen_font.py

# main.rel must come first: it carries the reset vector area
$(BUILD)/fw.bin: $(RELS)
	$(SDCC) $(CFLAGS) $(RELS) -o $(BUILD)/fw.ihx
	@awk '$$3 == "l_XINIT" && $$2 != "00000000" { print "error: initialised XRAM variable (README build rule 1)"; exit 1 }' $(BUILD)/fw.map
	makebin -p -s 65536 $(BUILD)/fw.ihx $(BUILD)/fw.bin   # the flash is 64 KB; makebin defaults to 32 KB
	@echo "image: $$(stat -c %s $(BUILD)/fw.bin) bytes"

$(BUILD)/openws3.html: tools/openws3.html tools/bundle.py $(BUILD)/fw.bin
	python3 tools/bundle.py $(BUILD)/fw.bin tools/openws3.html $@

$(BUILD)/test_%: test/test_%.c $(PURE) $(wildcard src/*.h) test/check.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -Isrc -o $@ $< $(PURE) -lm

TESTS   = $(patsubst test/%.c,$(BUILD)/%,$(wildcard test/test_*.c))

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

$(BUILD)/render_screens: tools/render_screens.c src/ui.c src/font.c $(PURE) $(wildcard src/*.h) $(BUILD)/version_build.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -Isrc -I$(BUILD) -D__code= -o $@ tools/render_screens.c src/ui.c src/font.c $(PURE) -lm

screens: $(BUILD)/render_screens
	python3 tools/render_screens.py $< docs/screens

clean:
	rm -rf build
.PHONY: all test screens clean FORCE
FORCE:
