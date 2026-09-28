#include "metrics.h"

#include <dirent.h>
#include <dlfcn.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

// ---------------------------------------------------------------------------
// Minimal NVML ABI, loaded at runtime so the binary runs on machines without NVIDIA.
namespace nv {
typedef int ret_t;
typedef void* dev_t;
enum { SUCCESS = 0, NOT_SUPPORTED = 3, NOT_FOUND = 6, INSUFFICIENT_SIZE = 7 };
struct Utilization { unsigned gpu, memory; };
struct Memory { unsigned long long total, free, used; };
struct ProcessInfo { unsigned pid; unsigned long long usedGpuMemory; unsigned gpuInstanceId, computeInstanceId; };
struct ProcUtilSample { unsigned pid; unsigned long long timeStamp; unsigned smUtil, memUtil, encUtil, decUtil; };
struct PciInfo { char busIdLegacy[16]; unsigned domain, bus, device, pciDeviceId, pciSubSystemId; char busId[32]; };

struct Api {
    void* lib = nullptr;
    ret_t (*Init)() = nullptr;
    ret_t (*DeviceGetCount)(unsigned*) = nullptr;
    ret_t (*DeviceGetHandleByIndex)(unsigned, dev_t*) = nullptr;
    ret_t (*DeviceGetName)(dev_t, char*, unsigned) = nullptr;
    ret_t (*DeviceGetUUID)(dev_t, char*, unsigned) = nullptr;
    ret_t (*DeviceGetUtilizationRates)(dev_t, Utilization*) = nullptr;
    ret_t (*DeviceGetMemoryInfo)(dev_t, Memory*) = nullptr;
    ret_t (*DeviceGetTemperature)(dev_t, int, unsigned*) = nullptr;
    ret_t (*DeviceGetPowerUsage)(dev_t, unsigned*) = nullptr;
    ret_t (*DeviceGetEnforcedPowerLimit)(dev_t, unsigned*) = nullptr;
    ret_t (*DeviceGetClockInfo)(dev_t, int, unsigned*) = nullptr;
    ret_t (*DeviceGetMaxClockInfo)(dev_t, int, unsigned*) = nullptr;
    ret_t (*DeviceGetFanSpeed)(dev_t, unsigned*) = nullptr;
    ret_t (*DeviceGetEncoderUtilization)(dev_t, unsigned*, unsigned*) = nullptr;
    ret_t (*DeviceGetDecoderUtilization)(dev_t, unsigned*, unsigned*) = nullptr;
    ret_t (*DeviceGetComputeRunningProcesses)(dev_t, unsigned*, ProcessInfo*) = nullptr;
    ret_t (*DeviceGetGraphicsRunningProcesses)(dev_t, unsigned*, ProcessInfo*) = nullptr;
    ret_t (*DeviceGetProcessUtilization)(dev_t, ProcUtilSample*, unsigned*, unsigned long long) = nullptr;
    ret_t (*DeviceGetPciInfo)(dev_t, PciInfo*) = nullptr;
    ret_t (*SystemGetDriverVersion)(char*, unsigned) = nullptr;
    ret_t (*SystemGetCudaDriverVersion)(int*) = nullptr;

