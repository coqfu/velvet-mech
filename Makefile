SDCC      ?= sdcc
CFLAGS    ?= -mmcs51 --model-small -Iinclude
BUILD     := build

COMMON    := src/main.c src/usb.c src/board.c
DETECT_SRC := $(COMMON) src/matrix.c
KBD_SRC    := $(COMMON) src/keyboard.c src/keymap.c src/led.c src/settings.c src/openrgb.c src/macro.c

.PHONY: all detect keyboard clean

all: detect

$(BUILD):
	mkdir -p $(BUILD)

define build_variant
	@mkdir -p $(BUILD)/$(1)
	@for f in $(2); do \
	    echo "  SDCC $$f"; \
	    $(SDCC) $(CFLAGS) $(3) -c $$f -o $(BUILD)/$(1)/`basename $$f .c`.rel || exit 1; \
	done
	$(SDCC) -mmcs51 --model-small $(patsubst src/%.c,$(BUILD)/$(1)/%.rel,$(2)) -o $(BUILD)/$(1).ihx
	packihx $(BUILD)/$(1).ihx > $(BUILD)/$(1).hex
	@echo "built $(BUILD)/$(1).hex"
endef

detect: $(BUILD)
	$(call build_variant,detect,$(DETECT_SRC),-DDETECT_MODE)

keyboard: $(BUILD)
	$(call build_variant,keyboard,$(KBD_SRC),-DOPENRGB)
	@python3 tools/check-isr-overlay.py

clean:
	rm -rf $(BUILD)
