// One-shot hardware / OS inventory for the copy-to-clipboard report.
#pragma once
#include <string>
#include <utility>
#include <vector>

struct KV {
    std::string key, value;
};

struct SysInfo {
    std::vector<KV> system;       // hostname, os, kernel, ...
    std::vector<KV> cpu;
    std::vector<KV> memory;
    std::vector<KV> board;
    std::vector<std::string> storage;
};

// Keys are translated through i18n at collection time.
SysInfo collect_sysinfo();