    template <class F> void sym(F& f, const char* a, const char* b = nullptr) {
        void* p = dlsym(lib, a);
        if (!p && b) p = dlsym(lib, b);
        f = reinterpret_cast<F>(p);
    }
    bool load() {
        lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        sym(Init, "nvmlInit_v2", "nvmlInit");
        sym(DeviceGetCount, "nvmlDeviceGetCount_v2", "nvmlDeviceGetCount");
        sym(DeviceGetHandleByIndex, "nvmlDeviceGetHandleByIndex_v2", "nvmlDeviceGetHandleByIndex");
        sym(DeviceGetName, "nvmlDeviceGetName");
        sym(DeviceGetUUID, "nvmlDeviceGetUUID");
        sym(DeviceGetUtilizationRates, "nvmlDeviceGetUtilizationRates");
        sym(DeviceGetMemoryInfo, "nvmlDeviceGetMemoryInfo");
        sym(DeviceGetTemperature, "nvmlDeviceGetTemperature");
        sym(DeviceGetPowerUsage, "nvmlDeviceGetPowerUsage");
        sym(DeviceGetEnforcedPowerLimit, "nvmlDeviceGetEnforcedPowerLimit");
        sym(DeviceGetClockInfo, "nvmlDeviceGetClockInfo");
        sym(DeviceGetMaxClockInfo, "nvmlDeviceGetMaxClockInfo");
        sym(DeviceGetFanSpeed, "nvmlDeviceGetFanSpeed");
        sym(DeviceGetEncoderUtilization, "nvmlDeviceGetEncoderUtilization");
        sym(DeviceGetDecoderUtilization, "nvmlDeviceGetDecoderUtilization");
        sym(DeviceGetComputeRunningProcesses, "nvmlDeviceGetComputeRunningProcesses_v3", "nvmlDeviceGetComputeRunningProcesses_v2");
        sym(DeviceGetGraphicsRunningProcesses, "nvmlDeviceGetGraphicsRunningProcesses_v3", "nvmlDeviceGetGraphicsRunningProcesses_v2");
        sym(DeviceGetProcessUtilization, "nvmlDeviceGetProcessUtilization");
        sym(DeviceGetPciInfo, "nvmlDeviceGetPciInfo_v3", "nvmlDeviceGetPciInfo_v2");
        sym(SystemGetDriverVersion, "nvmlSystemGetDriverVersion");
        sym(SystemGetCudaDriverVersion, "nvmlSystemGetCudaDriverVersion_v2", "nvmlSystemGetCudaDriverVersion");
        if (!Init || !DeviceGetCount || !DeviceGetHandleByIndex || Init() != SUCCESS) {
            dlclose(lib);
            lib = nullptr;
            return false;
        }
        return true;
    }
};
}  // namespace nv

// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\0')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    return s.substr(i);
}

static long long read_ll(const std::string& path, long long def = -1) {
    std::string s = trim(read_file(path));
    if (s.empty()) return def;
    try { return std::stoll(s); } catch (...) { return def; }
}

std::string fmt_bytes(uint64_t b) {
    char buf[64];
    double v = (double)b;
    if (v >= 1024.0 * 1024 * 1024) snprintf(buf, sizeof buf, "%.1f GiB", v / (1024.0 * 1024 * 1024));
    else if (v >= 1024.0 * 1024) snprintf(buf, sizeof buf, "%.0f MiB", v / (1024.0 * 1024));
    else snprintf(buf, sizeof buf, "%.0f KiB", v / 1024.0);
    return buf;
}

static void fill_proc_identity(GpuProcess& p) {
    std::string base = "/proc/" + std::to_string(p.pid);
    p.name = trim(read_file(base + "/comm"));
    std::string cl = read_file(base + "/cmdline");
    for (char& c : cl) if (c == '\0') c = ' ';
    p.cmdline = trim(cl);
    struct stat st{};
    if (stat(base.c_str(), &st) == 0) {
        if (passwd* pw = getpwuid(st.st_uid)) p.user = pw->pw_name;
        else p.user = std::to_string(st.st_uid);
    }
    if (p.name.empty()) p.name = "pid " + std::to_string(p.pid);
}

// ---------------------------------------------------------------------------
struct AmdCard {
    std::string dev;   // /sys/class/drm/cardN/device
    std::string hwmon;
    std::string name;
};

struct Metrics::Impl {
    nv::Api nvml;
    bool have_nvml = false;
    std::vector<nv::dev_t> nv_devs;
    std::vector<unsigned long long> nv_last_ts;
    std::vector<AmdCard> amd;
    unsigned long long cpu_prev_total = 0, cpu_prev_idle = 0;

