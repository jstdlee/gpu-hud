// GPU HUD — always-on-top, translucent GPU/RAM monitor (Dear ImGui + GLFW + OpenGL 3).
#include "backends/imgui_impl_opengl3_loader.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "i18n.h"
#include "imgui.h"
#include "metrics.h"
#include "sysinfo.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---------------------------------------------------------------------------
// Config (~/.config/gpu-hud/config.ini)
enum ImgMode { IMG_TILE, IMG_STRETCH, IMG_FILL, IMG_CENTER };

struct Config {
    std::string lang;  // empty = follow $LANG
    float bg_opacity = 0.72f;
    float content_opacity = 1.0f;
    float bg_color[3] = {0.06f, 0.07f, 0.09f};
    float accent[3] = {0.30f, 0.78f, 0.47f};
    std::string bg_image;
    int img_mode = IMG_TILE;
    float img_scale = 1.0f;
    float img_opacity = 0.55f;
    bool on_top = true;
    bool lock_pos = false;
    bool show_procs = true;
    bool show_graph = true;
    int sort_by = 0;  // 0 = GPU memory, 1 = SM utilization
    int refresh_ms = 1000;
    float font_size = 15.0f;
    int width = 380;
    int x = -100000, y = -100000;
};

static std::string config_path() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    std::string base = xdg && *xdg ? xdg : std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config";
    mkdir(base.c_str(), 0755);
    base += "/gpu-hud";
    mkdir(base.c_str(), 0755);
    return base + "/config.ini";
}

static void load_config(Config& c) {
    std::ifstream f(config_path());
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto f3 = [&](float* d) { sscanf(v.c_str(), "%f,%f,%f", &d[0], &d[1], &d[2]); };
        if (k == "lang") c.lang = v;
        else if (k == "bg_opacity") c.bg_opacity = std::stof(v);
        else if (k == "content_opacity") c.content_opacity = std::stof(v);
        else if (k == "bg_color") f3(c.bg_color);
        else if (k == "accent") f3(c.accent);
        else if (k == "bg_image") c.bg_image = v;
        else if (k == "img_mode") c.img_mode = std::stoi(v);
        else if (k == "img_scale") c.img_scale = std::stof(v);
        else if (k == "img_opacity") c.img_opacity = std::stof(v);
        else if (k == "on_top") c.on_top = v == "1";
        else if (k == "lock_pos") c.lock_pos = v == "1";
        else if (k == "show_procs") c.show_procs = v == "1";
        else if (k == "show_graph") c.show_graph = v == "1";
        else if (k == "sort_by") c.sort_by = std::stoi(v) ? 1 : 0;
        else if (k == "refresh_ms") c.refresh_ms = std::clamp(std::stoi(v), 200, 10000);
        else if (k == "font_size") c.font_size = std::clamp(std::stof(v), 10.0f, 32.0f);
        else if (k == "width") c.width = std::clamp(std::stoi(v), 240, 2000);
        else if (k == "x") c.x = std::stoi(v);
        else if (k == "y") c.y = std::stoi(v);
    }
}

static void save_config(const Config& c) {
    std::ofstream f(config_path());
    f << "lang=" << c.lang << "\n"
      << "bg_opacity=" << c.bg_opacity << "\ncontent_opacity=" << c.content_opacity << "\n"
      << "bg_color=" << c.bg_color[0] << "," << c.bg_color[1] << "," << c.bg_color[2] << "\n"
      << "accent=" << c.accent[0] << "," << c.accent[1] << "," << c.accent[2] << "\n"
      << "bg_image=" << c.bg_image << "\nimg_mode=" << c.img_mode << "\nimg_scale=" << c.img_scale
      << "\nimg_opacity=" << c.img_opacity << "\non_top=" << c.on_top << "\nlock_pos=" << c.lock_pos
      << "\nshow_procs=" << c.show_procs << "\nshow_graph=" << c.show_graph << "\nsort_by=" << c.sort_by << "\nrefresh_ms=" << c.refresh_ms
      << "\nfont_size=" << c.font_size << "\nwidth=" << c.width << "\nx=" << c.x << "\ny=" << c.y << "\n";
}

