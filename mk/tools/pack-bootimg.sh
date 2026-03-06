#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'EOF'
Usage:
  pack-bootimg.sh --kernel <Image.gz|Image> --out <boot.img> [options]
  pack-bootimg.sh --list-devices

Options:
  --device <name>             Use internal target layout database (devices/layouts/<name>.env)
  --list-devices              List layout entries from internal database
  --mkbootimg <path>          mkbootimg command/path (default: auto)
  --from-stock <boot.img>     Auto-fill boot params by inspecting stock boot image
  --dtb <path>                Optional DTB path
  --ramdisk <path>            Optional ramdisk path (default: 1-byte zero)
  --cmdline <string>          Kernel cmdline
  --board <name>              Board name
  --os-version <X.Y.Z>        Android OS version
  --os-patch-level <YYYY-MM>  Android patch level
  --base <hex>                Base address
  --kernel-offset <hex>       Kernel offset
  --ramdisk-offset <hex>      Ramdisk offset
  --second-offset <hex>       Second offset
  --tags-offset <hex>         Tags offset
  --dtb-offset <hex>          DTB offset (for header v2/v3 flows)
  --pagesize <bytes>          Page size
  --header-version <n>        Android boot header version
  --second <path>             Optional stage2 blob
  --append-seandroidenforce   Append SEANDROIDENFORCE footer
  --normalize-v0              Normalize legacy v0 fields (second_addr, boot id)

Notes:
  - Default mkbootimg: lk2nd's script when available, else system mkbootimg.
  - --from-stock removes manual offset/pagesize/header entry.
  - --device loads defaults from internal layout DB.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MK_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LAYOUT_DIR="${MK_ROOT}/devices/layouts"
MKBOOTIMG_CMD="${SCRIPT_DIR}/../../lk2nd_src/lk2nd/scripts/mkbootimg"
if [[ ! -x "$MKBOOTIMG_CMD" ]]; then
	MKBOOTIMG_CMD="mkbootimg"
fi