    void init() {
        have_nvml = nvml.load();
        if (have_nvml) {
            unsigned n = 0;
            nvml.DeviceGetCount(&n);
            for (unsigned i = 0; i < n; i++) {
                nv::dev_t d;
                if (nvml.DeviceGetHandleByIndex(i, &d) == nv::SUCCESS) nv_devs.push_back(d);
            }
            nv_last_ts.assign(nv_devs.size(), 0);
        }
        // AMD GPUs through amdgpu sysfs.
        if (DIR* dir = opendir("/sys/class/drm")) {
            while (dirent* e = readdir(dir)) {
                std::string n = e->d_name;
                if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos) continue;
                std::string dev = "/sys/class/drm/" + n + "/device";
                if (trim(read_file(dev + "/vendor")) != "0x1002") continue;
                if (read_ll(dev + "/gpu_busy_percent") < 0) continue;
                AmdCard c;
                c.dev = dev;
                if (DIR* h = opendir((dev + "/hwmon").c_str())) {
                    while (dirent* he = readdir(h))
                        if (strncmp(he->d_name, "hwmon", 5) == 0) c.hwmon = dev + "/hwmon/" + he->d_name;
                    closedir(h);
                }
                std::string pn = trim(read_file(dev + "/product_name"));
                c.name = pn.empty() ? "AMD Radeon (" + n + ")" : pn;
                amd.push_back(c);
            }
            closedir(dir);
        }
    }

    void sample_nvidia(Snapshot& s) {
        for (size_t i = 0; i < nv_devs.size(); i++) {
            nv::dev_t d = nv_devs[i];
            GpuStats g;
            g.vendor = "NVIDIA";
            char name[96] = {0};
            if (nvml.DeviceGetName && nvml.DeviceGetName(d, name, sizeof name) == nv::SUCCESS) g.name = name;
            nv::Utilization u{};
            if (nvml.DeviceGetUtilizationRates && nvml.DeviceGetUtilizationRates(d, &u) == nv::SUCCESS) {
                g.util = (int)u.gpu;
                g.mem_util = (int)u.memory;
            }
            nv::Memory m{};
            nv::ret_t mr = nvml.DeviceGetMemoryInfo ? nvml.DeviceGetMemoryInfo(d, &m) : nv::NOT_SUPPORTED;
            if (mr == nv::SUCCESS && m.total > 0) {
                g.mem_used = m.used;
                g.mem_total = m.total;
            } else {
                g.unified = true;  // filled from process totals + system RAM below
            }
            unsigned v = 0;
            if (nvml.DeviceGetTemperature && nvml.DeviceGetTemperature(d, 0, &v) == nv::SUCCESS) g.temp_c = (int)v;
            if (nvml.DeviceGetPowerUsage && nvml.DeviceGetPowerUsage(d, &v) == nv::SUCCESS) g.power_w = v / 1000.0;
            if (nvml.DeviceGetEnforcedPowerLimit && nvml.DeviceGetEnforcedPowerLimit(d, &v) == nv::SUCCESS) g.power_limit_w = v / 1000.0;
            if (nvml.DeviceGetClockInfo && nvml.DeviceGetClockInfo(d, 0, &v) == nv::SUCCESS) g.clock_mhz = (int)v;
            if (nvml.DeviceGetClockInfo && nvml.DeviceGetClockInfo(d, 2, &v) == nv::SUCCESS) g.mem_clock_mhz = (int)v;
            if (nvml.DeviceGetFanSpeed && nvml.DeviceGetFanSpeed(d, &v) == nv::SUCCESS) g.fan_pct = (int)v;
            unsigned period = 0;
            if (nvml.DeviceGetEncoderUtilization && nvml.DeviceGetEncoderUtilization(d, &v, &period) == nv::SUCCESS) g.enc_util = (int)v;
            if (nvml.DeviceGetDecoderUtilization && nvml.DeviceGetDecoderUtilization(d, &v, &period) == nv::SUCCESS) g.dec_util = (int)v;

            // Processes (compute + graphics, merged by pid).
            std::map<unsigned, GpuProcess> procs;
            auto collect = [&](auto fn) {
                if (!fn) return;
                unsigned cnt = 0;
                nv::ret_t r = fn(d, &cnt, nullptr);
                if (r != nv::SUCCESS && r != nv::INSUFFICIENT_SIZE) return;
                std::vector<nv::ProcessInfo> buf(cnt + 8);
                cnt = (unsigned)buf.size();
                if (fn(d, &cnt, buf.data()) != nv::SUCCESS) return;
                for (unsigned k = 0; k < cnt; k++) {
                    GpuProcess& p = procs[buf[k].pid];
                    p.pid = buf[k].pid;
                    p.gpu = (int)i;
                    unsigned long long mem = buf[k].usedGpuMemory;
                    if (mem != ~0ULL) p.mem_bytes = std::max<uint64_t>(p.mem_bytes, mem);
                }
            };
            collect(nvml.DeviceGetComputeRunningProcesses);
            collect(nvml.DeviceGetGraphicsRunningProcesses);

            if (nvml.DeviceGetProcessUtilization) {
                unsigned cnt = 0;
                nv::ret_t r = nvml.DeviceGetProcessUtilization(d, nullptr, &cnt, nv_last_ts[i]);
                if ((r == nv::SUCCESS || r == nv::INSUFFICIENT_SIZE) && cnt > 0) {
                    std::vector<nv::ProcUtilSample> sm(cnt);
                    if (nvml.DeviceGetProcessUtilization(d, sm.data(), &cnt, nv_last_ts[i]) == nv::SUCCESS) {
                        for (unsigned k = 0; k < cnt; k++) {
                            auto it = procs.find(sm[k].pid);
                            if (it == procs.end()) continue;
                            it->second.sm_util = std::max(it->second.sm_util, (int)sm[k].smUtil);
                            nv_last_ts[i] = std::max(nv_last_ts[i], sm[k].timeStamp);
                        }
                    }
                }
            }
            uint64_t proc_mem_sum = 0;
            for (auto& [pid, p] : procs) {
                if (p.sm_util < 0) p.sm_util = 0;
                proc_mem_sum += p.mem_bytes;
                fill_proc_identity(p);
                s.procs.push_back(p);
            }
            if (g.unified) {
                g.mem_used = proc_mem_sum;
                g.mem_total = s.ram_total;
            }
            s.gpus.push_back(g);
        }
    }

    void sample_amd(Snapshot& s) {
        for (auto& c : amd) {
            GpuStats g;
            g.vendor = "AMD";
            g.name = c.name;
            g.util = (int)read_ll(c.dev + "/gpu_busy_percent");
            g.mem_util = (int)read_ll(c.dev + "/mem_busy_percent");
            long long used = read_ll(c.dev + "/mem_info_vram_used", 0), total = read_ll(c.dev + "/mem_info_vram_total", 0);
            g.mem_used = (uint64_t)used;
            g.mem_total = (uint64_t)total;
            if (total < 512LL * 1024 * 1024) {  // APU with tiny carve-out: count GTT as well
                g.unified = true;
                g.mem_used += (uint64_t)read_ll(c.dev + "/mem_info_gtt_used", 0);
                g.mem_total = s.ram_total;
            }
            if (!c.hwmon.empty()) {
                long long t = read_ll(c.hwmon + "/temp1_input");
                if (t > 0) g.temp_c = (int)(t / 1000);
                long long p = read_ll(c.hwmon + "/power1_average");
                if (p < 0) p = read_ll(c.hwmon + "/power1_input");
                if (p > 0) g.power_w = p / 1e6;
                long long cap = read_ll(c.hwmon + "/power1_cap");
                if (cap > 0) g.power_limit_w = cap / 1e6;
                long long clk = read_ll(c.hwmon + "/freq1_input");
                if (clk > 0) g.clock_mhz = (int)(clk / 1000000);
                long long pwm = read_ll(c.hwmon + "/pwm1");
                if (pwm >= 0) g.fan_pct = (int)(pwm * 100 / 255);
            }
            s.gpus.push_back(g);
        }
    }

    void sample_system(Snapshot& s) {
        std::istringstream mi(read_file("/proc/meminfo"));
        std::string key;
        unsigned long long val;
        std::string unit;
        unsigned long long total = 0, avail = 0, stotal = 0, sfree = 0;
        while (mi >> key >> val) {
            std::getline(mi, unit);
            if (key == "MemTotal:") total = val;
            else if (key == "MemAvailable:") avail = val;
            else if (key == "SwapTotal:") stotal = val;
            else if (key == "SwapFree:") sfree = val;
        }
        s.ram_total = total * 1024;
        s.ram_used = (total - avail) * 1024;
        s.swap_total = stotal * 1024;
        s.swap_used = (stotal - sfree) * 1024;

        std::istringstream st(read_file("/proc/stat"));
        std::string cpu;
        unsigned long long f[10] = {0};
        st >> cpu;
        for (auto& x : f) st >> x;
        unsigned long long idle = f[3] + f[4], tot = 0;
        for (int i = 0; i < 8; i++) tot += f[i];
        if (cpu_prev_total && tot > cpu_prev_total)
            s.cpu_util = 100.0 * (1.0 - double(idle - cpu_prev_idle) / double(tot - cpu_prev_total));
        cpu_prev_total = tot;
        cpu_prev_idle = idle;
    }

    Snapshot sample() {
        Snapshot s;
        sample_system(s);
        if (have_nvml) sample_nvidia(s);
        sample_amd(s);
        std::sort(s.procs.begin(), s.procs.end(), [](const GpuProcess& a, const GpuProcess& b) {
            if (a.mem_bytes != b.mem_bytes) return a.mem_bytes > b.mem_bytes;
            return a.sm_util > b.sm_util;
        });
        return s;
    }
};