// ---------------------------------------------------------------------------
// Fonts: a Latin/Cyrillic base font plus the CJK face that matches the UI language.
static std::string shell(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[1024];
        while (fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

static bool file_exists(const std::string& p) {
    struct stat st{};
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

struct FontFace {
    std::string file;
    int index = 0;
};

static FontFace fc_match(const char* pattern) {
    FontFace f;
    std::string r = shell(std::string("fc-match -f '%{file}|%{index}' '") + pattern + "' 2>/dev/null");
    size_t bar = r.rfind('|');
    if (bar != std::string::npos) {
        f.file = r.substr(0, bar);
        f.index = atoi(r.c_str() + bar + 1);
    }
    if (!file_exists(f.file)) f.file.clear();
    // stb_truetype cannot read variable/woff fonts reliably; keep ttf/otf/ttc only.
    auto ends = [&](const char* e) { return f.file.size() > 4 && strcasecmp(f.file.c_str() + f.file.size() - 4, e) == 0; };
    if (!f.file.empty() && !ends(".ttf") && !ends(".otf") && !ends(".ttc")) f.file.clear();
    return f;
}

static void build_fonts(Lang lang) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    FontFace base;
    for (const char* p : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                          "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"})
        if (file_exists(p)) { base.file = p; break; }
    if (base.file.empty()) base = fc_match("sans-serif:lang=en");

    const char* cjk_pat = lang == L_ZH_TW ? "sans-serif:lang=zh-tw"
                        : lang == L_JA    ? "sans-serif:lang=ja"
                        : lang == L_KO    ? "sans-serif:lang=ko"
                                          : "sans-serif:lang=zh-cn";
    FontFace cjk = fc_match(cjk_pat);
    if (cjk.file == base.file) cjk.file.clear();

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    bool have_base = false;
    if (!base.file.empty()) {
        cfg.FontNo = base.index;
        have_base = io.Fonts->AddFontFromFileTTF(base.file.c_str(), 0.0f, &cfg) != nullptr;
    }
    if (!have_base) io.Fonts->AddFontDefault();
    if (!cjk.file.empty()) {
        ImFontConfig m;
        m.MergeMode = true;
        m.FontNo = cjk.index;
        io.Fonts->AddFontFromFileTTF(cjk.file.c_str(), 0.0f, &m);
    }
    // A symbols fallback for anything else (Thai, Arabic, emoji-less symbols…).
    FontFace any = fc_match("sans-serif:charset=2699");
    if (!any.file.empty() && any.file != base.file && any.file != cjk.file) {
        ImFontConfig m;
        m.MergeMode = true;
        m.FontNo = any.index;
        io.Fonts->AddFontFromFileTTF(any.file.c_str(), 0.0f, &m);
    }
}

// ---------------------------------------------------------------------------
// Background image
struct BgImage {
    GLuint tex = 0;
    int w = 0, h = 0;
    std::string loaded_path;
    bool failed = false;

    void unload() {
        if (tex) glDeleteTextures(1, &tex);
        tex = 0;
        w = h = 0;
        loaded_path.clear();
    }
    void load(const std::string& path) {
        unload();
        failed = false;
        if (path.empty()) return;
        int n = 0;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!px) { failed = true; w = h = 0; return; }
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        stbi_image_free(px);
        loaded_path = path;
    }
};

// ---------------------------------------------------------------------------
struct App {
    GLFWwindow* win = nullptr;
    Config cfg;
    Metrics metrics;
    Snapshot snap;
    BgImage bg;
    SysInfo sys;
    std::vector<GpuStatic> gpu_static;
    std::string gl_renderer;

    std::map<int, std::vector<float>> util_hist, mem_hist;  // per GPU index
    uint64_t last_seq = ~0ULL;

    bool show_settings = false;
    bool want_font_rebuild = false;
    bool cfg_dirty = false;
    double cfg_dirty_at = 0;
    double toast_until = 0;
    std::string toast;

    // Window drag / resize
    bool dragging = false, resizing = false;
    double drag_cx = 0, drag_cy = 0;
    int resize_start_w = 0;
    double resize_start_x = 0;

    // Async file dialog
    std::mutex dlg_mu;
    std::atomic<bool> dlg_running{false};
    std::string dlg_result;
    std::string pending_drop;
    char path_buf[1024] = {0};

    void mark_dirty() {
        cfg_dirty = true;
        cfg_dirty_at = glfwGetTime();
    }
};

static App* g_app = nullptr;
static volatile sig_atomic_t g_quit = 0;

static ImU32 accent_col(const App& a, float alpha = 1.0f) {
    return ImGui::GetColorU32(ImVec4(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], alpha));
}

static ImU32 load_col(const App& a, float frac) {
    if (frac >= 0.90f) return ImGui::GetColorU32(ImVec4(0.93f, 0.30f, 0.28f, 1));
    if (frac >= 0.70f) return ImGui::GetColorU32(ImVec4(0.95f, 0.68f, 0.22f, 1));
    return accent_col(a);
}

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// Label column + rounded bar with overlay text.
static void meter(App& a, const char* label, float label_w, float frac, const std::string& overlay) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_w);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() * 0.92f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = h * 0.35f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)), r);
    if (frac > 0.0f) dl->AddRectFilled(p, ImVec2(p.x + std::max(w * frac, r * 2), p.y + h), load_col(a, frac), r);
    ImVec2 ts = ImGui::CalcTextSize(overlay.c_str());
    ImVec2 tp(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f);
    dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 160), overlay.c_str());
    dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), overlay.c_str());
    ImGui::Dummy(ImVec2(w, h));
}

static void history_graph(App& a, const std::vector<float>& util, const std::vector<float>& mem) {
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFontSize() * 2.4f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.05f)), 4);
    auto plot = [&](const std::vector<float>& v, ImU32 col, bool fill) {
        if (v.size() < 2) return;
        const size_t N = 120;
        std::vector<ImVec2> pts;
        size_t start = v.size() > N ? v.size() - N : 0;
        for (size_t i = start; i < v.size(); i++) {
            float x = p.x + w * float(i - start + (N - std::min(N, v.size()))) / float(N - 1);
            pts.push_back(ImVec2(x, p.y + h - 1 - (h - 2) * std::clamp(v[i], 0.0f, 1.0f)));
        }
        if (fill)
            for (size_t i = 1; i < pts.size(); i++)
                dl->AddQuadFilled(pts[i - 1], pts[i], ImVec2(pts[i].x, p.y + h), ImVec2(pts[i - 1].x, p.y + h),
                                  (col & 0x00FFFFFF) | (60u << 24));
        dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, 1.5f);
    };
    plot(util, accent_col(a), true);
    plot(mem, ImGui::GetColorU32(ImVec4(0.45f, 0.62f, 0.98f, 1)), false);
    ImGui::Dummy(ImVec2(w, h));
}

