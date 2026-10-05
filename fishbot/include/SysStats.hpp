#pragma once
// Process + host statistics for the /stats command. No Discord or SQLite dependency,
// so it can be unit-tested on its own. Reads /proc on Linux; on other platforms every
// numeric field is 0 and `supported` is false (the command then shows "N/A").
#include <cstdint>
#include <string>

struct ProcStats {
    bool supported = false;
    int pid = 0;
    int threads = 0;
    uint64_t rss_bytes = 0;        // resident memory (what the bot really uses in RAM)
    uint64_t vsize_bytes = 0;      // virtual memory size (VmSize) - the "VM/VSZ" figure
    uint64_t vpeak_bytes = 0;      // peak virtual memory (VmPeak)
    uint64_t swap_bytes = 0;       // bot memory currently swapped out
    uint64_t cpu_time_ms = 0;      // total CPU time (user + system) since start
    double cpu_percent_core = 0;   // CPU use over a short sample, 100% == one full core
    double cpu_percent_total = 0;  // same, normalised by core count (100% == whole machine)
};

struct HostStats {
    bool supported = false;
    std::string cpu_model;
    int cores = 0;
    double load1 = 0, load5 = 0, load15 = 0;
    uint64_t mem_total = 0, mem_available = 0;   // bytes
    uint64_t swap_total = 0, swap_free = 0;      // bytes
    uint64_t uptime_seconds = 0;                 // host uptime
    std::string os;                              // e.g. "Ubuntu 24.04.1 LTS"
};

// Samples the process CPU over `sample_ms` milliseconds (blocks that long) and reads
// the memory counters. 250ms is plenty for a stable number.
ProcStats read_proc_stats(int sample_ms = 250);
HostStats read_host_stats();

// Seconds since the bot process started (monotonic).
uint64_t bot_uptime_seconds();

// ---- formatting helpers ----
// "1.50 MB" style, base 1024 (1 KB = 1024 bytes).
std::string fmt_bytes(uint64_t bytes);
// "12,345" with thousands separators.
std::string fmt_number(uint64_t n);
// "2d 3h 4m 5s" (always shows seconds, drops leading zero units).
std::string fmt_uptime(uint64_t seconds);

// Every size unit side by side, for the database-size display.
struct SizeBreakdown {
    uint64_t bits, bytes;
    double kb, mb, gb;
};
SizeBreakdown size_breakdown(uint64_t bytes);

// Size in bytes of a file, or 0 if it doesn't exist.
uint64_t file_size_or_zero(const std::string& path);
