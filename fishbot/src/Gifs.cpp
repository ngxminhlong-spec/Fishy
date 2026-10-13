#include "Gifs.hpp"
#include <dpp/json.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <vector>

namespace gifs {

namespace {

std::mutex lock_;
std::map<std::string, std::vector<std::string>> from_file;
std::map<std::string, std::vector<std::string>> from_api;

// What to ask GIPHY for, per moment in the game.
const std::map<std::string, std::string> SEARCH = {
    {"cast",    "fishing"},
    {"miss",    "sad fish"},
    {"escape",  "fish escapes"},
    {"catch",   "caught a fish"},
    {"legend",  "huge fish"},
    {"money",   "cartoon money"},
    {"level",   "level up"},
    {"boss",    "sea monster"},
    {"victory", "victory dance"},
    {"trade",   "handshake deal"},
    {"lottery", "jackpot"},
};

// A category with nothing in it borrows from a neighbour.
const std::map<std::string, std::string> FALLBACK = {
    {"legend", "catch"}, {"level", "victory"}, {"lottery", "money"},
    {"daily", "money"},  {"sell", "money"},    {"auction", "trade"},
    {"bite", "cast"},    {"win", "victory"},
};

std::mt19937& dice() {
    static std::mt19937 g{std::random_device{}()};
    return g;
}

bool disabled() {
    const char* v = std::getenv("GIFS");
    return v && std::string(v) == "off";
}

int chance_percent() {
    const char* v = std::getenv("GIF_CHANCE");
    if (!v || !*v) return 100;
    try { return std::max(0, std::min(100, std::stoi(v))); } catch (...) { return 100; }
}

std::string pick_from(const std::map<std::string, std::vector<std::string>>& src, const std::string& cat) {
    auto it = src.find(cat);
    if (it == src.end() || it->second.empty()) return "";
    std::uniform_int_distribution<size_t> d(0, it->second.size() - 1);
    return it->second[d(dice())];
}

std::string plus_encode(const std::string& s) {
    std::string out;
    for (char c : s) out += (c == ' ') ? '+' : c;
    return out;
}

}  // namespace

void load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cout << "[gifs] no list at " << path << ", relying on GIPHY only\n";
        return;
    }
    std::map<std::string, std::vector<std::string>> parsed;
    std::string line;
    size_t count = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string cat, url;
        if (!(ss >> cat >> url)) continue;
        if (url.rfind("http", 0) != 0) continue;
        parsed[cat].push_back(url);
        count++;
    }
    std::lock_guard<std::mutex> g(lock_);
    from_file = std::move(parsed);
    std::cout << "[gifs] loaded " << count << " gif(s) from " << path << "\n";
}

void refresh(dpp::cluster& bot) {
    const char* key = std::getenv("GIPHY_API_KEY");
    if (!key || !*key || disabled()) return;
    std::string api_key = key;

    for (const auto& [category, query] : SEARCH) {
        std::string url = "https://api.giphy.com/v1/gifs/search?api_key=" + api_key +
                          "&limit=40&rating=pg&lang=en&q=" + plus_encode(query);
        bot.request(url, dpp::m_get, [category](const dpp::http_request_completion_t& done) {
            if (done.status != 200) {
                std::cout << "[gifs] giphy answered " << done.status << " for '" << category << "'\n";
                return;
            }
            std::vector<std::string> found;
            try {
                auto body = dpp::json::parse(done.body);
                for (auto& item : body["data"]) {
                    auto& images = item["images"];
                    if (images.contains("downsized"))      found.push_back(images["downsized"]["url"].get<std::string>());
                    else if (images.contains("original"))  found.push_back(images["original"]["url"].get<std::string>());
                }
            } catch (const std::exception& ex) {
                std::cout << "[gifs] could not read giphy reply for '" << category << "': " << ex.what() << "\n";
                return;
            }
            if (found.empty()) return;
            std::lock_guard<std::mutex> g(lock_);
            from_api[category] = std::move(found);
        });
    }
}

std::string pick(const std::string& category) {
    if (disabled()) return "";
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(dice()) > chance_percent()) return "";

    std::lock_guard<std::mutex> g(lock_);
    std::string cat = category;
    for (int hops = 0; hops < 2; hops++) {
        std::string url = pick_from(from_api, cat);
        if (url.empty()) url = pick_from(from_file, cat);
        if (!url.empty()) return url;
        auto fb = FALLBACK.find(cat);
        if (fb == FALLBACK.end()) break;
        cat = fb->second;
    }
    return "";
}

}  // namespace gifs