// ---------------------------------------------------------------------------
static std::string build_report(App& a) {
    a.sys = collect_sysinfo();  // fresh, in current language
    const Snapshot& s = a.snap;
    std::ostringstream o;
    time_t now = time(nullptr);
    char ts[64];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
    o << "===== " << tr(S_REPORT_TITLE) << " — " << ts << " =====\n\n";

    o << "## " << tr(S_LIVE) << "\n";
    for (size_t i = 0; i < s.gpus.size(); i++) {
        const GpuStats& g = s.gpus[i];
        o << tr(S_GPU) << i << " " << g.name << "\n";
        if (g.util >= 0) o << "  " << tr(S_GPU) << ": " << g.util << "%\n";
        o << "  " << (g.unified ? tr(S_UNIFIED) : tr(S_GPU_MEM)) << ": " << fmt_bytes(g.mem_used) << " / "
          << fmt_bytes(g.mem_total) << "\n";
        if (g.temp_c >= 0) o << "  " << tr(S_TEMP) << ": " << g.temp_c << " °C\n";
        if (g.power_w >= 0) {
            o << "  " << tr(S_POWER) << ": " << fmt("%.1f W", g.power_w);
            if (g.power_limit_w > 0) o << fmt(" / %.0f W", g.power_limit_w);
            o << "\n";
        }
        if (g.clock_mhz >= 0) o << "  " << tr(S_CLOCK) << ": " << g.clock_mhz << " MHz\n";
        if (g.fan_pct >= 0) o << "  " << tr(S_FAN) << ": " << g.fan_pct << "%\n";
        if (g.enc_util >= 0) o << "  " << tr(S_ENC_DEC) << ": " << g.enc_util << "% / " << g.dec_util << "%\n";
    }
    if (s.gpus.empty()) o << tr(S_NO_GPU) << "\n";
    o << tr(S_CPU) << ": " << fmt("%.0f%%", s.cpu_util) << "\n";
    o << tr(S_RAM) << ": " << fmt_bytes(s.ram_used) << " / " << fmt_bytes(s.ram_total) << "\n";
    if (s.swap_total) o << tr(S_SWAP) << ": " << fmt_bytes(s.swap_used) << " / " << fmt_bytes(s.swap_total) << "\n";
    o << "\n## " << tr(S_TOP_PROCS) << "\n";
    if (s.procs.empty()) o << tr(S_NO_PROC) << "\n";
    for (const GpuProcess& p : s.procs) {
        o << fmt("  [%s%d] %s=%u  %s  %s=%s  %s=%d%%  ", tr(S_GPU), p.gpu, tr(S_PID), p.pid, p.user.c_str(), tr(S_MEM),
                 p.mem_bytes ? fmt_bytes(p.mem_bytes).c_str() : "N/A", tr(S_SM), std::max(p.sm_util, 0))
          << (p.cmdline.empty() ? p.name : p.cmdline) << "\n";
    }

    auto section = [&](const char* title, const std::vector<KV>& kv) {
        if (kv.empty()) return;
        o << "\n## " << title << "\n";
        for (auto& e : kv) o << "  " << e.key << ": " << e.value << "\n";
    };
    section(tr(S_SYSTEM), a.sys.system);
    section(tr(S_CPU), a.sys.cpu);
    section(tr(S_MEMORY), a.sys.memory);
    section(tr(S_MOTHERBOARD), a.sys.board);

    o << "\n## " << tr(S_GPU) << "\n";
    for (size_t i = 0; i < a.gpu_static.size(); i++) {
        const GpuStatic& g = a.gpu_static[i];
        o << "  " << tr(S_GPU) << i << ": " << g.name << "\n";
        if (!g.driver.empty()) o << "    " << tr(S_DRIVER) << ": " << g.driver << "\n";
        if (!g.cuda.empty()) o << "    " << tr(S_CUDA) << ": " << g.cuda << "\n";
        o << "    " << (g.unified ? tr(S_UNIFIED) : tr(S_GPU_MEM)) << ": " << fmt_bytes(g.mem_total) << "\n";
        if (!g.pci.empty()) o << "    " << tr(S_PCI) << ": " << g.pci << "\n";
        if (!g.uuid.empty()) o << "    " << tr(S_UUID) << ": " << g.uuid << "\n";
        if (g.max_clock_mhz > 0) o << "    " << tr(S_MAX_CLOCK) << ": " << g.max_clock_mhz << " MHz\n";
        if (g.power_limit_w > 0) o << "    " << tr(S_POWER_LIMIT) << ": " << fmt("%.0f W", g.power_limit_w) << "\n";
    }
    if (!a.gl_renderer.empty()) o << "  " << tr(S_RENDERER) << ": " << a.gl_renderer << "\n";

    int nmon = 0;
    GLFWmonitor** mons = glfwGetMonitors(&nmon);
    if (nmon > 0) {
        o << "\n## " << tr(S_DISPLAYS) << "\n";
        for (int i = 0; i < nmon; i++) {
            const GLFWvidmode* m = glfwGetVideoMode(mons[i]);
            int mw = 0, mh = 0;
            glfwGetMonitorPhysicalSize(mons[i], &mw, &mh);
            o << "  " << glfwGetMonitorName(mons[i]);
            if (m) o << fmt("  %dx%d @ %d Hz", m->width, m->height, m->refreshRate);
            if (mw > 0) o << fmt("  (%d×%d mm)", mw, mh);
            o << "\n";
        }
    }
    if (!a.sys.storage.empty()) {
        o << "\n## " << tr(S_STORAGE) << "\n";
        for (auto& l : a.sys.storage) o << "  " << l << "\n";
    }
    return o.str();
}

