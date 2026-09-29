#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <cstdint>

// One row per Discord user. Everything the economy needs lives here or in
// child tables (inventory, cooldowns, achievements, quests).
struct UserRow {
    std::string user_id;
    int64_t balance = 100;          // starting coins
    int rod_tier = 0;               // index into ROD table (0 = Twig Rod)
    int bait_id = -1;                // -1 = no bait equipped
    int boat_tier = -1;              // -1 = no boat (shore only)
    std::string location = "Pond";
    int64_t xp = 0;
    int level = 1;
    int daily_streak = 0;
    int64_t last_daily = 0;          // unix seconds
    int64_t last_cast = 0;           // unix seconds, cast cooldown
    int pet_id = -1;
    int pet_level = 1;
    int64_t pet_xp = 0;
    int crew_id = -1;               // -1 = not in a crew
};

struct InventoryItem {
    int inv_id;
    std::string user_id;
    int fish_id;
    double weight;
    bool in_aquarium = false;
    int64_t caught_at = 0;
};

struct PendingTrade {
    int trade_id;
    std::string user_a, user_b;
    std::string offer_a; // serialized "coins:INT;fish:id,id,id"
    std::string offer_b;
    bool a_confirmed = false;
    bool b_confirmed = false;
    int64_t created_at = 0;
};

struct CrewRow {
    int crew_id;
    std::string name;
    std::string owner_id;
    int64_t bank = 0;
    int64_t created_at = 0;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    void init_schema();

    // --- Users ---
    UserRow get_or_create_user(const std::string& user_id);
    void save_user(const UserRow& u);
    std::vector<UserRow> top_by_balance(int limit);
    std::vector<UserRow> top_by_fish_count(int limit);

    // --- Inventory ---
    int add_fish(const std::string& user_id, int fish_id, double weight);
    std::vector<InventoryItem> get_inventory(const std::string& user_id, bool aquarium_only = false);
    std::optional<InventoryItem> get_inventory_item(int inv_id);
    bool remove_inventory_item(int inv_id);
    bool set_aquarium(int inv_id, bool value);
    int count_fish(const std::string& user_id);
    double heaviest_catch(const std::string& user_id);

    // --- Fishdex (discovered species) ---
    void mark_discovered(const std::string& user_id, int fish_id);
    std::vector<int> get_discovered(const std::string& user_id);

    // --- Achievements ---
    void unlock_achievement(const std::string& user_id, const std::string& key);
    bool has_achievement(const std::string& user_id, const std::string& key);
    std::vector<std::string> get_achievements(const std::string& user_id);

    // --- Quests ---
    struct QuestRow {
        std::string user_id;
        std::string quest_key;
        int progress = 0;
        int target = 0;
        bool claimed = false;
        int64_t assigned_at = 0;
    };
    std::optional<QuestRow> get_active_quest(const std::string& user_id);
    void set_quest(const QuestRow& q);
    void bump_quest_progress(const std::string& user_id, const std::string& quest_key, int amount);

    // --- Trades ---
    int create_trade(const std::string& a, const std::string& b);
    std::optional<PendingTrade> get_trade(int trade_id);
    void update_trade(const PendingTrade& t);
    void delete_trade(int trade_id);

    // --- Tournament ---
    void tournament_add_score(const std::string& user_id, double weight);
    std::vector<std::pair<std::string,double>> tournament_leaderboard(int limit);
    void tournament_reset();

    // --- Crews (fishing clans) ---
    std::optional<int> create_crew(const std::string& name, const std::string& owner_id);
    std::optional<CrewRow> get_crew(int crew_id);
    std::optional<CrewRow> get_crew_by_name(const std::string& name);
    bool join_crew(const std::string& user_id, int crew_id);
    void leave_crew(const std::string& user_id);
    std::vector<std::string> get_crew_member_ids(int crew_id);
    int count_crew_members(int crew_id);
    void adjust_crew_bank(int crew_id, int64_t delta);
    void set_crew_owner(int crew_id, const std::string& new_owner_id);
    void delete_crew(int crew_id);
    std::vector<CrewRow> top_crews_by_bank(int limit);

    // --- Per-Discord-server settings (boss spawn channel) ---
    void set_boss_channel(const std::string& guild_id, const std::string& channel_id);
    std::optional<std::string> get_boss_channel(const std::string& guild_id);
    std::vector<std::pair<std::string,std::string>> all_boss_channels(); // (guild_id, channel_id)
    // Removes this guild's row from guild_settings entirely (e.g. when the bot
    // auto-leaves a disallowed server). Safe to call even if no row exists.
    void clear_boss_channel(const std::string& guild_id);

    sqlite3* raw() { return db_; }

private:
    sqlite3* db_ = nullptr;
    std::mutex mtx_;
    void exec(const std::string& sql);
};
