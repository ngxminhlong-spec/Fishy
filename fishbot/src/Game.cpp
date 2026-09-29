#include "Game.hpp"
#include <cmath>
#include <ctime>
#include <algorithm>

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

// ---------------- Weather ----------------

const char* weather_name(Weather w) {
    switch (w) {
        case Weather::Sunny: return "Sunny";
        case Weather::Rainy: return "Rainy";
        case Weather::Stormy: return "Stormy";
        case Weather::Foggy: return "Foggy";
        case Weather::Windy: return "Windy";
    }
    return "?";
}

const char* weather_emoji(Weather w) {
    switch (w) {
        case Weather::Sunny: return "☀️";
        case Weather::Rainy: return "🌧️";
        case Weather::Stormy: return "⛈️";
        case Weather::Foggy: return "🌫️";
        case Weather::Windy: return "💨";
    }
    return "?";
}

double weather_bite_mult(Weather w) {
    switch (w) {
        case Weather::Sunny: return 1.0;
        case Weather::Rainy: return 1.15;   // fish bite more in rain
        case Weather::Stormy: return 0.7;   // dangerous, fewer bites
        case Weather::Foggy: return 1.05;
        case Weather::Windy: return 0.9;
    }
    return 1.0;
}

double weather_rare_mult(Weather w) {
    switch (w) {
        case Weather::Sunny: return 1.0;
        case Weather::Rainy: return 1.1;
        case Weather::Stormy: return 1.5;   // storms stir up rare deep fish
        case Weather::Foggy: return 1.2;
        case Weather::Windy: return 1.0;
    }
    return 1.0;
}

WorldState& WorldState::instance() {
    static WorldState w;
    return w;
}

void WorldState::randomize_weather() {
    static const Weather all[] = {Weather::Sunny, Weather::Rainy, Weather::Stormy, Weather::Foggy, Weather::Windy};
    std::uniform_int_distribution<int> d(0, 4);
    weather_ = all[d(rng())];
}

void WorldState::start_tournament(const std::string& location, int minutes) {
    tournament_active_ = true;
    tournament_location_ = location;
    tournament_ends_at_ = (int64_t)time(nullptr) + minutes * 60;
}

void WorldState::end_tournament() {
    tournament_active_ = false;
    tournament_location_.clear();
    tournament_ends_at_ = 0;
}

void WorldState::start_event(const std::string& key, const std::string& display_name, int hours) {
    event_active_ = true;
    event_key_ = key;
    event_name_ = display_name;
    event_ends_at_ = (int64_t)time(nullptr) + (int64_t)hours * 3600;
}

void WorldState::end_event() {
    event_active_ = false;
    event_key_.clear();
    event_name_.clear();
    event_ends_at_ = 0;
}

// ---------------- Casting ----------------

CastOutcome resolve_cast(int rod_tier, int bait_id, const std::string& location, double pet_bonus) {
    CastOutcome out;
    const Rod* rod = find_rod(rod_tier);
    const Bait* bait = bait_id >= 0 ? find_bait(bait_id) : nullptr;
    Weather w = WorldState::instance().weather();

    double bite_chance = 0.55;
    bite_chance *= weather_bite_mult(w);
    if (bait) bite_chance *= bait->bite_bonus;
    bite_chance += pet_bonus;
    bite_chance = std::clamp(bite_chance, 0.05, 0.97);

    std::uniform_real_distribution<double> d01(0.0, 1.0);
    if (d01(rng()) > bite_chance) {
        out.bit = false;
        return out;
    }

    auto raw_pool = fish_for_location(location);
    if (raw_pool.empty()) { out.bit = false; return out; }

    bool tournament_here = WorldState::instance().tournament_active() &&
                            WorldState::instance().tournament_location() == location;
    std::string current_weather = weather_name(w);
    std::string active_event_key = WorldState::instance().event_active() ?
                                    WorldState::instance().event_key() : "";

    // Event fish only enter the pool when their weather/tournament conditions are met.
    std::vector<const FishSpecies*> pool;
    pool.reserve(raw_pool.size());
    for (auto* f : raw_pool) {
        if (f->boss_only) continue;
        if (!f->required_weather.empty() && f->required_weather != current_weather) continue;
        if (f->requires_tournament && !tournament_here) continue;
        if (!f->event_key.empty() && f->event_key != active_event_key) continue;
        pool.push_back(f);
    }
    if (pool.empty()) { out.bit = false; return out; }

    double rare_mult = weather_rare_mult(w) * (rod ? rod->rare_bonus : 1.0) * (bait ? bait->rare_bonus : 1.0);

    double total = 0;
    std::vector<double> weights;
    weights.reserve(pool.size());
    for (auto* f : pool) {
        double ww = f->spawn_weight;
        if (f->rarity >= Rarity::Rare) ww *= rare_mult;
        weights.push_back(ww);
        total += ww;
    }

    std::uniform_real_distribution<double> pick(0.0, total);
    double r = pick(rng());
    const FishSpecies* chosen = pool.back();
    double acc = 0;
    for (size_t i = 0; i < pool.size(); i++) {
        acc += weights[i];
        if (r <= acc) { chosen = pool[i]; break; }
    }

    std::uniform_real_distribution<double> wdist(chosen->min_weight, chosen->max_weight);
    out.bit = true;
    out.fish = chosen;
    out.weight = std::round(wdist(rng()) * 100.0) / 100.0;
    int power = rod ? rod->power : 2;
    out.reels_required = std::max(1, (int)std::ceil((double)chosen->strength / (double)power));
    return out;
}

