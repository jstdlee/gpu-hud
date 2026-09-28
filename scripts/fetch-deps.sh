#!/usr/bin/env bash
# Fetch pinned third-party sources (Dear ImGui, GLFW, stb_image) into third_party/.
set -euo pipefail
tp="$(cd "$(dirname "$0")/.." && pwd)/third_party"
mkdir -p "$tp"

IMGUI_REV=09f7a0f062b902dc0b377439421abc58d369b1d9   # 1.93 WIP (dynamic fonts)
GLFW_REV=7b6aead9fb88b3623e3b3725ebb42670cbe4c579    # 3.4
STB_REV=2c980bb59875b0d32144a71867fbdebb2f77cd20

fetch() {  # dir url rev
    if [ -d "$tp/$1/.git" ]; then return; fi
    git init -q "$tp/$1"
    git -C "$tp/$1" fetch -q --depth 1 "$2" "$3"
    git -C "$tp/$1" -c advice.detachedHead=false checkout -q FETCH_HEAD
}
fetch imgui https://github.com/ocornut/imgui.git "$IMGUI_REV"
fetch glfw https://github.com/glfw/glfw.git "$GLFW_REV"
mkdir -p "$tp/stb"
[ -f "$tp/stb/stb_image.h" ] || curl -fsSL -o "$tp/stb/stb_image.h" \
    "https://raw.githubusercontent.com/nothings/stb/$STB_REV/stb_image.h"
[ -f "$tp/stb/stb_image_write.h" ] || curl -fsSL -o "$tp/stb/stb_image_write.h" \
    "https://raw.githubusercontent.com/nothings/stb/$STB_REV/stb_image_write.h"
echo "third_party ready"
