#pragma once
#include "FishData.hpp"
#include "Database.hpp"
#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <random>

enum class Weather { Sunny, Rainy, Stormy, Foggy, Windy };

const char* weather_name(Weather w);
const char* weather_emoji(Weather w);
// Multiplier applied to bite chance and rare-spawn weight for this weather.
double weather_bite_mult(Weather w);
double weather_rare_mult(Weather w);

// Global, mutable game state shared across the process (weather + active tournament).
class WorldState {
public:
    static WorldState& instance();

    Weather weather() const { return weather_; }
    void set_weather(Weather w) { weather_ = w; }
    void randomize_weather();

    bool tournament_active() const { return tournament_active_; }
    std::string tournament_location() const { return tournament_location_; }
    int64_t tournament_ends_at() const { return tournament_ends_at_; }
    void start_tournament(const std::string& location, int minutes);
    void end_tournament();

    // Seasonal / limited-time event (e.g. "halloween") — gates FishSpecies::event_key.
    // Admin-controlled via /event, duration in hours since these tend to run for days.
    bool event_active() const { return event_active_; }
    std::string event_key() const { return event_key_; }
    std::string event_name() const { return event_name_; }
    int64_t event_ends_at() const { return event_ends_at_; }
    void start_event(const std::string& key, const std::string& display_name, int hours);
    void end_event();

private:
    WorldState() = default;
    Weather weather_ = Weather::Sunny;
    bool tournament_active_ = false;
    std::string tournament_location_;
    int64_t tournament_ends_at_ = 0;
    bool event_active_ = false;
    std::string event_key_;
    std::string event_name_;
    int64_t event_ends_at_ = 0;
};

// Result of a single /fish cast attempt (before the reel minigame resolves).
struct CastOutcome {
    bool bit = false;               // did anything bite at all?
    const FishSpecies* fish = nullptr;
    double weight = 0.0;
    int reels_required = 0;
};

// Determine whether a fish bites, and if so, which species + weight.
// Caller supplies the user's rod tier, equipped bait id (-1 none), location, and pet bonus.
CastOutcome resolve_cast(int rod_tier, int bait_id, const std::string& location, double pet_bonus);

// Market: sell price fluctuates per-day per-fish using a deterministic pseudo-random
// multiplier seeded off the calendar day, so all users see the same prices.
double market_multiplier(int fish_id);
int market_sell_price(const FishSpecies& fish, double weight);

// XP / leveling
int xp_for_catch(const FishSpecies& fish);
int level_for_xp(int64_t xp);
int64_t xp_needed_for_level(int level);

// Daily quest pool
struct QuestDef {
    std::string key;
    std::string description;
    int target;
    int reward_coins;
};
const std::vector<QuestDef>& quest_pool();
const QuestDef* find_quest(const std::string& key);
const QuestDef& random_quest();

// Achievement definitions (key -> human text), checked opportunistically after catches.
struct AchievementDef {
    std::string key;
    std::string name;
    std::string description;
};
const std::vector<AchievementDef>& achievement_pool();
// Returns newly unlocked achievement names (checks fish count / heaviest / rarity caught).
std::vector<std::string> check_achievements(Database& db, const std::string& user_id,
                                             const FishSpecies* just_caught, double weight);

// --- Reel minigame session tracking (in-memory, per Discord message) ---
struct ReelSession {
    std::string user_id;
    const FishSpecies* fish;
    double weight;
    int rod_power;
    int reels_needed;
    int reels_done = 0;
    int64_t expires_at; // unix seconds
    bool tournament_catch = false;
};

class ReelSessionManager {
public:
    static ReelSessionManager& instance();
    void put(const std::string& message_id, ReelSession session);
    std::optional<ReelSession> get(const std::string& message_id);
    void update(const std::string& message_id, const ReelSession& session);
    void erase(const std::string& message_id);
private:
    std::mutex mtx_;
    std::map<std::string, ReelSession> sessions_;
};

std::mt19937& rng();

// ============================= World Boss =============================

// A boss fight is shared by everyone in one Discord server (guild) at a time.
struct BossSession {
    std::string guild_id;
    std::string channel_id;
    const FishSpecies* fish;
    double weight;
    double total_hp;
    double current_hp;
    std::map<std::string, double> contributions;  // user_id -> total damage dealt
    std::map<std::string, int64_t> last_hit;       // user_id -> unix seconds of last click (per-user cooldown)
    int64_t expires_at;
};

class BossSessionManager {
public:
    static BossSessionManager& instance();
    bool has_active(const std::string& guild_id);
    void put(const std::string& guild_id, BossSession session);
    std::optional<BossSession> get(const std::string& guild_id);
    void update(const std::string& guild_id, const BossSession& session);
    void erase(const std::string& guild_id);
    std::vector<std::string> active_guild_ids(); // for the expiry-sweep timer
private:
    std::mutex mtx_;
    std::map<std::string, BossSession> sessions_;
};

// Picks a random boss-exclusive species and computes its starting HP pool.
const FishSpecies* pick_boss_fish();
double boss_hp_for(const FishSpecies& fish);

// Damage a single Reel click deals, given the clicker's rod tier and pet bonus.
double boss_damage_per_hit(int rod_tier, double pet_bonus);

// Renders a simple text health bar, e.g. "██████░░░░ 62%".
std::string render_health_bar(double current, double total, int segments = 10);

