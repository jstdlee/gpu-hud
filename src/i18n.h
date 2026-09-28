// UI translations (all strings UTF-8).
#pragma once

enum Lang { L_EN, L_ZH_CN, L_ZH_TW, L_JA, L_KO, L_ES, L_FR, L_DE, L_RU, L_PT, L_COUNT };

enum Str {
    S_GPU, S_GPU_MEM, S_UNIFIED, S_RAM, S_CPU, S_SWAP,
    S_TOP_PROC, S_TOP_PROCS, S_NO_PROC, S_NO_GPU,
    S_PID, S_PROCESS, S_USER, S_MEM, S_SM,
    S_COPY_ALL, S_COPIED, S_SETTINGS, S_QUIT, S_CLOSE,
    S_LANGUAGE, S_APPEARANCE, S_BG_OPACITY, S_CONTENT_OPACITY, S_BG_COLOR, S_ACCENT,
    S_BG_IMAGE, S_BROWSE, S_CLEAR, S_IMG_MODE, S_TILE, S_STRETCH, S_FILL, S_CENTER,
    S_IMG_SCALE, S_IMG_OPACITY, S_DROP_HINT, S_IMG_FAILED,
    S_BEHAVIOR, S_ALWAYS_ON_TOP, S_LOCK_POS, S_SHOW_PROCS, S_SHOW_GRAPH, S_REFRESH, S_FONT_SIZE, S_WIDTH,
    S_TEMP, S_POWER, S_CLOCK, S_FAN, S_ENC_DEC,
    S_REPORT_TITLE, S_LIVE, S_SYSTEM, S_HOSTNAME, S_OS, S_KERNEL, S_ARCH, S_UPTIME, S_DESKTOP,
    S_MODEL, S_VENDOR, S_CORES, S_MAX_FREQ, S_MEMORY, S_TOTAL, S_MOTHERBOARD, S_PRODUCT, S_BOARD, S_BIOS,
    S_DRIVER, S_CUDA, S_PCI, S_UUID, S_POWER_LIMIT, S_MAX_CLOCK, S_DISPLAYS, S_STORAGE, S_RENDERER, S_USED,
    S_COUNT
};

extern const char* const kLangCodes[L_COUNT];   // "en", "zh_CN", ...
extern const char* const kLangNames[L_COUNT];   // native names

const char* tr(Str s);
void set_lang(Lang l);
Lang get_lang();
Lang lang_from_code(const char* code);  // accepts "zh_CN.UTF-8", "ja", ...