Metrics::Metrics() : impl_(new Impl) {
    impl_->init();
    impl_->sample();  // primes CPU counters so the first real sample has a delta
}

Metrics::~Metrics() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    delete impl_;
}

void Metrics::start(int interval_ms, std::function<void()> on_update) {
    interval_ms_ = interval_ms;
    {
        Snapshot s = impl_->sample();
        std::lock_guard<std::mutex> lk(mu_);
        snap_ = std::move(s);
    }
    thread_ = std::thread([this, on_update] { run(on_update); });
}

void Metrics::run(std::function<void()> on_update) {
    uint64_t seq = 1;
    while (!stop_) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(interval_ms_.load()), [this] { return stop_.load(); });
        }
        if (stop_) break;
        Snapshot s = impl_->sample();
        s.seq = ++seq;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snap_ = std::move(s);
        }
        if (on_update) on_update();
    }
}

Snapshot Metrics::snapshot() {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

std::vector<GpuStatic> Metrics::gpu_static() {
    std::vector<GpuStatic> out;
    Impl& m = *impl_;
    std::string driver, cuda;
    if (m.have_nvml) {
        char buf[96] = {0};
        if (m.nvml.SystemGetDriverVersion && m.nvml.SystemGetDriverVersion(buf, sizeof buf) == nv::SUCCESS) driver = buf;
        int cv = 0;
        if (m.nvml.SystemGetCudaDriverVersion && m.nvml.SystemGetCudaDriverVersion(&cv) == nv::SUCCESS)
            cuda = std::to_string(cv / 1000) + "." + std::to_string((cv % 1000) / 10);
    }
    long long ram_kb = 0;
    {
        std::istringstream mi(read_file("/proc/meminfo"));
        std::string k;
        mi >> k >> ram_kb;
    }
    for (auto d : m.nv_devs) {
        GpuStatic g;
        g.vendor = "NVIDIA";
        g.driver = driver;
        g.cuda = cuda;
        char buf[96] = {0};
        if (m.nvml.DeviceGetName && m.nvml.DeviceGetName(d, buf, sizeof buf) == nv::SUCCESS) g.name = buf;
        memset(buf, 0, sizeof buf);
        if (m.nvml.DeviceGetUUID && m.nvml.DeviceGetUUID(d, buf, sizeof buf) == nv::SUCCESS) g.uuid = buf;
        nv::PciInfo pci{};
        if (m.nvml.DeviceGetPciInfo && m.nvml.DeviceGetPciInfo(d, &pci) == nv::SUCCESS) g.pci = pci.busId;
        nv::Memory mem{};
        if (m.nvml.DeviceGetMemoryInfo && m.nvml.DeviceGetMemoryInfo(d, &mem) == nv::SUCCESS && mem.total) g.mem_total = mem.total;
        else { g.unified = true; g.mem_total = (uint64_t)ram_kb * 1024; }
        unsigned v = 0;
        if (m.nvml.DeviceGetMaxClockInfo && m.nvml.DeviceGetMaxClockInfo(d, 0, &v) == nv::SUCCESS) g.max_clock_mhz = (int)v;
        if (m.nvml.DeviceGetMaxClockInfo && m.nvml.DeviceGetMaxClockInfo(d, 2, &v) == nv::SUCCESS) g.max_mem_clock_mhz = (int)v;
        if (m.nvml.DeviceGetEnforcedPowerLimit && m.nvml.DeviceGetEnforcedPowerLimit(d, &v) == nv::SUCCESS) g.power_limit_w = v / 1000.0;
        out.push_back(g);
    }
    for (auto& c : m.amd) {
        GpuStatic g;
        g.vendor = "AMD";
        g.name = c.name;
        g.driver = "amdgpu";
        g.mem_total = (uint64_t)read_ll(c.dev + "/mem_info_vram_total", 0);
        char path[PATH_MAX] = {0};
        if (realpath(c.dev.c_str(), path)) {
            std::string p = path;
            g.pci = p.substr(p.find_last_of('/') + 1);
        }
        g.uuid = trim(read_file(c.dev + "/unique_id"));
        out.push_back(g);
    }
    return out;
}