static void do_copy(App& a) {
    std::string r = build_report(a);
    glfwSetClipboardString(a.win, r.c_str());
    a.toast = tr(S_COPIED);
    a.toast_until = glfwGetTime() + 1.8;
}

static void start_browse(App& a) {
    if (a.dlg_running.exchange(true)) return;
    std::thread([&a] {
        std::string r = shell(
            "zenity --file-selection --title='Background image' "
            "--file-filter='Images | *.png *.jpg *.jpeg *.bmp *.gif *.tga *.PNG *.JPG *.JPEG' 2>/dev/null || "
            "kdialog --getopenfilename ~ 'image/png image/jpeg image/bmp image/gif' 2>/dev/null");
        {
            std::lock_guard<std::mutex> lk(a.dlg_mu);
            a.dlg_result = r;
        }
        a.dlg_running = false;
        glfwPostEmptyEvent();
    }).detach();
}

static void set_image(App& a, const std::string& path) {
    a.cfg.bg_image = path;
    snprintf(a.path_buf, sizeof a.path_buf, "%s", path.c_str());
    a.bg.load(path);
    a.mark_dirty();
}

// ---------------------------------------------------------------------------
static void draw_background(App& a, ImVec2 size) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float rnd = 10.0f;
    ImVec2 p0(0, 0), p1 = size;
    dl->AddRectFilled(p0, p1,
                      ImGui::ColorConvertFloat4ToU32(ImVec4(a.cfg.bg_color[0], a.cfg.bg_color[1], a.cfg.bg_color[2], a.cfg.bg_opacity)),
                      rnd);
    if (a.bg.tex && a.bg.w > 0) {
        ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, a.cfg.img_opacity * std::max(a.cfg.bg_opacity, 0.15f)));
        float iw = a.bg.w * a.cfg.img_scale, ih = a.bg.h * a.cfg.img_scale;
        ImTextureRef tex((ImTextureID)(intptr_t)a.bg.tex);
        // The GL3 backend binds its own sampler (clamp-to-edge); use the texture's GL_REPEAT instead.
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        if (pio.DrawCallback_SetSamplerFromTex) dl->AddCallback(pio.DrawCallback_SetSamplerFromTex, nullptr);
        switch (a.cfg.img_mode) {
            case IMG_TILE:
                dl->AddImageRounded(tex, p0, p1, ImVec2(0, 0), ImVec2(size.x / iw, size.y / ih), tint, rnd);
                break;
            case IMG_STRETCH:
                dl->AddImageRounded(tex, p0, p1, ImVec2(0, 0), ImVec2(1, 1), tint, rnd);
                break;
            case IMG_FILL: {  // cover, cropped to keep aspect
                float ra = size.x / size.y, ri = (float)a.bg.w / a.bg.h;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (ri > ra) { float k = ra / ri; uv0.x = (1 - k) / 2; uv1.x = 1 - uv0.x; }
                else { float k = ri / ra; uv0.y = (1 - k) / 2; uv1.y = 1 - uv0.y; }
                dl->AddImageRounded(tex, p0, p1, uv0, uv1, tint, rnd);
                break;
            }
            case IMG_CENTER: {
                ImVec2 c((size.x - iw) / 2, (size.y - ih) / 2);
                dl->PushClipRect(p0, p1, true);
                dl->AddImage(tex, c, ImVec2(c.x + iw, c.y + ih), ImVec2(0, 0), ImVec2(1, 1), tint);
                dl->PopClipRect();
                break;
            }
        }
        if (pio.DrawCallback_ResetRenderState) dl->AddCallback(pio.DrawCallback_ResetRenderState, nullptr);
    }
    dl->AddRect(p0, ImVec2(p1.x - 0.5f, p1.y - 0.5f), accent_col(a, 0.35f), rnd, 0, 1.0f);
}

static bool small_button_right(const char* label, float& right_x) {
    float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
    right_x -= w;
    ImGui::SameLine(right_x);
    right_x -= ImGui::GetStyle().ItemSpacing.x;
    return ImGui::SmallButton(label);
}

