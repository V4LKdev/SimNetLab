#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/network_probe.cpp"
BINARY="$SCRIPT_DIR/network_probe"

if [[ ! -f "$SOURCE" ]]; then
  printf 'Missing %s\n' "$SOURCE" >&2
  exit 1
fi

for tool in g++ pkg-config; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Missing required tool: %s\n' "$tool" >&2
    printf 'On Arch Linux: sudo pacman -S --needed base-devel pkgconf zstd\n' >&2
    exit 1
  fi
done

if ! pkg-config --exists libzstd; then
  printf 'libzstd development files were not found.\n' >&2
  printf 'On Arch Linux: sudo pacman -S --needed zstd\n' >&2
  exit 1
fi

if [[ ! -x "$BINARY" || "$SOURCE" -nt "$BINARY" ]]; then
  printf 'Building network probe...\n'
  g++ \
    -std=c++23 \
    -O3 \
    -DNDEBUG \
    -Wall -Wextra -Wpedantic \
    "$SOURCE" \
    -o "$BINARY" \
    $(pkg-config --cflags --libs libzstd)
fi

exec "$BINARY" "$@"
