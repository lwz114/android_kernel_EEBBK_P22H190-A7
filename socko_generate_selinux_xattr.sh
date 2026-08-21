#!/usr/bin/env bash
set -euo pipefail

# Collect built modules into ./out_a3/socko, then replace/add them in a copy of
# the stock socko filesystem image at ./out_a3/socko.img.

STOCK_SOCKO="${1:-/mnt/c/Users/rtyus/Desktop/socko.img}"
SELABEL="${2:-u:object_r:vendor_file:s0}"
KERNEL_ROOT="$(pwd -P)"
SOCKO_DIR="$KERNEL_ROOT/out_a3/socko"
SOCKO_IMG="$KERNEL_ROOT/out_a3/socko.img"
KERNEL_OWNER="$(stat -c '%u:%g' "$KERNEL_ROOT")"
WORK_DIR="$(mktemp -d /tmp/a3_socko_repack.XXXXXX)"
CMD_FILE="$WORK_DIR/debugfs.cmds"
DEBUGFS_LOG="$WORK_DIR/debugfs.log"
FSCK_LOG="$WORK_DIR/e2fsck.log"
SELABEL_FILE="$WORK_DIR/selabel.bin"

cleanup() {
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required tool: $1" >&2
    exit 1
  }
}

need cp
need debugfs
need e2fsck
need find
need python3
need sort
need stat

if [[ "$EUID" -ne 0 ]]; then
  echo "security.selinux xattr requires root; run this script with sudo." >&2
  exit 1
fi

if [[ ! -f "$STOCK_SOCKO" ]]; then
  echo "stock socko image not found: $STOCK_SOCKO" >&2
  exit 1
fi

mkdir -p "$SOCKO_DIR"

mapfile -d '' found_modules < <(
  find . -type f -name '*.ko' ! -path './out_a3/socko/*' -print0 | sort -z
)

if [[ "${#found_modules[@]}" -eq 0 ]]; then
  echo "no .ko modules were found under $KERNEL_ROOT" >&2
  exit 1
fi

find "$SOCKO_DIR" -maxdepth 1 -type f -name '*.ko' -delete

declare -A copied_basenames=()
duplicates=0
for rel in "${found_modules[@]}"; do
  src="$KERNEL_ROOT/${rel#./}"
  dst="$(basename "$rel")"

  if [[ -n "${copied_basenames[$dst]:-}" ]]; then
    echo "WARN duplicate module basename: $dst; using $rel over ${copied_basenames[$dst]}" >&2
    duplicates=$((duplicates + 1))
  fi

  copied_basenames[$dst]="$rel"
  cp -f "$src" "$SOCKO_DIR/$dst"
done

mapfile -d '' socko_modules < <(
  find "$SOCKO_DIR" -maxdepth 1 -type f -name '*.ko' -print0 | sort -z
)

python3 - "$SELABEL" "${socko_modules[@]}" <<'PY'
import os
import sys

label = sys.argv[1].encode() + b"\0"
for module in sys.argv[2:]:
    os.setxattr(module, b"security.selinux", label)
PY

chown "$KERNEL_OWNER" "${socko_modules[@]}"

cp -f "$STOCK_SOCKO" "$SOCKO_IMG"
printf '%s\0' "$SELABEL" > "$SELABEL_FILE"
: > "$CMD_FILE"

for module in "${socko_modules[@]}"; do
  dst="$(basename "$module")"
  {
    printf 'rm /%s\n' "$dst"
    printf 'write %s /%s\n' "$module" "$dst"
    printf 'ea_set -f %s /%s security.selinux\n' "$SELABEL_FILE" "$dst"
  } >> "$CMD_FILE"
done

debugfs -w -f "$CMD_FILE" "$SOCKO_IMG" > "$DEBUGFS_LOG" 2>&1
if grep -Eq 'Usage:|Could not allocate|while .* extended attribute|Permission denied' "$DEBUGFS_LOG"; then
  cat "$DEBUGFS_LOG" >&2
  echo "debugfs failed while updating $SOCKO_IMG" >&2
  exit 1
fi

set +e
e2fsck -fy "$SOCKO_IMG" > "$FSCK_LOG" 2>&1
fsck_status=$?
set -e
if [[ "$fsck_status" -gt 1 ]]; then
  cat "$FSCK_LOG" >&2
  echo "e2fsck failed for $SOCKO_IMG" >&2
  exit 1
fi

stock_size="$(stat -c '%s' "$STOCK_SOCKO")"
output_size="$(stat -c '%s' "$SOCKO_IMG")"
if [[ "$output_size" -ne "$stock_size" ]]; then
  echo "socko image size changed: stock=$stock_size output=$output_size" >&2
  exit 1
fi

chown "$KERNEL_OWNER" "$SOCKO_IMG"

echo "module search root: $KERNEL_ROOT"
echo "staged socko dir: $SOCKO_DIR"
echo "socko image: $SOCKO_IMG"
echo "modules found: ${#found_modules[@]}"
echo "modules staged/packed: ${#socko_modules[@]}"
echo "duplicate basenames overwritten: $duplicates"
echo "selinux label: $SELABEL"
echo "image size: $output_size bytes"