static void draw_settings(App& a) {
    ImGui::Separator();
    ImGui::PushItemWidth(-FLT_MIN);
    float lw = ImGui::GetContentRegionAvail().x * 0.42f;
    auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    ImGui::TextColored(ImVec4(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], 1), "%s", tr(S_APPEARANCE));
    row(tr(S_LANGUAGE));
    Lang cur = get_lang();
    if (ImGui::BeginCombo("##lang", kLangNames[cur])) {
        for (int l = 0; l < L_COUNT; l++)
            if (ImGui::Selectable(kLangNames[l], l == cur)) {
                set_lang((Lang)l);
                a.cfg.lang = kLangCodes[l];
                a.want_font_rebuild = true;
                a.mark_dirty();
            }
        ImGui::EndCombo();
    }
    row(tr(S_BG_OPACITY));
    if (ImGui::SliderFloat("##bgop", &a.cfg.bg_opacity, 0.0f, 1.0f, "%.2f")) a.mark_dirty();
    row(tr(S_CONTENT_OPACITY));
    if (ImGui::SliderFloat("##ctop", &a.cfg.content_opacity, 0.2f, 1.0f, "%.2f")) a.mark_dirty();
    row(tr(S_BG_COLOR));
    if (ImGui::ColorEdit3("##bgc", a.cfg.bg_color, ImGuiColorEditFlags_NoInputs)) a.mark_dirty();
    row(tr(S_ACCENT));
    if (ImGui::ColorEdit3("##acc", a.cfg.accent, ImGuiColorEditFlags_NoInputs)) a.mark_dirty();
    row(tr(S_FONT_SIZE));
    if (ImGui::SliderFloat("##fs", &a.cfg.font_size, 10.0f, 32.0f, "%.0f px")) a.mark_dirty();
    row(tr(S_WIDTH));
    if (ImGui::SliderInt("##w", &a.cfg.width, 240, 1200, "%d px")) a.mark_dirty();

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], 1), "%s", tr(S_BG_IMAGE));
    {
        std::lock_guard<std::mutex> lk(a.dlg_mu);
        if (!a.dlg_result.empty()) { set_image(a, a.dlg_result); a.dlg_result.clear(); }
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##img", "/path/to/image.png", a.path_buf, sizeof a.path_buf,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
        set_image(a, a.path_buf);
    ImGui::BeginDisabled(a.dlg_running);
    if (ImGui::Button(tr(S_BROWSE))) start_browse(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr(S_CLEAR))) set_image(a, "");
    if (a.bg.failed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1), "%s", tr(S_IMG_FAILED));
    }
    const char* modes[] = {tr(S_TILE), tr(S_STRETCH), tr(S_FILL), tr(S_CENTER)};
    row(tr(S_IMG_MODE));
    if (ImGui::Combo("##mode", &a.cfg.img_mode, modes, 4)) a.mark_dirty();
    row(tr(S_IMG_SCALE));
    if (ImGui::SliderFloat("##isc", &a.cfg.img_scale, 0.05f, 4.0f, "%.2f×", ImGuiSliderFlags_Logarithmic)) a.mark_dirty();
    row(tr(S_IMG_OPACITY));
    if (ImGui::SliderFloat("##iop", &a.cfg.img_opacity, 0.0f, 1.0f, "%.2f")) a.mark_dirty();
    ImGui::TextDisabled("%s", tr(S_DROP_HINT));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], 1), "%s", tr(S_BEHAVIOR));
    if (ImGui::Checkbox(tr(S_ALWAYS_ON_TOP), &a.cfg.on_top)) {
        glfwSetWindowAttrib(a.win, GLFW_FLOATING, a.cfg.on_top ? GLFW_TRUE : GLFW_FALSE);
        a.mark_dirty();
    }
    if (ImGui::Checkbox(tr(S_LOCK_POS), &a.cfg.lock_pos)) a.mark_dirty();
    if (ImGui::Checkbox(tr(S_SHOW_PROCS), &a.cfg.show_procs)) a.mark_dirty();
    if (ImGui::Checkbox(tr(S_SHOW_GRAPH), &a.cfg.show_graph)) a.mark_dirty();
    row(tr(S_REFRESH));
    if (ImGui::SliderInt("##rf", &a.cfg.refresh_ms, 200, 5000, "%d ms")) {
        a.metrics.set_interval(a.cfg.refresh_ms);
        a.mark_dirty();
    }
    ImGui::Spacing();
    if (ImGui::Button(tr(S_CLOSE))) a.show_settings = false;
    ImGui::SameLine();
    if (ImGui::Button(tr(S_QUIT))) glfwSetWindowShouldClose(a.win, GLFW_TRUE);
    ImGui::PopItemWidth();
}

