# Builds rpg-asm on x86-64 Linux (see docs/asm.md; on other systems run it through the Dockerfile).
#
#   make            generate the game data, assemble and link build/rpg-asm
#   make test       PRNG vectors + every shared/golden scenario + bot parity
#   make test-simulator   simulator report byte-identical to the Python reference (needs ../rpg-python)
#   make clean

NASM ?= nasm
CC ?= gcc
PYTHON ?= python3
SHARED ?= ../shared
REFERENCE ?= ../rpg-python/src

BUILD := build
BIN := $(BUILD)/rpg-asm
SRCS := $(sort $(wildcard src/*/*.asm))
OBJS := $(patsubst src/%.asm,$(BUILD)/%.o,$(SRCS)) $(BUILD)/gamedata.o
INCS := $(wildcard src/*.inc src/*/*.inc)
GENERATED := $(BUILD)/gamedata.inc $(BUILD)/gamedata.asm
DATA := $(wildcard $(SHARED)/data/*.json) $(SHARED)/i18n/en.json

# -w+all -w+error: every NASM warning is an error. The `reloc` class is switched off on purpose: the program
# is linked non-PIE, so `[table + reg*8]` uses 32-bit absolute addresses and tables hold 64-bit pointers.
NASMFLAGS := -f elf64 -g -F dwarf -w+all -w-reloc -w+error -Isrc/ -I$(BUILD)/
# gcc is only the linker driver: it adds the C runtime start files and libc. -no-pie: see NASMFLAGS.
LDFLAGS := -no-pie -Wl,--fatal-warnings

.PHONY: all test test-simulator clean
all: $(BIN)

# The game content is generated from shared/data and shared/i18n: no balance number is written by hand.
$(BUILD)/gamedata.stamp: tools/gen_data.py src/domain/enums.inc $(DATA)
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_data.py --shared $(SHARED) --enums src/domain/enums.inc --out-dir $(BUILD)
	@touch $@

$(GENERATED): $(BUILD)/gamedata.stamp ;

$(BUILD)/gamedata.o: $(BUILD)/gamedata.asm $(INCS)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(BUILD)/%.o: src/%.asm $(INCS) $(BUILD)/gamedata.inc
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

test: $(BIN)
	$(PYTHON) tools/golden_test.py --bin $(BIN) --shared $(SHARED)

test-simulator: $(BIN)
	$(PYTHON) tools/simulator_test.py --bin $(BIN) --reference $(REFERENCE)

clean:
	rm -rf $(BUILD)