// ---------------- Market ----------------

double market_multiplier(int fish_id) {
    time_t now = time(nullptr);
    struct tm* t = gmtime(&now);
    int day_seed = t->tm_yday + t->tm_year * 400;
    // simple deterministic hash -> [0.8, 1.35]
    unsigned int h = (unsigned int)(day_seed * 2654435761u + fish_id * 40503u);
    h ^= (h >> 13);
    h *= 0x85ebca6b;
    h ^= (h >> 16);
    double frac = (h % 10000) / 10000.0;
    return 0.8 + frac * 0.55;
}

int market_sell_price(const FishSpecies& fish, double weight) {
    double price = fish.base_value * weight * market_multiplier(fish.id);
    return std::max(1, (int)std::round(price));
}

// ---------------- XP / Leveling ----------------

int xp_for_catch(const FishSpecies& fish) {
    int rarity_index = (int)fish.rarity; // 0..5
    return 5 + rarity_index * 15 + (int)std::round(fish.strength * 0.5);
}

int64_t xp_needed_for_level(int level) {
    // cumulative xp to REACH `level` (level 1 = 0 xp)
    return (int64_t)level * (level - 1) / 2 * 50; // triangular growth
}

int level_for_xp(int64_t xp) {
    int level = 1;
    while (xp_needed_for_level(level + 1) <= xp) level++;
    return level;
}

// ---------------- Quests ----------------

const std::vector<QuestDef>& quest_pool() {
    static const std::vector<QuestDef> Q = {
        {"catch_5",       "Catch 5 fish of any kind",             5,  100},
        {"catch_3_uncommon","Catch 3 Uncommon fish or rarer",     3,  200},
        {"catch_1_rare",  "Catch 1 Rare fish or rarer",           1,  300},
        {"sell_300",      "Earn 300 coins from selling fish",     300,250},
        {"catch_river",   "Catch 4 fish in the River",            4,  180},
        {"catch_ocean",   "Catch 3 fish in the Ocean",            3,  260},
        {"cast_10",       "Cast your line 10 times",              10, 150},
    };
    return Q;
}

const QuestDef* find_quest(const std::string& key) {
    for (auto& q : quest_pool()) if (q.key == key) return &q;
    return nullptr;
}

const QuestDef& random_quest() {
    auto& pool = quest_pool();
    std::uniform_int_distribution<size_t> d(0, pool.size() - 1);
    return pool[d(rng())];
}

// ---------------- Achievements ----------------

const std::vector<AchievementDef>& achievement_pool() {
    static const std::vector<AchievementDef> A = {
        {"first_catch",   "First Catch",       "Catch your very first fish."},
        {"fifty_fish",    "Fisher",            "Catch 50 fish in total."},
        {"five_hundred_fish","Master Angler",  "Catch 500 fish in total."},
        {"legendary_catch","Legend Hunter",    "Catch a Legendary fish."},
        {"mythic_catch",  "Mythbreaker",       "Catch a Mythic fish."},
        {"heavy_fifty",   "Big Game",          "Catch a fish weighing 50kg or more."},
        {"own_pet",       "Companion",         "Adopt a fishing pet."},
        {"level_10",      "Getting Somewhere", "Reach level 10."},
        {"level_25",      "Veteran Angler",    "Reach level 25."},
        {"void_rod",      "Top Of The Line",   "Own the Void Rod."},
        {"submarine",     "Deep Diver",        "Own a Submarine."},
        {"ten_quests",    "Quest Regular",     "Complete 10 daily quests."},
        {"boss_slayer",   "Boss Slayer",       "Help take down a World Boss."},
        {"boss_champion", "Boss Champion",     "Deal the most damage in a defeated World Boss fight."},
        {"crew_founder",  "Crew Founder",      "Found a fishing crew."},
    };
    return A;
}