static float draw_ui(App& a) {
    ImGuiStyle& st = ImGui::GetStyle();
    const Snapshot& s = a.snap;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    // Fixed width, oversized height: the OS window is later fitted to the measured content height.
    ImGui::SetNextWindowSize(ImVec2((float)a.cfg.width, 8000.0f));
    ImGui::Begin("##hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Header
    ImGui::TextColored(ImVec4(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], 1), "● GPU HUD");
    float rx = ImGui::GetWindowContentRegionMax().x;
    if (small_button_right("✕", rx)) glfwSetWindowShouldClose(a.win, GLFW_TRUE);
    if (small_button_right("⚙", rx)) a.show_settings = !a.show_settings;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(S_SETTINGS));
    if (small_button_right(glfwGetTime() < a.toast_until ? a.toast.c_str() : tr(S_COPY_ALL), rx)) do_copy(a);

    // Label column width = widest label in use.
    float lw = 0;
    for (Str k : {S_GPU, S_GPU_MEM, S_RAM, S_CPU, S_SWAP})
        lw = std::max(lw, ImGui::CalcTextSize(tr(k)).x);
    for (auto& g : s.gpus)
        if (g.unified) lw = std::max(lw, ImGui::CalcTextSize(tr(S_UNIFIED)).x);
    lw += st.ItemSpacing.x * 2 + st.WindowPadding.x;

    for (size_t i = 0; i < s.gpus.size(); i++) {
        const GpuStats& g = s.gpus[i];
        ImGui::Spacing();
        std::string info;
        if (g.temp_c >= 0) info += fmt("%d°C  ", g.temp_c);
        if (g.power_w >= 0) info += fmt("%.0fW  ", g.power_w);
        if (g.clock_mhz >= 0) info += fmt("%dMHz", g.clock_mhz);
        if (g.fan_pct >= 0) info += fmt("  %s %d%%", tr(S_FAN), g.fan_pct);
        ImGui::TextUnformatted(g.name.c_str());
        float iw = ImGui::CalcTextSize(info.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetWindowContentRegionMax().x - iw, ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 8));
        ImGui::TextDisabled("%s", info.c_str());

        meter(a, tr(S_GPU), lw, g.util / 100.0f, g.util >= 0 ? fmt("%d%%", g.util) : "N/A");
        float mf = g.mem_total ? float(double(g.mem_used) / double(g.mem_total)) : 0.0f;
        meter(a, g.unified ? tr(S_UNIFIED) : tr(S_GPU_MEM), lw, mf,
              fmt("%s / %s", fmt_bytes(g.mem_used).c_str(), fmt_bytes(g.mem_total).c_str()));
        if (a.cfg.show_graph) history_graph(a, a.util_hist[(int)i], a.mem_hist[(int)i]);
    }
    if (s.gpus.empty()) ImGui::TextDisabled("%s", tr(S_NO_GPU));

    ImGui::Spacing();
    meter(a, tr(S_RAM), lw, s.ram_total ? float(double(s.ram_used) / s.ram_total) : 0,
          fmt("%s / %s", fmt_bytes(s.ram_used).c_str(), fmt_bytes(s.ram_total).c_str()));
    meter(a, tr(S_CPU), lw, float(s.cpu_util / 100.0), fmt("%.0f%%", s.cpu_util));
    if (s.swap_total > 0 && s.swap_used > 0)
        meter(a, tr(S_SWAP), lw, float(double(s.swap_used) / s.swap_total),
              fmt("%s / %s", fmt_bytes(s.swap_used).c_str(), fmt_bytes(s.swap_total).c_str()));

    // Top process
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr(S_TOP_PROC));
    if (s.procs.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(tr(S_NO_PROC));
    } else {
        const GpuProcess& p = s.procs[0];
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], 1), "%s", p.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s: %s  %s: %u", p.cmdline.c_str(), tr(S_USER), p.user.c_str(), tr(S_PID), p.pid);
        ImGui::SameLine();
        ImGui::TextDisabled("%u · %s · %s %d%%", p.pid, p.mem_bytes ? fmt_bytes(p.mem_bytes).c_str() : "N/A", tr(S_SM),
                            std::max(p.sm_util, 0));
    }

    if (a.cfg.show_procs && s.procs.size() > 1) {
        ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody |
                             ImGuiTableFlags_Sortable;
        if (ImGui::BeginTable("##procs", 4, tf)) {
            const ImGuiTableColumnFlags ns = ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch;
            const ImGuiTableColumnFlags sd = ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthStretch;
            ImGui::TableSetupColumn(tr(S_PROCESS), ns, 3.0f);
            ImGui::TableSetupColumn(tr(S_PID), ns, 1.2f);
            ImGui::TableSetupColumn(tr(S_MEM), sd | (a.cfg.sort_by == 0 ? ImGuiTableColumnFlags_DefaultSort : 0), 1.6f, 2);
            ImGui::TableSetupColumn(tr(S_SM), sd | (a.cfg.sort_by == 1 ? ImGuiTableColumnFlags_DefaultSort : 0), 0.9f, 3);
            if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs())
                if (ss->SpecsDirty && ss->SpecsCount > 0) {
                    int want = ss->Specs[0].ColumnUserID == 3 ? 1 : 0;
                    if (want != a.cfg.sort_by) { a.cfg.sort_by = want; a.mark_dirty(); }
                    ss->SpecsDirty = false;
                }
            ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(1, 1, 1, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1, 1, 1, 0.05f));
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < std::min<size_t>(s.procs.size(), 6); i++) {
                const GpuProcess& p = s.procs[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.name.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s: %s", p.cmdline.c_str(), tr(S_USER), p.user.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", p.pid);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.mem_bytes ? fmt_bytes(p.mem_bytes).c_str() : "—");
                ImGui::TableNextColumn();
                ImGui::Text("%d%%", std::max(p.sm_util, 0));
            }
            ImGui::PopStyleColor(2);
            ImGui::EndTable();
        }
    }

    if (a.show_settings) draw_settings(a);

    float h = ImGui::GetCursorPosY() - st.ItemSpacing.y + st.WindowPadding.y;
    ImGui::End();
    return h;
}