resolve_path() {
	local path="$1"
	if [[ -z "$path" ]]; then
		echo ""
	elif [[ "$path" = /* ]]; then
		echo "$path"
	else
		echo "${MK_ROOT}/${path}"
	fi
}

list_devices() {
	local f
	shopt -s nullglob
	for f in "${LAYOUT_DIR}"/*.env; do
		basename "${f%.env}"
	done
	shopt -u nullglob
}

normalize_offset_for_mkbootimg() {
	python3 - "$1" "$2" <<'PY'
import sys
base = int(sys.argv[1], 0)
off = int(sys.argv[2], 0)
if base + off > 0xffffffff:
    off -= 0x100000000
if off < 0:
    print(f"-0x{(-off):x}")
else:
    print(f"0x{off:x}")
PY
}

extract_dtb_from_boot() {
	local boot_img="$1"
	local out_dtb="$2"
	local workdir="$3"
	local stock_tmp="${workdir}/stock_extract"

	command -v magiskboot >/dev/null 2>&1 || return 1
	rm -rf "$stock_tmp"
	mkdir -p "$stock_tmp"
	cp "$boot_img" "$stock_tmp/boot.img"
	(
		cd "$stock_tmp"
		magiskboot unpack -h boot.img >/dev/null 2>&1 || true
	)
	[[ -f "$stock_tmp/dtb" ]] || return 1
	mkdir -p "$(dirname "$out_dtb")"
	cp "$stock_tmp/dtb" "$out_dtb"
	return 0
}

KERNEL=""
OUT=""
DEVICE=""
LIST_DEVICES=0
FROM_STOCK=""
DTB=""
RAMDISK=""
CMDLINE=""
BOARD=""
OS_VERSION=""
OS_PATCH_LEVEL=""
BASE="0x40000000"
KERNEL_OFFSET="0x00008000"
RAMDISK_OFFSET="0x01100000"
SECOND_OFFSET="0x00f00000"
TAGS_OFFSET="0x00000100"
DTB_OFFSET=""
PAGESIZE="4096"
HEADER_VERSION="2"
SECOND=""
APPEND_SEANDROIDENFORCE=0
NORMALIZE_V0=0

SET_DTB=0
SET_RAMDISK=0
SET_CMDLINE=0
SET_BOARD=0
SET_OS_VERSION=0
SET_OS_PATCH_LEVEL=0
SET_BASE=0
SET_KERNEL_OFFSET=0
SET_RAMDISK_OFFSET=0
SET_SECOND_OFFSET=0
SET_TAGS_OFFSET=0
SET_DTB_OFFSET=0
SET_PAGESIZE=0
SET_HEADER_VERSION=0
SET_APPEND_SEANDROIDENFORCE=0
SET_NORMALIZE_V0=0

LAYOUT_DTB=""
LAYOUT_STOCK_BOOT=""

while [[ $# -gt 0 ]]; do
	case "$1" in
	--device) DEVICE="${2:-}"; shift 2 ;;
	--list-devices) LIST_DEVICES=1; shift ;;
	--mkbootimg) MKBOOTIMG_CMD="${2:-}"; shift 2 ;;
	--from-stock) FROM_STOCK="${2:-}"; shift 2 ;;
	--kernel) KERNEL="${2:-}"; shift 2 ;;
	--out) OUT="${2:-}"; shift 2 ;;
	--dtb) DTB="${2:-}"; SET_DTB=1; shift 2 ;;
	--ramdisk) RAMDISK="${2:-}"; SET_RAMDISK=1; shift 2 ;;
	--cmdline) CMDLINE="${2:-}"; SET_CMDLINE=1; shift 2 ;;
	--board) BOARD="${2:-}"; SET_BOARD=1; shift 2 ;;
	--os-version) OS_VERSION="${2:-}"; SET_OS_VERSION=1; shift 2 ;;
	--os-patch-level) OS_PATCH_LEVEL="${2:-}"; SET_OS_PATCH_LEVEL=1; shift 2 ;;
	--base) BASE="${2:-}"; SET_BASE=1; shift 2 ;;
	--kernel-offset) KERNEL_OFFSET="${2:-}"; SET_KERNEL_OFFSET=1; shift 2 ;;
	--ramdisk-offset) RAMDISK_OFFSET="${2:-}"; SET_RAMDISK_OFFSET=1; shift 2 ;;
	--second-offset) SECOND_OFFSET="${2:-}"; SET_SECOND_OFFSET=1; shift 2 ;;
	--tags-offset) TAGS_OFFSET="${2:-}"; SET_TAGS_OFFSET=1; shift 2 ;;
	--dtb-offset) DTB_OFFSET="${2:-}"; SET_DTB_OFFSET=1; shift 2 ;;
	--pagesize) PAGESIZE="${2:-}"; SET_PAGESIZE=1; shift 2 ;;
	--header-version) HEADER_VERSION="${2:-}"; SET_HEADER_VERSION=1; shift 2 ;;
	--second) SECOND="${2:-}"; shift 2 ;;
	--append-seandroidenforce) APPEND_SEANDROIDENFORCE=1; SET_APPEND_SEANDROIDENFORCE=1; shift ;;
	--normalize-v0) NORMALIZE_V0=1; SET_NORMALIZE_V0=1; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
	esac
done

if [[ "$LIST_DEVICES" == "1" ]]; then
	list_devices
	exit 0
fi

if [[ -n "$DEVICE" ]]; then
	layout_file="${LAYOUT_DIR}/${DEVICE}.env"
	if [[ ! -f "$layout_file" ]]; then
		echo "error: device layout not found: $DEVICE ($layout_file)" >&2
		exit 1
	fi
	# shellcheck disable=SC1090
	source "$layout_file"

	[[ $SET_CMDLINE -eq 1 ]] || CMDLINE="${DEVICE_CMDLINE:-$CMDLINE}"
	[[ $SET_BOARD -eq 1 ]] || BOARD="${DEVICE_BOARD:-$BOARD}"
	[[ $SET_OS_VERSION -eq 1 ]] || OS_VERSION="${DEVICE_OS_VERSION:-$OS_VERSION}"
	[[ $SET_OS_PATCH_LEVEL -eq 1 ]] || OS_PATCH_LEVEL="${DEVICE_OS_PATCH_LEVEL:-$OS_PATCH_LEVEL}"
	[[ $SET_BASE -eq 1 ]] || BASE="${DEVICE_BASE:-$BASE}"
	[[ $SET_KERNEL_OFFSET -eq 1 ]] || KERNEL_OFFSET="${DEVICE_KERNEL_OFFSET:-$KERNEL_OFFSET}"
	[[ $SET_RAMDISK_OFFSET -eq 1 ]] || RAMDISK_OFFSET="${DEVICE_RAMDISK_OFFSET:-$RAMDISK_OFFSET}"
	[[ $SET_SECOND_OFFSET -eq 1 ]] || SECOND_OFFSET="${DEVICE_SECOND_OFFSET:-$SECOND_OFFSET}"
	[[ $SET_TAGS_OFFSET -eq 1 ]] || TAGS_OFFSET="${DEVICE_TAGS_OFFSET:-$TAGS_OFFSET}"
	[[ $SET_DTB_OFFSET -eq 1 ]] || DTB_OFFSET="${DEVICE_DTB_OFFSET:-$DTB_OFFSET}"
	[[ $SET_PAGESIZE -eq 1 ]] || PAGESIZE="${DEVICE_PAGESIZE:-$PAGESIZE}"
	[[ $SET_HEADER_VERSION -eq 1 ]] || HEADER_VERSION="${DEVICE_HEADER_VERSION:-$HEADER_VERSION}"
	[[ $SET_APPEND_SEANDROIDENFORCE -eq 1 ]] || APPEND_SEANDROIDENFORCE="${DEVICE_APPEND_SEANDROIDENFORCE:-$APPEND_SEANDROIDENFORCE}"
	[[ $SET_NORMALIZE_V0 -eq 1 ]] || NORMALIZE_V0="${DEVICE_NORMALIZE_V0:-$NORMALIZE_V0}"
	LAYOUT_DTB="$(resolve_path "${DEVICE_DTB:-}")"
	LAYOUT_STOCK_BOOT="$(resolve_path "${DEVICE_STOCK_BOOT:-}")"
fi

if [[ -z "$KERNEL" || -z "$OUT" ]]; then
	echo "error: --kernel and --out are required" >&2
	usage
	exit 1
fi

if ! command -v "$MKBOOTIMG_CMD" >/dev/null 2>&1 && [[ ! -x "$MKBOOTIMG_CMD" ]]; then
	echo "error: mkbootimg not found: $MKBOOTIMG_CMD" >&2
	exit 1
fi

if [[ ! -f "$KERNEL" ]]; then
	echo "error: kernel not found: $KERNEL" >&2
	exit 1
fi

if [[ -n "$SECOND" && ! -f "$SECOND" ]]; then
	echo "error: second-stage blob not found: $SECOND" >&2
	exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [[ -n "$FROM_STOCK" ]]; then
	if [[ ! -f "$FROM_STOCK" ]]; then
		echo "error: stock boot image not found: $FROM_STOCK" >&2
		exit 1
	fi
	if ! command -v unpackbootimg >/dev/null 2>&1; then
		echo "error: unpackbootimg not found (required by --from-stock)" >&2
		exit 1
	fi

	stock_info="$(unpackbootimg -i "$FROM_STOCK" 2>/dev/null || true)"
	if [[ -z "$stock_info" ]]; then
		echo "error: failed to inspect stock boot image: $FROM_STOCK" >&2
		exit 1
	fi

	stock_get() {
		local key="$1"
		printf '%s\n' "$stock_info" | sed -n "s/^${key}[[:space:]]\\+//p" | head -n1
	}

	[[ $SET_CMDLINE -eq 1 ]] || CMDLINE="$(stock_get BOARD_KERNEL_CMDLINE)"
	[[ $SET_BOARD -eq 1 ]] || BOARD="$(stock_get BOARD_NAME)"
	[[ $SET_BASE -eq 1 ]] || BASE="$(stock_get BOARD_KERNEL_BASE)"
	[[ $SET_PAGESIZE -eq 1 ]] || PAGESIZE="$(stock_get BOARD_PAGE_SIZE)"
	[[ $SET_KERNEL_OFFSET -eq 1 ]] || KERNEL_OFFSET="$(stock_get BOARD_KERNEL_OFFSET)"
	[[ $SET_RAMDISK_OFFSET -eq 1 ]] || RAMDISK_OFFSET="$(stock_get BOARD_RAMDISK_OFFSET)"
	[[ $SET_SECOND_OFFSET -eq 1 ]] || SECOND_OFFSET="$(stock_get BOARD_SECOND_OFFSET)"
	[[ $SET_TAGS_OFFSET -eq 1 ]] || TAGS_OFFSET="$(stock_get BOARD_TAGS_OFFSET)"
	[[ $SET_DTB_OFFSET -eq 1 ]] || DTB_OFFSET="$(stock_get BOARD_DTB_OFFSET)"
	[[ $SET_HEADER_VERSION -eq 1 ]] || HEADER_VERSION="$(stock_get BOARD_HEADER_VERSION)"
	[[ $SET_OS_VERSION -eq 1 ]] || OS_VERSION="$(stock_get BOARD_OS_VERSION)"
	[[ $SET_OS_PATCH_LEVEL -eq 1 ]] || OS_PATCH_LEVEL="$(stock_get BOARD_OS_PATCH_LEVEL)"

	stock_dtb_size="$(stock_get BOARD_DTB_SIZE)"
	if [[ $SET_DTB -eq 0 && -n "${stock_dtb_size:-}" && "${stock_dtb_size:-0}" != "0" ]]; then
		if [[ -n "$LAYOUT_DTB" ]]; then
			if extract_dtb_from_boot "$FROM_STOCK" "$LAYOUT_DTB" "$tmp"; then
				DTB="$LAYOUT_DTB"
				echo "pack-bootimg: cached DTB from stock image -> $DTB"
			fi
		elif extract_dtb_from_boot "$FROM_STOCK" "$tmp/stock.dtb" "$tmp"; then
			DTB="$tmp/stock.dtb"
			echo "pack-bootimg: using DTB extracted from stock image"
		fi
	fi
fi

# Prefer cached per-device DTB if provided and explicit --dtb not used.
if [[ $SET_DTB -eq 0 && -z "$DTB" && -n "$LAYOUT_DTB" && -f "$LAYOUT_DTB" ]]; then
	DTB="$LAYOUT_DTB"
	echo "pack-bootimg: using cached device DTB: $DTB"
fi

# Try to auto-populate DTB from per-device stock image only when needed.
if [[ $SET_DTB -eq 0 && -z "$DTB" && -n "$LAYOUT_STOCK_BOOT" && -f "$LAYOUT_STOCK_BOOT" ]]; then
	if [[ -n "$LAYOUT_DTB" ]]; then
		if extract_dtb_from_boot "$LAYOUT_STOCK_BOOT" "$LAYOUT_DTB" "$tmp"; then
			DTB="$LAYOUT_DTB"
			echo "pack-bootimg: extracted DTB from device stock boot -> $DTB"
		fi
	elif extract_dtb_from_boot "$LAYOUT_STOCK_BOOT" "$tmp/layout.dtb" "$tmp"; then
		DTB="$tmp/layout.dtb"
		echo "pack-bootimg: using DTB extracted from device stock boot"
	fi
fi

if [[ -n "$DTB" && ! -f "$DTB" ]]; then
	echo "error: dtb not found: $DTB" >&2
	exit 1
fi

if [[ -n "$RAMDISK" && ! -f "$RAMDISK" ]]; then
	echo "error: ramdisk not found: $RAMDISK" >&2
	exit 1
fi

if [[ -n "$BASE" && -n "$KERNEL_OFFSET" ]]; then
	KERNEL_OFFSET="$(normalize_offset_for_mkbootimg "$BASE" "$KERNEL_OFFSET")"
fi
if [[ -n "$BASE" && -n "$RAMDISK_OFFSET" ]]; then
	RAMDISK_OFFSET="$(normalize_offset_for_mkbootimg "$BASE" "$RAMDISK_OFFSET")"
fi
if [[ -n "$BASE" && -n "$SECOND_OFFSET" ]]; then
	SECOND_OFFSET="$(normalize_offset_for_mkbootimg "$BASE" "$SECOND_OFFSET")"
fi
if [[ -n "$BASE" && -n "$TAGS_OFFSET" ]]; then
	TAGS_OFFSET="$(normalize_offset_for_mkbootimg "$BASE" "$TAGS_OFFSET")"
fi
if [[ -n "$BASE" && -n "$DTB_OFFSET" ]]; then
	DTB_OFFSET="$(normalize_offset_for_mkbootimg "$BASE" "$DTB_OFFSET")"
fi

if [[ "$HEADER_VERSION" =~ ^[234]$ ]] && [[ -z "$DTB" ]]; then
	echo "error: header_version=$HEADER_VERSION requires DTB; provide --dtb, use --from-stock, or set DEVICE_DTB/DEVICE_STOCK_BOOT in layout" >&2
	exit 1
fi

empty_ramdisk="$tmp/empty-ramdisk"
printf '\0' > "$empty_ramdisk"
if [[ -z "$RAMDISK" ]]; then
	RAMDISK="$empty_ramdisk"
fi

args=(
	--kernel "$KERNEL"
	--ramdisk "$RAMDISK"
	--cmdline "$CMDLINE"
	--base "$BASE"
	--kernel_offset "$KERNEL_OFFSET"
	--ramdisk_offset "$RAMDISK_OFFSET"
	--second_offset "$SECOND_OFFSET"
	--tags_offset "$TAGS_OFFSET"
	--pagesize "$PAGESIZE"
	--header_version "$HEADER_VERSION"
	--output "$OUT"
)

if [[ -n "$BOARD" ]]; then
	args+=( --board "$BOARD" )
fi
if [[ -n "$OS_VERSION" ]]; then
	args+=( --os_version "$OS_VERSION" )
fi
if [[ -n "$OS_PATCH_LEVEL" ]]; then
	args+=( --os_patch_level "$OS_PATCH_LEVEL" )
fi
if [[ -n "$DTB" ]]; then
	args+=( --dtb "$DTB" )
	if [[ -n "$DTB_OFFSET" ]]; then
		args+=( --dtb_offset "$DTB_OFFSET" )
	fi
fi
if [[ -n "$SECOND" ]]; then
	args+=( --second "$SECOND" )
fi

echo "pack-bootimg: mkbootimg ${args[*]}"
"$MKBOOTIMG_CMD" "${args[@]}"

if [[ "$NORMALIZE_V0" == "1" && "$HEADER_VERSION" == "0" ]]; then
	python3 - "$OUT" "$BASE" "$SECOND_OFFSET" <<'PY'
import struct
import sys

path = sys.argv[1]
base = int(sys.argv[2], 0)
second_off = int(sys.argv[3], 0)
second_addr = (base + second_off) & 0xFFFFFFFF

with open(path, "r+b") as f:
    hdr = bytearray(f.read(608))
    if len(hdr) < 608 or hdr[:8] != b"ANDROID!":
        raise SystemExit(f"invalid boot image header: {path}")
    struct.pack_into("<I", hdr, 28, second_addr)
    hdr[576:608] = b"\x00" * 32
    f.seek(0)
    f.write(hdr)
PY
fi

if [[ "$APPEND_SEANDROIDENFORCE" == "1" ]]; then
	printf '%s' 'SEANDROIDENFORCE' >> "$OUT"
fi

echo "pack-bootimg: wrote $OUT"
