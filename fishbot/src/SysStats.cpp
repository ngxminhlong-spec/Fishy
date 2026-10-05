#include "SysStats.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <sys/stat.h>

#ifdef __linux__
#include <unistd.h>
#endif

// ---------------------------------------------------------------- uptime

static const std::chrono::steady_clock::time_point g_started = std::chrono::steady_clock::now();

uint64_t bot_uptime_seconds() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - g_started).count();
}

// ---------------------------------------------------------------- formatting

std::string fmt_bytes(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    char buf[64];
    if (u == 0) std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)bytes);
    else        std::snprintf(buf, sizeof buf, "%.2f %s", v, units[u]);
    return buf;
}

std::string fmt_number(uint64_t n) {
    std::string s = std::to_string(n), out;
    int cnt = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (cnt && cnt % 3 == 0) out.push_back(',');
        out.push_back(*it);
        cnt++;
    }
    return std::string(out.rbegin(), out.rend());
}

std::string fmt_uptime(uint64_t s) {
    uint64_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
    std::ostringstream o;
    if (d) o << d << "d ";
    if (d || h) o << h << "h ";
    if (d || h || m) o << m << "m ";
    o << sec << "s";
    return o.str();
}

SizeBreakdown size_breakdown(uint64_t bytes) {
    SizeBreakdown b;
    b.bytes = bytes;
    b.bits = bytes * 8;
    b.kb = bytes / 1024.0;
    b.mb = bytes / (1024.0 * 1024.0);
    b.gb = bytes / (1024.0 * 1024.0 * 1024.0);
    return b;
}

uint64_t file_size_or_zero(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

// ---------------------------------------------------------------- /proc helpers

#ifdef __linux__

static uint64_t kb_field(const std::string& line) {
    // "VmRSS:     12345 kB"
    size_t colon = line.find(':');
    if (colon == std::string::npos) return 0;
    return (uint64_t)std::strtoull(line.c_str() + colon + 1, nullptr, 10) * 1024ULL;
}

// utime + stime of this process, in clock ticks. Returns false on failure.
static bool read_cpu_ticks(uint64_t& ticks) {
    std::ifstream f("/proc/self/stat");
    std::string line;
    if (!f || !std::getline(f, line)) return false;
    // Field 2 (comm) is in parentheses and may contain spaces, so skip past the LAST ')'.
    size_t rp = line.rfind(')');
    if (rp == std::string::npos) return false;
    std::istringstream rest(line.substr(rp + 1));
    std::vector<std::string> tok;
    std::string t;
    while (rest >> t) tok.push_back(t);
    // tok[0] is field 3 (state), so utime (field 14) is tok[11] and stime (field 15) is tok[12].
    if (tok.size() < 13) return false;
    ticks = std::strtoull(tok[11].c_str(), nullptr, 10) + std::strtoull(tok[12].c_str(), nullptr, 10);
    return true;
}

ProcStats read_proc_stats(int sample_ms) {
    ProcStats p;
    p.pid = (int)getpid();

    std::ifstream st("/proc/self/status");
    std::string line;
    while (st && std::getline(st, line)) {
        if (line.rfind("VmRSS:", 0) == 0) p.rss_bytes = kb_field(line);
        else if (line.rfind("VmSize:", 0) == 0) p.vsize_bytes = kb_field(line);
        else if (line.rfind("VmPeak:", 0) == 0) p.vpeak_bytes = kb_field(line);
        else if (line.rfind("VmSwap:", 0) == 0) p.swap_bytes = kb_field(line);
        else if (line.rfind("Threads:", 0) == 0) p.threads = std::atoi(line.c_str() + 8);
    }

    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores <= 0) cores = 1;

    uint64_t t0 = 0, t1 = 0;
    bool ok0 = read_cpu_ticks(t0);
    auto w0 = std::chrono::steady_clock::now();
    if (sample_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));
    bool ok1 = read_cpu_ticks(t1);
    auto w1 = std::chrono::steady_clock::now();

    if (ok0 && ok1) {
        double wall = std::chrono::duration<double>(w1 - w0).count();
        if (wall > 0) {
            double cpu_secs = (double)(t1 - t0) / (double)hz;
            p.cpu_percent_core = 100.0 * cpu_secs / wall;
            p.cpu_percent_total = p.cpu_percent_core / (double)cores;
        }
        p.cpu_time_ms = (uint64_t)((double)t1 * 1000.0 / (double)hz);
    }
    p.supported = ok1 || p.rss_bytes > 0;
    return p;
}

HostStats read_host_stats() {
    HostStats h;

    {
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (f && std::getline(f, line)) {
            if (line.rfind("model name", 0) == 0) {
                size_t c = line.find(':');
                if (c != std::string::npos) {
                    h.cpu_model = line.substr(c + 1);
                    size_t s = h.cpu_model.find_first_not_of(" \t");
                    h.cpu_model = (s == std::string::npos) ? "" : h.cpu_model.substr(s);
                }
                break;
            }
            // ARM boards often have "Hardware" / "Model" instead of "model name".
            if (h.cpu_model.empty() && (line.rfind("Hardware", 0) == 0 || line.rfind("Model", 0) == 0)) {
                size_t c = line.find(':');
                if (c != std::string::npos && c + 2 <= line.size()) h.cpu_model = line.substr(c + 2);
            }
        }
    }
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    h.cores = cores > 0 ? (int)cores : 1;

    {
        std::ifstream f("/proc/loadavg");
        if (f) f >> h.load1 >> h.load5 >> h.load15;
    }
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (f && std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) h.mem_total = kb_field(line);
            else if (line.rfind("MemAvailable:", 0) == 0) h.mem_available = kb_field(line);
            else if (line.rfind("SwapTotal:", 0) == 0) h.swap_total = kb_field(line);
            else if (line.rfind("SwapFree:", 0) == 0) h.swap_free = kb_field(line);
        }
    }
    {
        std::ifstream f("/proc/uptime");
        double up = 0;
        if (f && (f >> up)) h.uptime_seconds = (uint64_t)up;
    }
    {
        std::ifstream f("/etc/os-release");
        std::string line;
        while (f && std::getline(f, line)) {
            if (line.rfind("PRETTY_NAME=", 0) == 0) {
                h.os = line.substr(12);
                if (h.os.size() >= 2 && h.os.front() == '"') h.os = h.os.substr(1, h.os.size() - 2);
                break;
            }
        }
    }
    h.supported = h.mem_total > 0;
    return h;
}

#else  // ---------- non-Linux fallback: everything stays zero / "N/A"

ProcStats read_proc_stats(int) { return ProcStats{}; }
HostStats read_host_stats() { return HostStats{}; }

#endif
