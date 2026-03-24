CLANG ?= clang
LD_LLD ?= ld.lld

STAGE0_DIR := stage0/aarch64
STAGE0_OUT_DIR := out/stage0
STAGE0_ELF := $(STAGE0_OUT_DIR)/mk-stage0.elf
STAGE0_BIN := $(STAGE0_OUT_DIR)/mk-stage0.bin
STAGE0_OBJS := \
	$(STAGE0_OUT_DIR)/entry.o \
	$(STAGE0_OUT_DIR)/stage0.o
STAGE0_KPAYLOAD_ELF := $(STAGE0_OUT_DIR)/mk-kpayload.elf
STAGE0_KPAYLOAD_BIN := $(STAGE0_OUT_DIR)/mk-kpayload.bin
STAGE0_KPAYLOAD_GZ := $(STAGE0_OUT_DIR)/mk-kpayload.bin.gz
STAGE0_KPAYLOAD_OBJS := \
	$(STAGE0_OUT_DIR)/kernel_payload.o \
	$(STAGE0_OUT_DIR)/kernel_payload_main.o \
	$(STAGE0_OUT_DIR)/mk_boot.o \
	$(STAGE0_OUT_DIR)/mk_fdt.o \
	$(STAGE0_OUT_DIR)/mk_wdt.o \
	$(STAGE0_OUT_DIR)/mk_pmic.o \
	$(STAGE0_OUT_DIR)/mk_ext2.o \
	$(STAGE0_OUT_DIR)/mk_zlib_gzip.o \
	$(STAGE0_OUT_DIR)/zlib_inflate.o \
	$(STAGE0_OUT_DIR)/zlib_inffast.o \
	$(STAGE0_OUT_DIR)/zlib_inftrees.o \
	$(STAGE0_OUT_DIR)/zlib_zutil.o \
	$(STAGE0_OUT_DIR)/zlib_adler32.o \
	$(STAGE0_OUT_DIR)/mtk_panel.o \
	$(STAGE0_OUT_DIR)/mtk_display.o \
	$(STAGE0_OUT_DIR)/mtk_gpio.o \
	$(STAGE0_OUT_DIR)/mtk_i2c.o \
	$(STAGE0_OUT_DIR)/mtk_dsi.o \
	$(STAGE0_OUT_DIR)/mtk_pwm.o \
	$(STAGE0_OUT_DIR)/mtk_usb.o \
	$(STAGE0_OUT_DIR)/mk_ui.o
DEVICE ?= oppo-a16
DEVICE_PROFILE_MK := devices/$(DEVICE)/profile.mk
BOOTIMG_OUT_DIR := out/bootimg
BOOTIMG_NOKERNEL := $(BOOTIMG_OUT_DIR)/mk-$(DEVICE)-boot.img
BOOTIMG_KERNEL_PAYLOAD ?= $(STAGE0_KPAYLOAD_GZ)
PACK_BOOTIMG ?= ./tools/pack-bootimg.sh

ifneq ("$(wildcard $(DEVICE_PROFILE_MK))","")
include $(DEVICE_PROFILE_MK)
endif

STAGE0_KPAYLOAD_CPPFLAGS :=
ifdef PHOENIX_BOOTSTAGE
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_PHOENIX_BOOTSTAGE=\"$(PHOENIX_BOOTSTAGE)\"
endif
ifdef PHOENIX_PRIMARY_PARTITION
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_PHOENIX_PRIMARY_PARTITION=\"$(PHOENIX_PRIMARY_PARTITION)\"
endif
ifdef PHOENIX_FALLBACK_PARTITION
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_PHOENIX_FALLBACK_PARTITION=\"$(PHOENIX_FALLBACK_PARTITION)\"
endif
ifdef PHOENIX_UFS_OFFSET
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_PHOENIX_UFS_OFFSET=$(PHOENIX_UFS_OFFSET)
endif
ifdef PHOENIX_EMMC_OFFSET
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_PHOENIX_EMMC_OFFSET=$(PHOENIX_EMMC_OFFSET)
endif
ifdef PHOENIX_RECORD_MAGIC
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_PHOENIX_RECORD_MAGIC=\"$(PHOENIX_RECORD_MAGIC)\"
endif
ifdef BCB_PARA_LBA
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_BCB_PARA_LBA=$(BCB_PARA_LBA)
endif
ifdef BOOT_LABEL
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_BOOT_LABEL=\"$(BOOT_LABEL)\"
endif
ifdef ROOT_LABEL
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_ROOT_LABEL=\"$(ROOT_LABEL)\"
endif
ifdef LCM_BOOT_NAME
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_LCM_BOOT_NAME=\"$(LCM_BOOT_NAME)\"
endif
ifeq ($(HAS_FASTBOOT_USB),y)
STAGE0_KPAYLOAD_CPPFLAGS += -DMK_DEVICE_HAS_FASTBOOT_USB=1
endif

