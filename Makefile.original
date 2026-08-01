# Chicago-95 BrainFS Bootloader - Build System
# Assembles Stage 1, compiles Stage 2 + Stage 3 into flat binary

.DEFAULT_GOAL := all

AS = nasm
CC = gcc
LD = ld

# Flags
ASFLAGS = -f bin -Wno-label-redef-late
CFLAGS_32 = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc -Iinclude -c -O2 -Wall
CFLAGS_64 = -m64 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc -Iinclude -Istage3 -c -O2 -Wall
LDFLAGS_32 = -m elf_i386 -Ttext 0x0600 --oformat binary -e stage2_entry
LDFLAGS_64 = -Ttext 0x100000 --oformat binary -e stage3_entry
LDFLAGS_64_STAGE4 = -Ttext 0x2000 --oformat binary -e stage4_entry
LDFLAGS_64_STAGE5 = -Ttext 0x30000 --oformat binary -e stage5_entry
LDFLAGS_64_STAGE6 = -Ttext 0x40000 --oformat binary -e stage6_entry
LDFLAGS_64_STAGE7 = -Ttext 0x50000 --oformat binary -e stage7_entry
LDFLAGS_64_STAGE8 = -Ttext 0x60000 --oformat binary -e stage8_entry

# Include LDFLAGS for generated stages 9-100
-include $(BUILD)/ldflags.mk

# Directories
BUILD = build
STAGE1 = stage1
STAGE2 = stage2
STAGE3 = stage3
STAGE4 = stage4
SEC = $(STAGE2)/security

# Stage 1
STAGE1_SRC = $(STAGE1)/boot.asm
STAGE1_BIN = $(BUILD)/stage1.bin

# Stage 2 objects
# Optional hardware-dependent drivers (detected by scan)
STAGE2_OPT_SRCS = \
	$(STAGE2)/drivers/nic_e1000.c \
	$(STAGE2)/drivers/wifi/wifi_core.c \
	$(STAGE2)/drivers/wifi/wifi_autodetect.c \
	$(STAGE2)/drivers/wifi/intel/wifi_intel.c \
	$(STAGE2)/drivers/wifi/atheros/wifi_atheros.c \
	$(STAGE2)/drivers/wifi/broadcom/wifi_broadcom.c \
	$(STAGE2)/drivers/wifi/realtek/wifi_realtek.c \
	$(STAGE2)/drivers/wifi/mediatek/wifi_mediatek.c \
	$(STAGE2)/drivers/wifi/marvell/wifi_marvell.c \
	$(STAGE2)/drivers/wifi/prism5/wifi_prism5.c

# Allow override from scanner output
-include $(BUILD)/drivers.mk

STAGE2_ASM_SRCS = $(STAGE2)/entry.asm

