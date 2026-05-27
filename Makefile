# TootBoot Makefile
# make        — build image
# make run    — run in QEMU
# make debug  — run with GDB server on :1234
# make clean

NASM := nasm
CC   := gcc
LD   := ld
QEMU := qemu-system-x86_64

CFLAGS := \
    -ffreestanding \
    -fno-stack-protector \
    -fno-builtin \
    -fno-pic \
    -m64 \
    -O2 \
    -Wall \
    -Wextra \
    -I.

LDFLAGS_BOOT1 := \
    -T linker_boot1.ld \
    --no-dynamic-linker \
    -nostdlib

# 1.44MB floppy-sized image so SeaBIOS is happy with INT 13h
IMAGE_SIZE_KB := 1440

QEMUFLAGS := \
    -drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
    -m 128M \
    -cpu qemu64 \
    -no-reboot \
    -no-shutdown \
    -vga std \
    -display gtk

QEMUFLAGS_DEBUG := $(QEMUFLAGS) -s -S -d int,cpu_reset -D $(BUILD)/qemu.log

BUILD := build
IMAGE := $(BUILD)/tootboot.img
BOOT0 := $(BUILD)/boot0.bin
BOOT1 := $(BUILD)/boot1.bin

.PHONY: all run debug info clean

all: $(IMAGE)

# Pad image to IMAGE_SIZE_KB so BIOS geometry works
$(IMAGE): $(BOOT0) $(BOOT1)
	cat $(BOOT0) $(BOOT1) > $(BUILD)/tootboot_raw.img
	cp $(BUILD)/tootboot_raw.img $@
	truncate -s $(IMAGE_SIZE_KB)K $@
	@echo "[IMG] $@ — $$(wc -c < $@) bytes"

$(BOOT0): boot0.asm | $(BUILD)
	$(NASM) -f bin -o $@ $<
	@echo "[AS]  $< — $$(wc -c < $@) bytes"

$(BOOT1): $(BUILD)/boot1.o linker_boot1.ld | $(BUILD)
	$(LD) $(LDFLAGS_BOOT1) -o $@ $<
	@echo "[LD]  $@ — $$(wc -c < $@) bytes"

$(BUILD)/boot1.o: boot1.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "[CC]  $<"

$(BUILD):
	mkdir -p $@

run: $(IMAGE)
	$(QEMU) $(QEMUFLAGS)

debug: $(IMAGE)
	$(QEMU) $(QEMUFLAGS_DEBUG)

info: $(BUILD)/boot1.o
	@echo "=== boot1 sections ==="
	@objdump -h $(BUILD)/boot1.o

clean:
	rm -rf $(BUILD)
	@echo "[CLEAN]"