// ---------------------------------------------------------------------------
static void handle_window_drag(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    double cx, cy;
    glfwGetCursorPos(a.win, &cx, &cy);
    int ww, wh;
    glfwGetWindowSize(a.win, &ww, &wh);
    const float grip = 14.0f;
    bool in_grip = cx > ww - grip && cy > wh - grip;

    if (in_grip || a.resizing) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
        if (in_grip) {
            a.resizing = true;
            a.resize_start_w = a.cfg.width;
            int wx, wy;
            glfwGetWindowPos(a.win, &wx, &wy);
            a.resize_start_x = wx + cx;
        } else if (!a.cfg.lock_pos) {
            a.dragging = true;
            a.drag_cx = cx;
            a.drag_cy = cy;
        }
    }
    if (ImGui::IsMouseClicked(1) && !ImGui::IsAnyItemHovered()) a.show_settings = !a.show_settings;
    if (!io.MouseDown[0]) {
        if (a.dragging || a.resizing) a.mark_dirty();
        a.dragging = a.resizing = false;
    }
    if (a.dragging) {
        int wx, wy;
        glfwGetWindowPos(a.win, &wx, &wy);
        int nx = wx + int(cx - a.drag_cx), ny = wy + int(cy - a.drag_cy);
        if (nx != wx || ny != wy) {
            glfwSetWindowPos(a.win, nx, ny);
            a.cfg.x = nx;
            a.cfg.y = ny;
        }
    }
    if (a.resizing) {
        int wx, wy;
        glfwGetWindowPos(a.win, &wx, &wy);
        a.cfg.width = std::clamp(a.resize_start_w + int(wx + cx - a.resize_start_x), 240, 2000);
    }
}

static void draw_grip(App& a, ImVec2 size) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 c = accent_col(a, 0.55f);
    for (int i = 1; i <= 3; i++) {
        float o = 4.0f * i;
        dl->AddLine(ImVec2(size.x - o - 2, size.y - 3), ImVec2(size.x - 3, size.y - o - 2), c, 1.2f);
    }
}

static void sort_procs(App& a) {
    if (a.cfg.sort_by == 1)
        std::stable_sort(a.snap.procs.begin(), a.snap.procs.end(),
                         [](const GpuProcess& x, const GpuProcess& y) { return x.sm_util > y.sm_util; });
}

static void update_history(App& a) {
    if (a.snap.seq == a.last_seq) return;
    a.last_seq = a.snap.seq;
    for (size_t i = 0; i < a.snap.gpus.size(); i++) {
        const GpuStats& g = a.snap.gpus[i];
        auto& u = a.util_hist[(int)i];
        auto& m = a.mem_hist[(int)i];
        u.push_back(std::max(g.util, 0) / 100.0f);
        m.push_back(g.mem_total ? float(double(g.mem_used) / g.mem_total) : 0.0f);
        if (u.size() > 240) u.erase(u.begin(), u.begin() + 120);
        if (m.size() > 240) m.erase(m.begin(), m.begin() + 120);
    }
}

// Read the back buffer (premultiplied alpha) and write a straight-alpha PNG.
static void save_screenshot(const std::string& path, int w, int h) {
    std::vector<unsigned char> px((size_t)w * h * 4), out(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* s = &px[((size_t)(h - 1 - y) * w + x) * 4];
            unsigned char* d = &out[((size_t)y * w + x) * 4];
            int a = s[3];
            for (int c = 0; c < 3; c++) d[c] = a ? (unsigned char)std::min(255, s[c] * 255 / a) : 0;
            d[3] = (unsigned char)a;
        }
    if (stbi_write_png(path.c_str(), w, h, 4, out.data(), w * 4)) fprintf(stderr, "saved %s (%dx%d)\n", path.c_str(), w, h);
    else fprintf(stderr, "failed to write %s\n", path.c_str());
}

static void apply_style(App& a) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 10.0f;
    st.FrameRounding = 5.0f;
    st.PopupRounding = 6.0f;
    st.GrabRounding = 4.0f;
    st.WindowPadding = ImVec2(12, 10);
    st.ItemSpacing = ImVec2(8, 5);
    st.WindowBorderSize = 0;
    ImVec4 acc(a.cfg.accent[0], a.cfg.accent[1], a.cfg.accent[2], 1);
    st.Colors[ImGuiCol_CheckMark] = acc;
    st.Colors[ImGuiCol_SliderGrab] = acc;
    st.Colors[ImGuiCol_SliderGrabActive] = acc;
    st.Colors[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_ButtonHovered] = ImVec4(acc.x, acc.y, acc.z, 0.45f);
    st.Colors[ImGuiCol_ButtonActive] = ImVec4(acc.x, acc.y, acc.z, 0.70f);
    st.Colors[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.08f);
    st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.14f);
    st.Colors[ImGuiCol_FrameBgActive] = ImVec4(acc.x, acc.y, acc.z, 0.35f);
    st.Colors[ImGuiCol_Header] = ImVec4(acc.x, acc.y, acc.z, 0.30f);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(acc.x, acc.y, acc.z, 0.45f);
    st.Colors[ImGuiCol_TableHeaderBg] = ImVec4(1, 1, 1, 0.06f);
    st.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.97f);
    st.Colors[ImGuiCol_TextDisabled] = ImVec4(0.74f, 0.76f, 0.80f, 1.0f);  // stays legible over background images
}