STAGE2_SRCS = \
	$(STAGE2)/main.c \
	$(SEC)/firewall/gen2_packet_filter/fw_gen2_packet_filter.c \
	$(SEC)/firewall/gen2_stateful/fw_gen2_stateful.c \
	$(SEC)/firewall/gen2_app_layer/fw_gen2_app_layer.c \
	$(SEC)/firewall/gen2_adaptive/fw_gen2_adaptive.c \
	$(SEC)/dns_encrypt/doh/dns_doh.c \
	$(SEC)/dns_encrypt/dot/dns_dot.c \
	$(SEC)/dns_encrypt/dnscrypt/dns_dnscrypt.c \
	$(SEC)/wifi_encrypt/wpa2_aes/wpa2_aes.c \
	$(SEC)/wifi_encrypt/wpa3_sae/wpa3_sae.c \
	$(SEC)/mac_encrypt/mac_random/mac_random.c \
	$(SEC)/mac_encrypt/mac_clone/mac_clone.c \
	$(SEC)/mac_encrypt/mac_mask/mac_mask.c \
	$(SEC)/mac_encrypt/mac_rot/mac_rot.c \
	$(SEC)/mac_encrypt/mac_oui/mac_oui.c \
	$(SEC)/anti_ip/anti_ip.c \
	$(SEC)/disk_encrypt/uuid_random/uuid_random.c \
	$(SEC)/disk_encrypt/serial_mask/serial_mask.c \
	$(SEC)/disk_encrypt/gpt_header/gpt_header.c \
	$(SEC)/disk_encrypt/mbr_scramble/mbr_scramble.c \
	$(SEC)/disk_encrypt/luks_camouflage/luks_camouflage.c \
	$(SEC)/disk_encrypt/partname_encrypt/partname_encrypt.c \
	$(SEC)/disk_encrypt/label_encrypt/label_encrypt.c \
	$(SEC)/disk_encrypt/smart_obfuscate/smart_obfuscate.c \
	$(SEC)/disk_encrypt/inquiry_scramble/inquiry_scramble.c \
	$(SEC)/disk_encrypt/fingerprint_rotator/fingerprint_rotator.c \
	$(SEC)/disk_privacy/uuid_encrypt/uuid_encrypt.c \
	$(SEC)/disk_privacy/uuid_rot/uuid_rot.c \
	$(SEC)/disk_privacy/uuid_clone/uuid_clone.c \
	$(SEC)/disk_privacy/disk_serial_mask/disk_serial_mask.c \
	$(SEC)/disk_privacy/disk_model_spoof/disk_model_spoof.c \
	$(SEC)/disk_privacy/disk_label_enc/disk_label_enc.c \
	$(SEC)/disk_privacy/disk_mbr_gpt_mask/disk_mbr_gpt_mask.c \
	$(SEC)/disk_privacy/disk_lba_scramble/disk_lba_scramble.c \
	$(SEC)/disk_privacy/disk_size_obfs/disk_size_obfs.c \
	$(SEC)/disk_privacy/disk_fs_hide/disk_fs_hide.c \
	$(STAGE2)/fs/brainfs_fat.c \
	$(STAGE2)/fs/brainfs_core.c \
	$(STAGE2)/fs/brainvfs.c \
	$(STAGE2)/fs/encfs_mount.c \
	$(STAGE2)/security/netguard/netguard.c \
	$(SEC)/common/aes/aes256.c \
	$(SEC)/common/sha256/sha256.c \
	$(SEC)/common/sha512/sha512_stub.c \
	$(SEC)/common/chacha20/chacha20.c \
	$(SEC)/common/poly1305/poly1305_stub.c \
	$(SEC)/common/rfc4/rfc4.c \
	$(SEC)/common/hkdf/hkdf.c \
	$(SEC)/common/crypto/crypto.c \
	$(SEC)/common/panic/panic.c \
	$(SEC)/common/pgp/pgp.c \
	$(SEC)/common/pgp/pgp_mpi.c \
	$(SEC)/john/john_core.c \
	$(SEC)/john/john_hash.c \
	$(SEC)/john/john_brute.c \
	$(SEC)/john/john_wordlist.c \
	$(SEC)/john/john_analyzer.c \
	$(SEC)/john/john_table.c \
	$(STAGE2)/security/tor/core/tor_core.c \
	$(SEC)/tor/socks5/tor_socks5.c \
	$(SEC)/tor/directory/tor_directory.c \
	$(SEC)/tor/hidden_service/tor_hs.c \
	$(STAGE2)/security/tor/tor_bootstrapper.c \
	$(STAGE2)/vga/vga.c \
	$(STAGE2)/memory/pmm.c \
	$(STAGE2)/memory/vmm.c \
	$(STAGE2)/menu/menu.c \
	$(STAGE2)/tape/ata.c \
	$(STAGE2)/tape/floppy.c \
	$(STAGE2)/boot/ring0_init.c \
	$(STAGE2)/boot/pre_init.c \
	$(STAGE2)/shell/shell.c \
	$(STAGE2)/shell/fish_shell.c \
	$(STAGE2)/shell/gnu_tools.c \
	$(STAGE2)/shell/awk.c \
	$(STAGE2)/shell/nano.c \
	$(STAGE2)/boot/fs_menu.c \
	$(STAGE2)/drivers/mouse.c \
	$(STAGE2)/gui/gui.c \
	$(STAGE2)/drivers/mouse.c \
	$(STAGE2)/gui/gui.c \
	lib/real/real.c \
	lib/protected/protected.c \
	lib/long/long.c \
	lib/divmod64.c \
	lib/mem.c \
	$(STAGE2_OPT_SRCS)

STAGE2_OBJS = $(patsubst %.asm,$(BUILD)/%.o,$(STAGE2_ASM_SRCS)) $(patsubst %.c,$(BUILD)/%.o,$(STAGE2_SRCS))

