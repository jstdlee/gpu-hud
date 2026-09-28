// Live hardware metrics: GPU (NVML via dlopen, AMD via sysfs), CPU, RAM, GPU processes.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct GpuProcess {
    unsigned pid = 0;
    int gpu = 0;
    uint64_t mem_bytes = 0;   // 0 when the driver does not report it
    int sm_util = -1;         // -1 = unknown
    double cpu_pct = -1;      // % of one core (top-style), -1 = not yet known
    std::string name;         // /proc/<pid>/comm
    std::string cmdline;
    std::string user;
};

struct GpuStats {
    std::string vendor;       // "NVIDIA", "AMD"
    std::string name;
    int util = -1;            // %
    int mem_util = -1;        // memory controller %
    uint64_t mem_used = 0, mem_total = 0;
    bool unified = false;     // no dedicated VRAM (e.g. GB10 / Jetson / APU)
    int temp_c = -1;
    double power_w = -1, power_limit_w = -1;
    int clock_mhz = -1, mem_clock_mhz = -1;
    int fan_pct = -1;
    int enc_util = -1, dec_util = -1;
};

struct Snapshot {
    std::vector<GpuStats> gpus;
    std::vector<GpuProcess> procs;   // sorted: memory desc, then SM desc
    double cpu_util = 0;             // %
    uint64_t ram_used = 0, ram_total = 0;
    uint64_t swap_used = 0, swap_total = 0;
    uint64_t seq = 0;
};

// Static per-GPU facts used by the hardware report.
struct GpuStatic {
    std::string vendor, name, driver, cuda, pci, uuid;
    uint64_t mem_total = 0;
    bool unified = false;
    int max_clock_mhz = -1, max_mem_clock_mhz = -1;
    double power_limit_w = -1;
};

class Metrics {
public:
    Metrics();
    ~Metrics();
    void start(int interval_ms, std::function<void()> on_update);
    void set_interval(int ms) { interval_ms_ = ms; }
    Snapshot snapshot();
    std::vector<GpuStatic> gpu_static();

private:
    struct Impl;
    Impl* impl_;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<int> interval_ms_{1000};
    Snapshot snap_;
    void run(std::function<void()> on_update);
};

std::string fmt_bytes(uint64_t b);
