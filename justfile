# tootboot/justfile

nasm  := "nasm"
cc    := "gcc"
ld    := "ld"
build := "build"

cflags := "-ffreestanding -fno-stack-protector -fno-builtin -fno-pic -m64 -O2 -Wall -Wextra -I. -IbootABI"
ldflags := "-T linker_boot1.ld --no-dynamic-linker -nostdlib"

objs := build + "/boot1.o " + build + "/disk.o " + build + "/bootabi.o " + build + "/tan.o " + build + "/linux.o " + build + "/multiboot.o"

# Default: build bootloader image
default: build

build: (build + "/tootboot.img")

{{build + "/tootboot.img"}}: (build + "/boot0.bin") (build + "/boot1.bin")
    cat {{build}}/boot0.bin {{build}}/boot1.bin > {{build}}/tootboot.img
    truncate -s 1440K {{build}}/tootboot.img
    @echo "[IMG] {{build}}/tootboot.img — $(wc -c < {{build}}/tootboot.img) bytes"

{{build + "/boot0.bin"}}: boot0.asm
    mkdir -p {{build}}
    {{nasm}} -f bin -o {{build}}/boot0.bin boot0.asm
    @echo "[AS]  boot0.asm — $(wc -c < {{build}}/boot0.bin) bytes"

{{build + "/boot1.bin"}}: {{build + "/boot1.o"}} {{build + "/disk.o"}} {{build + "/bootabi.o"}} {{build + "/tan.o"}} {{build + "/linux.o"}} {{build + "/multiboot.o"}} linker_boot1.ld
    {{ld}} {{ldflags}} -o {{build}}/boot1.bin {{objs}}
    @echo "[LD]  {{build}}/boot1.bin — $(wc -c < {{build}}/boot1.bin) bytes"

{{build + "/boot1.o"}}: boot1.c
    mkdir -p {{build}}
    {{cc}} {{cflags}} -c -o {{build}}/boot1.o boot1.c
    @echo "[CC]  boot1.c"

{{build + "/disk.o"}}: disk.asm
    mkdir -p {{build}}
    {{nasm}} -f elf64 -o {{build}}/disk.o disk.asm
    @echo "[AS]  disk.asm"

{{build + "/bootabi.o"}}: bootABI/bootabi.c
    mkdir -p {{build}}
    {{cc}} {{cflags}} -c -o {{build}}/bootabi.o bootABI/bootabi.c
    @echo "[CC]  bootABI/bootabi.c"

{{build + "/tan.o"}}: bootABI/tan.c
    mkdir -p {{build}}
    {{cc}} {{cflags}} -c -o {{build}}/tan.o bootABI/tan.c
    @echo "[CC]  bootABI/tan.c"

{{build + "/linux.o"}}: bootABI/linux.c
    mkdir -p {{build}}
    {{cc}} {{cflags}} -c -o {{build}}/linux.o bootABI/linux.c
    @echo "[CC]  bootABI/linux.c"

{{build + "/multiboot.o"}}: bootABI/multiboot.c
    mkdir -p {{build}}
    {{cc}} {{cflags}} -c -o {{build}}/multiboot.o bootABI/multiboot.c
    @echo "[CC]  bootABI/multiboot.c"

clean:
    rm -rf {{build}}
    @echo "[CLEAN]"