# Stage 3 objects
STAGE3_SRCS = $(wildcard $(STAGE3)/*.c)
STAGE3_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(STAGE3_SRCS))

# Stage 4 objects
STAGE4_SRCS = $(STAGE4)/main.c
STAGE4_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(STAGE4_SRCS))

# Stage 5 objects (boot manager)
STAGE5 = stage5
STAGE5_SRCS = $(STAGE5)/main.c
STAGE5_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(STAGE5_SRCS))
STAGE5_BIN = $(BUILD)/stage5.bin

# Stage 6 objects (recovery shell)
STAGE6 = stage6
STAGE6_SRCS = $(STAGE6)/main.c
STAGE6_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(STAGE6_SRCS))
STAGE6_BIN = $(BUILD)/stage6.bin

# Stage 7 objects (Tetris)
STAGE7 = stage7
STAGE7_SRCS = $(STAGE7)/main.c
STAGE7_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(STAGE7_SRCS))
STAGE7_BIN = $(BUILD)/stage7.bin

# Stage 8 objects (Snake)
STAGE8 = stage8
STAGE8_SRCS = $(STAGE8)/main.c
STAGE8_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(STAGE8_SRCS))
STAGE8_BIN = $(BUILD)/stage8.bin

# Final outputs
STAGE2_BIN = $(BUILD)/stage2.bin
STAGE3_BIN = $(BUILD)/stage3.bin
STAGE4_BIN = $(BUILD)/stage4.bin
KERNEL_BIN = kernel/build/kernel.bin
BOOT_IMG   = $(BUILD)/chicago95.bin

# Include generated stages 9-100 definitions and LDFLAGS (auto-generated if missing)
$(BUILD)/stages.mk $(BUILD)/ldflags.mk: tools/gen_stages.py
	@mkdir -p $(BUILD)
	python3 $<

-include $(BUILD)/stages.mk
-include $(BUILD)/ldflags.mk

# Stage LBAs (for disk layout)
STAGE5_LBA = 0x800
STAGE6_LBA = 0x810

.PHONY: all clean dirs gen-stages

all: dirs $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) $(STAGE4_BIN) $(STAGE5_BIN) $(STAGE6_BIN) $(STAGE7_BIN) $(STAGE8_BIN) $(STAGE_BINS) $(KERNEL_BIN) $(BOOT_IMG)
	@echo "Build complete: $(BOOT_IMG)"

$(KERNEL_BIN):
	$(MAKE) -C kernel

dirs:
	@mkdir -p $(BUILD)/$(STAGE1) \
		$(BUILD)/$(STAGE2)/security/firewall/gen2_packet_filter \
		$(BUILD)/$(STAGE2)/security/firewall/gen2_stateful \
		$(BUILD)/$(STAGE2)/security/firewall/gen2_app_layer \
		$(BUILD)/$(STAGE2)/security/firewall/gen2_adaptive \
		$(BUILD)/$(STAGE2)/security/dns_encrypt/doh \
		$(BUILD)/$(STAGE2)/security/dns_encrypt/dot \
		$(BUILD)/$(STAGE2)/security/dns_encrypt/dnscrypt \
		$(BUILD)/$(STAGE2)/security/wifi_encrypt/wpa2_aes \
		$(BUILD)/$(STAGE2)/security/wifi_encrypt/wpa3_sae \
		$(BUILD)/$(STAGE2)/security/mac_encrypt/mac_random \
		$(BUILD)/$(STAGE2)/security/mac_encrypt/mac_clone \
		$(BUILD)/$(STAGE2)/security/mac_encrypt/mac_mask \
		$(BUILD)/$(STAGE2)/security/mac_encrypt/mac_rot \
		$(BUILD)/$(STAGE2)/security/mac_encrypt/mac_oui \
		$(BUILD)/$(STAGE2)/security/anti_ip \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/uuid_random \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/serial_mask \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/gpt_header \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/mbr_scramble \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/luks_camouflage \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/partname_encrypt \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/label_encrypt \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/smart_obfuscate \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/inquiry_scramble \
		$(BUILD)/$(STAGE2)/security/disk_encrypt/fingerprint_rotator \
		$(BUILD)/$(STAGE2)/security/disk_privacy/uuid_encrypt \
		$(BUILD)/$(STAGE2)/security/disk_privacy/uuid_rot \
		$(BUILD)/$(STAGE2)/security/disk_privacy/uuid_clone \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_serial_mask \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_model_spoof \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_label_enc \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_mbr_gpt_mask \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_lba_scramble \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_size_obfs \
		$(BUILD)/$(STAGE2)/security/disk_privacy/disk_fs_hide \
		$(BUILD)/$(STAGE2)/security/tor/core \
		$(BUILD)/$(STAGE2)/security/tor/socks5 \
		$(BUILD)/$(STAGE2)/security/tor/directory \
		$(BUILD)/$(STAGE2)/security/tor/hidden_service \
		$(BUILD)/$(STAGE2)/drivers \
		$(BUILD)/$(STAGE2)/drivers/wifi \
		$(BUILD)/$(STAGE2)/drivers/wifi/intel \
		$(BUILD)/$(STAGE2)/drivers/wifi/atheros \
		$(BUILD)/$(STAGE2)/drivers/wifi/broadcom \
		$(BUILD)/$(STAGE2)/drivers/wifi/realtek \
		$(BUILD)/$(STAGE2)/drivers/wifi/mediatek \
		$(BUILD)/$(STAGE2)/drivers/wifi/prism5 \
		$(BUILD)/$(STAGE2)/drivers/wifi/marvell \
		$(BUILD)/$(STAGE2)/fs \
		$(BUILD)/$(STAGE2)/boot \
		$(BUILD)/$(STAGE2)/shell \
		$(BUILD)/$(STAGE2)/vga \
		$(BUILD)/$(STAGE2)/security/netguard \
		$(BUILD)/$(STAGE2)/security/common/aes \
		$(BUILD)/$(STAGE2)/security/common/sha256 \
		$(BUILD)/$(STAGE2)/security/common/sha512 \
		$(BUILD)/$(STAGE2)/security/common/chacha20 \
		$(BUILD)/$(STAGE2)/security/common/poly1305 \
		$(BUILD)/$(STAGE2)/security/common/rfc4 \
		$(BUILD)/$(STAGE2)/security/common/hkdf \
		$(BUILD)/$(STAGE2)/security/common/crypto \
		$(BUILD)/$(STAGE2)/security/common/panic \
		$(BUILD)/$(STAGE2)/security/common/pgp \
		$(BUILD)/$(STAGE2)/security/john \
		$(BUILD)/$(STAGE2)/drivers \
		$(BUILD)/$(STAGE2)/gui \
		$(BUILD)/$(STAGE2)/vga \
		$(BUILD)/$(STAGE2)/memory \
		$(BUILD)/$(STAGE2)/menu \
		$(BUILD)/$(STAGE2)/tape \
		$(BUILD)/lib/real \
		$(BUILD)/lib/protected \
		$(BUILD)/lib/long \
		$(BUILD)/$(STAGE3) \
		$(BUILD)/$(STAGE4) \
		$(BUILD)/$(STAGE5) \
		$(BUILD)/$(STAGE6) \
		$(BUILD)/$(STAGE7) \
		$(BUILD)/$(STAGE8) \
		$(STAGE_DIRS)

# Stage 1: MBR (512 bytes)
$(STAGE1_BIN): $(STAGE1_SRC)
	$(AS) $(ASFLAGS) $< -o $@

# Stage 2: compile each .c to .o, assemble each .asm to .o, then link
$(BUILD)/%.o: %.asm
	@mkdir -p $(@D)
	nasm -f elf32 $< -o $@

$(BUILD)/%.o: %.c
	$(CC) $(CFLAGS_32) $< -o $@

$(STAGE2_BIN): $(STAGE2_OBJS)
	$(LD) $(LDFLAGS_32) $^ -o $@

# Stage 3: compile and link as 64-bit
$(BUILD)/$(STAGE3)/%.o: $(STAGE3)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_64) $< -o $@

$(STAGE3_BIN): $(STAGE3_OBJS)
	$(LD) $(LDFLAGS_64) $^ -o $@

# Stage 4: compile and link as 64-bit
$(BUILD)/$(STAGE4)/%.o: $(STAGE4)/%.c
	$(CC) $(CFLAGS_64) $< -o $@

$(STAGE4_BIN): $(STAGE4_OBJS)
	$(LD) $(LDFLAGS_64_STAGE4) $^ -o $@

# Stage 5: compile and link as 64-bit (boot manager at 0x30000)
$(BUILD)/$(STAGE5)/%.o: $(STAGE5)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_64) $< -o $@

$(STAGE5_BIN): $(STAGE5_OBJS)
	$(LD) $(LDFLAGS_64_STAGE5) $^ -o $@

# Stage 6: compile and link as 64-bit (recovery shell at 0x40000)
$(BUILD)/$(STAGE6)/%.o: $(STAGE6)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_64) $< -o $@

$(STAGE6_BIN): $(STAGE6_OBJS)
	$(LD) $(LDFLAGS_64_STAGE6) $^ -o $@

# Stage 7: compile and link as 64-bit (Tetris at 0x50000)
$(BUILD)/$(STAGE7)/%.o: $(STAGE7)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_64) $< -o $@

$(STAGE7_BIN): $(STAGE7_OBJS)
	$(LD) $(LDFLAGS_64_STAGE7) $^ -o $@

# Stage 8: compile and link as 64-bit (Snake at 0x60000)
$(BUILD)/$(STAGE8)/%.o: $(STAGE8)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_64) $< -o $@

$(STAGE8_BIN): $(STAGE8_OBJS)
	$(LD) $(LDFLAGS_64_STAGE8) $^ -o $@

# Final image: bootloader + up to 100 stages at designated LBAs, kernel, padding to 1GB
# Layout on disk (512-byte sectors):
#   LBA 0x0000:      Stage1
#   LBA 0x0001+:     Stage2 + Stage3 + Stage4 (concatenated)
#   LBA 0x0400:      Stage3 (loaded by Stage1 to 0x100000)
#   LBA 0x0430:      Stage4 (unused by current chain)
#   LBA 0x0800:      Stage5 (boot manager)
#   LBA 0x0810:      Stage6 (recovery shell)
#   LBA 0x0820:      Stage7 (Tetris)
#   LBA 0x0830:      Stage8 (Snake)
#   LBA 0x0840-0x0DF0: Stages 9-100 (each 16 sectors)
#   LBA 0x1000:      Kernel (loaded by Stage1 to 0x10000)
# All remaining space padded to 1GB
1GB = 1073741824

$(BOOT_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(STAGE3_BIN) $(STAGE4_BIN) $(STAGE5_BIN) $(STAGE6_BIN) $(STAGE7_BIN) $(STAGE8_BIN) $(STAGE_BINS) $(KERNEL_BIN)
	rm -f $@
	# Stage 1 at LBA 0
	dd if=$(STAGE1_BIN) of=$@ bs=512 seek=0 count=1 conv=notrunc 2>/dev/null
	# Stage 2 at LBA 1 (full 1023-sector image; padded by dd to a sector boundary)
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	# Stage 3 at LBA 0x400
	dd if=$(STAGE3_BIN) of=$@ bs=512 seek=1024 conv=notrunc 2>/dev/null
	# Stage 4 at LBA 0x430
	dd if=$(STAGE4_BIN) of=$@ bs=512 seek=1072 conv=notrunc 2>/dev/null
	# Stage5 at LBA 0x800 through Stage100 at LBA 0xDF0
	for n in 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 \
	         31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 \
	         51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 \
	         71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 \
	         91 92 93 94 95 96 97 98 99 100; do \
		lba=$$((0x800 + ($$n - 5) * 16)); \
		bin="$(BUILD)/stage$${n}.bin"; \
		[ -f "$$bin" ] && dd if="$$bin" of=$@ bs=512 seek=$$lba conv=notrunc 2>/dev/null; \
	done
	# Kernel at LBA 0x1000
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=4096 conv=notrunc 2>/dev/null
	@echo "Stages 1-100 + kernel written at designated LBAs"
	@echo "Unpadded size: $$(wc -c < $@) bytes"
	# Pad to exactly 1GB using truncate
	truncate -s $(1GB) $@
	@echo "Padded to 1GB: $$(wc -c < $@) bytes"

gen-stages:
	python3 tools/gen_stages.py

# Driver scanner tool
TOOLS_SCAN = tools/scan_driver
TOOLS_SCAN_BIN = $(BUILD)/scan_driver

$(TOOLS_SCAN_BIN): $(TOOLS_SCAN).c
	@mkdir -p $(BUILD)
	$(CC) -O2 -Wall -o $@ $<

$(BUILD)/drivers.mk: $(TOOLS_SCAN_BIN)
	$(TOOLS_SCAN_BIN) > $@

scan: $(BUILD)/drivers.mk
	@echo "---"
	@echo "Driver scan complete: $$(grep -c '\[HW \]' $(BUILD)/drivers.mk) hardware drivers selected"
	@echo "Run 'make clean all' to rebuild with only needed drivers"

clean:
	rm -rf $(BUILD)

install-stage4: $(STAGE4_BIN)
	@echo "Write stage4.bin to device at LBA 0x3000:"
	@echo "  dd if=$(STAGE4_BIN) of=/dev/sdX bs=512 seek=12288"