.PHONY: all clean stage0 stage0-clean stage0-kpayload bootimg-nokernel

all: stage0 bootimg-nokernel

stage0: $(STAGE0_ELF) $(STAGE0_BIN) stage0-kpayload

$(STAGE0_OUT_DIR):
	mkdir -p $@

$(BOOTIMG_OUT_DIR):
	mkdir -p $@

$(STAGE0_OUT_DIR)/entry.o: $(STAGE0_DIR)/entry.S | $(STAGE0_OUT_DIR)
	$(CLANG) -target aarch64-none-elf -ffreestanding -fno-stack-protector -nostdlib -c $< -o $@

$(STAGE0_OUT_DIR)/stage0.o: $(STAGE0_DIR)/stage0.c | $(STAGE0_OUT_DIR)
	$(CLANG) -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -std=c11 -Wall -Wextra -Werror -O2 -c $< -o $@

$(STAGE0_ELF): $(STAGE0_OBJS) $(STAGE0_DIR)/linker.ld
	$(LD_LLD) -nostdlib -T $(STAGE0_DIR)/linker.ld -o $@ $(STAGE0_OBJS)

$(STAGE0_BIN): $(STAGE0_ELF)
	llvm-objcopy -O binary $< $@

stage0-kpayload: $(STAGE0_KPAYLOAD_ELF) $(STAGE0_KPAYLOAD_BIN) $(STAGE0_KPAYLOAD_GZ)

bootimg-nokernel: stage0-kpayload | $(BOOTIMG_OUT_DIR)
	$(PACK_BOOTIMG) --device "$(DEVICE)" --kernel "$(BOOTIMG_KERNEL_PAYLOAD)" --out "$(BOOTIMG_NOKERNEL)"
	@echo "bootimg-nokernel: $(BOOTIMG_NOKERNEL)"

$(STAGE0_OUT_DIR)/kernel_payload.o: $(STAGE0_DIR)/kernel_payload.S | $(STAGE0_OUT_DIR)
	$(CLANG) -target aarch64-none-elf -ffreestanding -fno-stack-protector -nostdlib -c $< -o $@

$(STAGE0_OUT_DIR)/kernel_payload_main.o: $(STAGE0_DIR)/kernel_payload_main.c $(STAGE0_DIR)/mtk_panel.h $(STAGE0_DIR)/mtk_display.h $(STAGE0_DIR)/peacock_logo_asset.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mk_boot.o: $(STAGE0_DIR)/mk_boot.c $(STAGE0_DIR)/mk_boot.h $(STAGE0_DIR)/mk_ext2.h $(STAGE0_DIR)/mk_ui.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mk_fdt.o: $(STAGE0_DIR)/mk_fdt.c $(STAGE0_DIR)/mk_fdt.h $(STAGE0_DIR)/mk_common.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mk_wdt.o: $(STAGE0_DIR)/mk_wdt.c $(STAGE0_DIR)/mk_wdt.h $(STAGE0_DIR)/mk_common.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mk_pmic.o: $(STAGE0_DIR)/mk_pmic.c $(STAGE0_DIR)/mk_pmic.h $(STAGE0_DIR)/mk_common.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mk_ext2.o: $(STAGE0_DIR)/mk_ext2.c $(STAGE0_DIR)/mk_ext2.h $(STAGE0_DIR)/mtk_storage.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

