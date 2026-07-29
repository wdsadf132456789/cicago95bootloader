# Chicago-95 Kernel Build System

AS = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy

ASFLAGS = -f elf64
CFLAGS = -m64 -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc \
         -I. -c -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LDFLAGS = -T linker.ld -nostdlib -static

BUILD = build

C_SRCS = entry.c kernel.c gdt.c idt.c irq.c timer.c keyboard.c \
         console.c serial.c kmalloc.c process.c pci.c ata.c elf.c \
         init.c shell.c libc.c syscall.c vfs.c device.c btop.c kmsg.c \
         drivers/xhci.c drivers/nvme.c drivers/usb.c \
         drivers/e1000.c drivers/net.c

ASM_SRCS = isr.asm

OBJS = $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRCS))

KERNEL_BIN = $(BUILD)/kernel.bin
KERNEL_ELF = $(BUILD)/kernel.elf

.PHONY: all clean

all: $(KERNEL_BIN)
	@echo "Kernel built: $(KERNEL_BIN)"
	@echo "Size: $$(wc -c < $(KERNEL_BIN)) bytes"

$(BUILD):
	mkdir -p $(BUILD) $(BUILD)/drivers

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.asm | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL_ELF): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
