DEVICE_NAME := oppo-a16
SOC_FAMILY := mt6765

# Runtime defaults for initial bring-up.
BOOT_LABEL := boot
ROOT_LABEL := root

# Device-specific toggles (to be consumed by platform code later).
HAS_TOUCH := y
HAS_FB := y
HAS_FASTBOOT_USB := y

# Phoenix recovery metadata for this device family.
PHOENIX_BOOTSTAGE := NATIVE_INIT_POST_FS
PHOENIX_PRIMARY_PARTITION := oplusreserve1
PHOENIX_FALLBACK_PARTITION := opporeserve1
PHOENIX_UFS_OFFSET := 0x456000
PHOENIX_EMMC_OFFSET := 0x440000
PHOENIX_RECORD_MAGIC := kernelog
BCB_PARA_LBA := 202816

# Preferred panel identifier for device-scoped display init.
# The boot tag string is stale on this device; the real recovery kernel binds
# ilt7807s_hlt_jelly_hdp_dsi_vdo_lcm.
LCM_BOOT_NAME := ilt7807s_hlt_jelly_hdp_dsi_vdo_lcm