STAGE0_CFLAGS := -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -nostdlib -std=c11 -Wall -Wextra -Werror -O2 -mgeneral-regs-only

ZLIB_INFLATE_DIR := ../lk2nd_src/lib/zlib_inflate
ZLIB_STAGE0_CFLAGS := -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -nostdlib -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -O2 -mgeneral-regs-only -DZ_SOLO -I$(ZLIB_INFLATE_DIR)

$(STAGE0_OUT_DIR)/mk_zlib_gzip.o: $(STAGE0_DIR)/mk_zlib_gzip.c $(STAGE0_DIR)/mk_zlib_gzip.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(ZLIB_STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/zlib_inflate.o: $(ZLIB_INFLATE_DIR)/inflate.c | $(STAGE0_OUT_DIR)
	$(CLANG) $(ZLIB_STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/zlib_inffast.o: $(ZLIB_INFLATE_DIR)/inffast.c | $(STAGE0_OUT_DIR)
	$(CLANG) $(ZLIB_STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/zlib_inftrees.o: $(ZLIB_INFLATE_DIR)/inftrees.c | $(STAGE0_OUT_DIR)
	$(CLANG) $(ZLIB_STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/zlib_zutil.o: $(ZLIB_INFLATE_DIR)/zutil.c | $(STAGE0_OUT_DIR)
	$(CLANG) $(ZLIB_STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/zlib_adler32.o: $(ZLIB_INFLATE_DIR)/adler32.c | $(STAGE0_OUT_DIR)
	$(CLANG) $(ZLIB_STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_panel.o: $(STAGE0_DIR)/mtk_panel.c $(STAGE0_DIR)/mtk_panel.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_display.o: $(STAGE0_DIR)/mtk_display.c $(STAGE0_DIR)/mtk_display.h $(STAGE0_DIR)/mtk_panel.h $(STAGE0_DIR)/mtk_gpio.h $(STAGE0_DIR)/mtk_i2c.h $(STAGE0_DIR)/mtk_dsi.h $(STAGE0_DIR)/mtk_pwm.h $(STAGE0_DIR)/panel_nt35695b_init.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_gpio.o: $(STAGE0_DIR)/mtk_gpio.c $(STAGE0_DIR)/mtk_gpio.h $(STAGE0_DIR)/mtk_panel.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_i2c.o: $(STAGE0_DIR)/mtk_i2c.c $(STAGE0_DIR)/mtk_i2c.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_dsi.o: $(STAGE0_DIR)/mtk_dsi.c $(STAGE0_DIR)/mtk_dsi.h $(STAGE0_DIR)/mtk_panel.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_pwm.o: $(STAGE0_DIR)/mtk_pwm.c $(STAGE0_DIR)/mtk_pwm.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mtk_usb.o: $(STAGE0_DIR)/mtk_usb.c $(STAGE0_DIR)/mtk_usb.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) -c $< -o $@

$(STAGE0_OUT_DIR)/mk_ui.o: $(STAGE0_DIR)/mk_ui.c $(STAGE0_DIR)/mk_ui.h $(STAGE0_DIR)/mk_common.h $(STAGE0_DIR)/peacock_logo_asset.h | $(STAGE0_OUT_DIR)
	$(CLANG) $(STAGE0_CFLAGS) $(STAGE0_KPAYLOAD_CPPFLAGS) -c $< -o $@

$(STAGE0_KPAYLOAD_ELF): $(STAGE0_KPAYLOAD_OBJS) $(STAGE0_DIR)/kernel_payload.ld
	$(LD_LLD) -nostdlib -T $(STAGE0_DIR)/kernel_payload.ld -o $@ $(STAGE0_KPAYLOAD_OBJS)

$(STAGE0_KPAYLOAD_BIN): $(STAGE0_KPAYLOAD_ELF)
	llvm-objcopy -O binary $< $@

$(STAGE0_KPAYLOAD_GZ): $(STAGE0_KPAYLOAD_BIN)
	gzip -n -c $< > $@

stage0-clean:
	rm -rf $(STAGE0_OUT_DIR)

clean:
	rm -rf $(STAGE0_OUT_DIR)
