# TootBoot Makefile
# make        — build image
# make run    — run in QEMU
# make debug  — run with GDB server on :1234
# make clean

NASM := nasm
CC   := gcc
LD   := ld
QEMU := qemu-system-x86_64

BUILD := build
IMAGE := $(BUILD)/tootboot.img
BOOT0 := $(BUILD)/boot0.bin
BOOT1 := $(BUILD)/boot1.bin

IMAGE_SIZE_KB := 1440

CFLAGS := \
	-ffreestanding \
	-fno-stack-protector \
	-fno-builtin \
	-fno-pic \
	-m64 \
	-O2 \
	-Wall \
	-Wextra \
	-I. \
	-IbootABI

LDFLAGS_BOOT1 := \
	-T linker_boot1.ld \
	--no-dynamic-linker \
	-nostdlib

QEMUFLAGS := \
	-drive format=raw,file=$(IMAGE),if=ide,index=0,media=disk \
	-m 128M \
	-cpu qemu64 \
	-no-reboot \
	-no-shutdown \
	-vga std \
	-display gtk

QEMUFLAGS_DEBUG := $(QEMUFLAGS) -s -S -d int,cpu_reset -D $(BUILD)/qemu.log

OBJS_BOOT1 := \
	$(BUILD)/boot1.o \
	$(BUILD)/bootabi.o \
	$(BUILD)/tan.o \
	$(BUILD)/linux.o \
	$(BUILD)/multiboot.o

.PHONY: all run debug info clean

all: $(IMAGE)

$(IMAGE): $(BOOT0) $(BOOT1)
	cat $(BOOT0) $(BOOT1) > $@
	truncate -s $(IMAGE_SIZE_KB)K $@
	@echo "[IMG] $@ — $$(wc -c < $@) bytes"

$(BOOT0): boot0.asm | $(BUILD)
	$(NASM) -f bin -o $@ $<
	@echo "[AS]  $< — $$(wc -c < $@) bytes"

$(BOOT1): $(OBJS_BOOT1) linker_boot1.ld | $(BUILD)
	$(LD) $(LDFLAGS_BOOT1) -o $@ $(OBJS_BOOT1)
	@echo "[LD]  $@ — $$(wc -c < $@) bytes"

$(BUILD)/boot1.o: boot1.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "[CC]  $<"

$(BUILD)/bootabi.o: bootABI/bootabi.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "[CC]  $<"

$(BUILD)/tan.o: bootABI/tan.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "[CC]  $<"

$(BUILD)/linux.o: bootABI/linux.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "[CC]  $<"

$(BUILD)/multiboot.o: bootABI/multiboot.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "[CC]  $<"

$(BUILD):
	mkdir -p $@

run: $(IMAGE)
	$(QEMU) $(QEMUFLAGS)

debug: $(IMAGE)
	$(QEMU) $(QEMUFLAGS_DEBUG)

info: $(OBJS_BOOT1)
	@echo "=== boot1 + bootABI section sizes ==="
	@size $(OBJS_BOOT1)

clean:
	rm -rf $(BUILD)
	@echo "[CLEAN]"