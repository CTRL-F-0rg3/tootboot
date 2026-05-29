# tootboot/justfile

nasm  := "nasm"
cc    := "gcc"
ld    := "ld"

cflags  := "-ffreestanding -fno-stack-protector -fno-builtin -fno-pic -m64 -O2 -Wall -Wextra -I. -IbootABI"
ldflags := "-T linker_boot1.ld --no-dynamic-linker -nostdlib"
objs    := "build/boot1.o build/bootabi.o build/tan.o build/linux.o build/multiboot.o"

default: build

build: _dirs boot0 boot1 image

_dirs:
    mkdir -p build

boot0:
    {{nasm}} -f bin -o build/boot0.bin boot0.asm
    @echo "[AS]  boot0.asm — $(wc -c < build/boot0.bin) bytes"

boot1: _dirs
    {{cc}} {{cflags}} -c -o build/boot1.o     boot1.c           && echo "[CC]  boot1.c"
    {{cc}} {{cflags}} -c -o build/bootabi.o   bootABI/bootabi.c && echo "[CC]  bootabi.c"
    {{cc}} {{cflags}} -c -o build/tan.o       bootABI/tan.c     && echo "[CC]  tan.c"
    {{cc}} {{cflags}} -c -o build/linux.o     bootABI/linux.c   && echo "[CC]  linux.c"
    {{cc}} {{cflags}} -c -o build/multiboot.o bootABI/multiboot.c && echo "[CC]  multiboot.c"
    {{ld}} {{ldflags}} -o build/boot1.bin {{objs}}
    @echo "[LD]  build/boot1.bin — $(wc -c < build/boot1.bin) bytes"

image:
    cat build/boot0.bin build/boot1.bin > build/tootboot.img
    truncate -s 1440K build/tootboot.img
    @echo "[IMG] build/tootboot.img — $(wc -c < build/tootboot.img) bytes"

clean:
    rm -rf build
    @echo "[CLEAN]"
