#include "sysinfo.h"

#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "i18n.h"
#include "metrics.h"

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

static std::string run(const char* cmd) {
    std::string out;
    if (FILE* p = popen(cmd, "r")) {
        char buf[512];
        while (fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    return out;
}

static std::string strip(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\"");
    size_t b = s.find_last_not_of(" \t\"\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::string os_pretty() {
    std::istringstream in(slurp("/etc/os-release"));
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("PRETTY_NAME=", 0) == 0) return strip(line.substr(12));
    return "Linux";
}

static std::string uptime_str() {
    double up = atof(slurp("/proc/uptime").c_str());
    long s = (long)up;
    char buf[64];
    snprintf(buf, sizeof buf, "%ldd %02ld:%02ld", s / 86400, (s / 3600) % 24, (s / 60) % 60);
    return buf;
}

SysInfo collect_sysinfo() {
    SysInfo si;
    utsname u{};
    uname(&u);
    char host[256] = {0};
    gethostname(host, sizeof host - 1);
    const char* desk = getenv("XDG_CURRENT_DESKTOP");
    const char* sess = getenv("XDG_SESSION_TYPE");
    si.system = {
        {tr(S_HOSTNAME), host},
        {tr(S_OS), os_pretty()},
        {tr(S_KERNEL), std::string(u.sysname) + " " + u.release},
        {tr(S_ARCH), u.machine},
        {tr(S_UPTIME), uptime_str()},
        {tr(S_DESKTOP), std::string(desk ? desk : "?") + " (" + (sess ? sess : "?") + ")"},
    };

    // CPU: lscpu handles both x86 and heterogeneous ARM clusters.
    std::istringstream ls(run("LC_ALL=C lscpu 2>/dev/null"));
    std::string line, vendor, maxmhz;
    std::vector<std::string> models;
    std::string cur_model;
    std::map<std::string, int> cores_per_model;
    while (std::getline(ls, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = strip(line.substr(0, c)), v = strip(line.substr(c + 1));
        if (k == "Vendor ID" && vendor.empty()) vendor = v;
        else if (k == "Model name") { cur_model = v; models.push_back(v); }
        else if (k == "Core(s) per socket" || k == "Core(s) per cluster") cores_per_model[cur_model] += atoi(v.c_str());
        else if (k == "CPU max MHz") {
            double mhz = atof(v.c_str());
            if (maxmhz.empty() || mhz > atof(maxmhz.c_str())) maxmhz = std::to_string((int)mhz);
        }
    }
    if (models.empty()) {  // fallback: /proc/cpuinfo
        std::istringstream ci(slurp("/proc/cpuinfo"));
        while (std::getline(ci, line))
            if (line.rfind("model name", 0) == 0) { models.push_back(strip(line.substr(line.find(':') + 1))); break; }
    }
    std::string model_str;
    for (auto& m : models) {
        if (!model_str.empty()) model_str += " + ";
        int n = cores_per_model[m];
        model_str += n > 0 && models.size() > 1 ? std::to_string(n) + "× " + m : m;
    }
    si.cpu = {
        {tr(S_MODEL), model_str.empty() ? "?" : model_str},
        {tr(S_VENDOR), vendor.empty() ? "?" : vendor},
        {tr(S_CORES), std::to_string(sysconf(_SC_NPROCESSORS_ONLN))},
    };
    if (!maxmhz.empty()) si.cpu.push_back({tr(S_MAX_FREQ), maxmhz + " MHz"});

    long pages = sysconf(_SC_PHYS_PAGES), psize = sysconf(_SC_PAGE_SIZE);
    si.memory = {{tr(S_TOTAL), fmt_bytes((uint64_t)pages * psize)}};
    {
        std::istringstream mi(slurp("/proc/meminfo"));
        std::string k;
        unsigned long long v;
        std::string unit;
        while (mi >> k >> v) {
            std::getline(mi, unit);
            if (k == "SwapTotal:") si.memory.push_back({tr(S_SWAP), fmt_bytes(v * 1024)});
        }
    }

    auto dmi = [](const char* f) { return slurp(std::string("/sys/class/dmi/id/") + f); };
    std::string sv = dmi("sys_vendor"), pn = dmi("product_name"), bv = dmi("board_vendor"), bn = dmi("board_name");
    std::string biosv = dmi("bios_version"), biosd = dmi("bios_date");
    if (pn.empty()) pn = slurp("/proc/device-tree/model");  // ARM boards
    if (!sv.empty() || !pn.empty()) si.board.push_back({tr(S_PRODUCT), strip(sv + " " + pn)});
    if (!bv.empty() || !bn.empty()) si.board.push_back({tr(S_BOARD), strip(bv + " " + bn)});
    if (!biosv.empty()) si.board.push_back({tr(S_BIOS), strip(biosv + " " + biosd)});

    // Block devices (whole disks) with model + size, then mounted filesystems.
    std::istringstream lb(run("LC_ALL=C lsblk -dno NAME,SIZE,MODEL,TRAN,TYPE 2>/dev/null"));
    while (std::getline(lb, line)) {
        if (line.find("loop") == 0 || line.find(" disk") == std::string::npos) continue;
        line = line.substr(0, line.rfind(" disk"));
        std::string l2;
        bool sp = false;
        for (char ch : line) {  // collapse whitespace
            if (ch == ' ') { if (!sp) l2 += ' '; sp = true; }
            else { l2 += ch; sp = false; }
        }
        si.storage.push_back("/dev/" + strip(l2));
    }
    std::istringstream mounts(slurp("/proc/mounts"));
    std::set<std::string> seen;
    while (std::getline(mounts, line)) {
        std::istringstream ml(line);
        std::string dev, mnt, fs;
        ml >> dev >> mnt >> fs;
        if (dev.rfind("/dev/", 0) != 0 || dev.rfind("/dev/loop", 0) == 0 || seen.count(dev)) continue;
        if (mnt.rfind("/snap", 0) == 0 || mnt.rfind("/boot", 0) == 0) continue;
        seen.insert(dev);
        struct statvfs st{};
        if (statvfs(mnt.c_str(), &st) != 0 || st.f_blocks == 0) continue;
        uint64_t total = (uint64_t)st.f_blocks * st.f_frsize, avail = (uint64_t)st.f_bavail * st.f_frsize;
        si.storage.push_back("  " + mnt + " (" + fs + ") " + fmt_bytes(total - avail) + " / " + fmt_bytes(total) + " " + tr(S_USED));
    }
    return si;
}