int main(int argc, char** argv) {
    App app;
    g_app = &app;
    bool reset = false, report = false;
    std::string shot_path;
    double shot_delay = 6.0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--reset")) reset = true;
        else if (!strcmp(argv[i], "--report")) report = true;
        else if (!strcmp(argv[i], "--settings")) app.show_settings = true;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot_path = argv[++i];
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc) shot_delay = atof(argv[++i]);
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("Usage: gpu-hud [--reset] [--report]\n  --reset   ignore saved settings\n"
                   "  --report  print the full system report (same as \"Copy all\") and exit\n"
                   "  --settings  start with the settings panel open\n"
                   "  --screenshot FILE  save the window (with alpha) to a PNG after --delay SEC (default 6) and exit\n"
                   "Drag to move, drag bottom-right corner to resize, right-click for settings.\n");
            return 0;
        }
    }
    if (!reset) load_config(app.cfg);

    if (!app.cfg.lang.empty()) set_lang(lang_from_code(app.cfg.lang.c_str()));
    else {
        const char* l = getenv("LC_ALL");
        if (!l || !*l) l = getenv("LC_MESSAGES");
        if (!l || !*l) l = getenv("LANG");
        set_lang(lang_from_code(l));
    }

    // X11 first: Wayland has no always-on-top or self-positioning, so run through XWayland there.
#ifdef GLFW_PLATFORM
    if (glfwPlatformSupported(GLFW_PLATFORM_X11)) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    glfwSetErrorCallback([](int c, const char* d) { fprintf(stderr, "GLFW error %d: %s\n", c, d); });
    if (!glfwInit()) return 1;

    if (report) {
        app.gpu_static = app.metrics.gpu_static();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // CPU% needs two samples
        app.metrics.start(100000, nullptr);
        app.snap = app.metrics.snapshot();
        sort_procs(app);
        fputs(build_report(app).c_str(), stdout);
        glfwTerminate();
        return 0;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, app.cfg.on_top ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "gpu-hud");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "gpu-hud");

    app.win = glfwCreateWindow(app.cfg.width, 300, "GPU HUD", nullptr, nullptr);
    if (!app.win) {
        glfwTerminate();
        return 1;
    }
    if (app.cfg.x > -100000) glfwSetWindowPos(app.win, app.cfg.x, app.cfg.y);
    else if (GLFWmonitor* m = glfwGetPrimaryMonitor()) {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(m, &mx, &my, &mw, &mh);
        app.cfg.x = mx + mw - app.cfg.width - 24;
        app.cfg.y = my + 24;
        glfwSetWindowPos(app.win, app.cfg.x, app.cfg.y);
    }
    glfwMakeContextCurrent(app.win);
    glfwSwapInterval(1);
    glfwSetDropCallback(app.win, [](GLFWwindow*, int n, const char** paths) {
        if (n > 0) g_app->pending_drop = paths[0];
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    apply_style(app);
    build_fonts(get_lang());
    ImGui_ImplGlfw_InitForOpenGL(app.win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    if (const GLubyte* r = glGetString(GL_RENDERER)) app.gl_renderer = (const char*)r;
    app.gpu_static = app.metrics.gpu_static();
    snprintf(app.path_buf, sizeof app.path_buf, "%s", app.cfg.bg_image.c_str());
    app.bg.load(app.cfg.bg_image);
    app.metrics.start(app.cfg.refresh_ms, [] { glfwPostEmptyEvent(); });

    float accent_prev[3] = {-1, -1, -1};
    int frame = 0;
    signal(SIGINT, [](int) { g_quit = 1; });
    signal(SIGTERM, [](int) { g_quit = 1; });
    while (!glfwWindowShouldClose(app.win) && !g_quit) {
        bool busy = app.dragging || app.resizing || glfwGetTime() < app.toast_until;
        glfwWaitEventsTimeout(busy ? 0.016 : 0.5);

        if (!app.pending_drop.empty()) {
            set_image(app, app.pending_drop);
            app.pending_drop.clear();
        }
        if (app.want_font_rebuild) {
            build_fonts(get_lang());
            app.want_font_rebuild = false;
        }
        if (memcmp(accent_prev, app.cfg.accent, sizeof accent_prev) != 0) {
            memcpy(accent_prev, app.cfg.accent, sizeof accent_prev);
            apply_style(app);
        }
        ImGuiStyle& st = ImGui::GetStyle();
        st.FontSizeBase = app.cfg.font_size;
        st.Alpha = app.cfg.content_opacity;

        app.snap = app.metrics.snapshot();
        sort_procs(app);
        update_history(app);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        handle_window_drag(app);
        float content_h = draw_ui(app);

        int ww, wh;
        glfwGetWindowSize(app.win, &ww, &wh);
        draw_background(app, ImVec2((float)ww, (float)wh));
        draw_grip(app, ImVec2((float)ww, (float)wh));
        ImGui::Render();

        int fw, fh;
        glfwGetFramebufferSize(app.win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!shot_path.empty() && frame > 3 && glfwGetTime() >= shot_delay) {
            save_screenshot(shot_path, fw, fh);
            glfwSetWindowShouldClose(app.win, GLFW_TRUE);
        }
        glfwSwapBuffers(app.win);

        // Fit the OS window to the content (height auto, width from config).
        int want_h = (int)std::ceil(content_h);
        if (want_h > 0 && (want_h != wh || app.cfg.width != ww)) glfwSetWindowSize(app.win, app.cfg.width, want_h);
        if (++frame == 2) {  // show once sized, avoids a flash
            glfwShowWindow(app.win);
            glfwSetWindowPos(app.win, app.cfg.x, app.cfg.y);
        }

        if (app.cfg_dirty && glfwGetTime() - app.cfg_dirty_at > 0.8) {
            save_config(app.cfg);
            app.cfg_dirty = false;
        }
    }
    save_config(app.cfg);

    app.bg.unload();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(app.win);
    glfwTerminate();
    return 0;
}
