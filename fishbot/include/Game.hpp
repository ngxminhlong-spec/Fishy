#pragma once
#include "FishData.hpp"
#include "Database.hpp"
#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <random>

enum class Weather { Sunny, Rainy, Stormy, Foggy, Windy };

const char* weather_name(Weather w);  // English - matched against FishSpecies::required_weather, don't localize this one
const char* weather_emoji(Weather w);
// Multiplier applied to bite chance and rare-spawn weight for this weather.
double weather_bite_mult(Weather w);
double weather_rare_mult(Weather w);

// Weather, tournament and event state shared by the whole process.
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

    // Seasonal event, matched against FishSpecies::event_key. Started from the CLI, runs for hours.
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

// What one cast turned up, before the reel minigame starts.
struct CastOutcome {
    bool bit = false;               // did anything bite at all?
    const FishSpecies* fish = nullptr;
    double weight = 0.0;
    int reels_required = 0;
};

// Rolls bite chance, then species and weight. bait_id -1 means no bait.
CastOutcome resolve_cast(int rod_tier, int bait_id, const std::string& location, double pet_bonus,
                         const Enchants& ench = Enchants{});

// Greedy Hook: +4% per level on a sale.
int apply_sell_bonus(int price, int greedy_level);

// Daily price swing per species. Seeded by the date, so everyone sees the same market.
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

// Achievements are checked after each catch.
struct AchievementDef {
    std::string key;
    std::string name;
    std::string description;
};
const std::vector<AchievementDef>& achievement_pool();
// Returns the names of anything unlocked just now.
std::vector<std::string> check_achievements(Database& db, const std::string& user_id,
                                             const FishSpecies* just_caught, double weight);

// Live reel minigames, keyed by the session id baked into the button.
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
    void sweep(int64_t now);
private:
    std::mutex mtx_;
    std::map<std::string, ReelSession> sessions_;
};

std::mt19937& rng();

// One boss fight per server at a time.
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
    std::string gif;                               // picked once at spawn so the fight keeps its picture
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

// Random boss species, and its HP.
const FishSpecies* pick_boss_fish();
double boss_hp_for(const FishSpecies& fish);

// Damage per Attack click.
double boss_damage_per_hit(int rod_tier, double pet_bonus);

// Text health bar like "██████░░░░ 62%".
std::string render_health_bar(double current, double total, int segments = 10);