std::vector<std::string> check_achievements(Database& db, const std::string& user_id,
                                             const FishSpecies* just_caught, double weight) {
    std::vector<std::string> unlocked;
    auto try_unlock = [&](const std::string& key) {
        if (!db.has_achievement(user_id, key)) {
            db.unlock_achievement(user_id, key);
            for (auto& a : achievement_pool()) {
                if (a.key == key) { unlocked.push_back(a.name); break; }
            }
        }
    };

    int total = db.count_fish(user_id);
    if (total >= 1) try_unlock("first_catch");
    if (total >= 50) try_unlock("fifty_fish");
    if (total >= 500) try_unlock("five_hundred_fish");
    if (weight >= 50.0) try_unlock("heavy_fifty");
    if (just_caught) {
        if (just_caught->rarity == Rarity::Legendary) try_unlock("legendary_catch");
        if (just_caught->rarity == Rarity::Mythic) try_unlock("mythic_catch");
    }

    UserRow u = db.get_or_create_user(user_id);
    if (u.pet_id >= 0) try_unlock("own_pet");
    if (u.level >= 10) try_unlock("level_10");
    if (u.level >= 25) try_unlock("level_25");
    if (u.rod_tier >= 5) try_unlock("void_rod");
    if (u.boat_tier >= 2) try_unlock("submarine");

    return unlocked;
}

// ---------------- Reel sessions ----------------

ReelSessionManager& ReelSessionManager::instance() {
    static ReelSessionManager m;
    return m;
}

void ReelSessionManager::put(const std::string& message_id, ReelSession session) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[message_id] = std::move(session);
}

std::optional<ReelSession> ReelSessionManager::get(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(message_id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void ReelSessionManager::update(const std::string& message_id, const ReelSession& session) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[message_id] = session;
}

void ReelSessionManager::erase(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_.erase(message_id);
}

// ---------------- World Boss ----------------

BossSessionManager& BossSessionManager::instance() {
    static BossSessionManager m;
    return m;
}

bool BossSessionManager::has_active(const std::string& guild_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    return sessions_.find(guild_id) != sessions_.end();
}

void BossSessionManager::put(const std::string& guild_id, BossSession session) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[guild_id] = std::move(session);
}

std::optional<BossSession> BossSessionManager::get(const std::string& guild_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(guild_id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void BossSessionManager::update(const std::string& guild_id, const BossSession& session) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[guild_id] = session;
}

void BossSessionManager::erase(const std::string& guild_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_.erase(guild_id);
}

std::vector<std::string> BossSessionManager::active_guild_ids() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> out;
    out.reserve(sessions_.size());
    for (auto& [gid, sess] : sessions_) out.push_back(gid);
    return out;
}

const FishSpecies* pick_boss_fish() {
    std::vector<const FishSpecies*> bosses;
    for (auto& f : all_fish()) if (f.boss_only) bosses.push_back(&f);
    if (bosses.empty()) return nullptr;
    std::uniform_int_distribution<size_t> d(0, bosses.size() - 1);
    return bosses[d(rng())];
}

double boss_hp_for(const FishSpecies& fish) {
    // Scales with the boss's "strength" stat; tuned so a full server working
    // together takes a few minutes, but one person alone realistically can't solo it.
    return fish.strength * 20.0;
}

double boss_damage_per_hit(int rod_tier, double pet_bonus) {
    const Rod* rod = find_rod(rod_tier);
    double base = rod ? rod->power : 2;
    return base * (1.0 + pet_bonus);
}

std::string render_health_bar(double current, double total, int segments) {
    if (total <= 0) total = 1;
    double frac = std::clamp(current / total, 0.0, 1.0);
    int filled = (int)std::round(frac * segments);
    std::string bar;
    for (int i = 0; i < segments; i++) bar += (i < filled) ? "█" : "░";
    int pct = (int)std::round(frac * 100.0);
    return bar + " " + std::to_string(pct) + "%";
}
