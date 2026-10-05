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

// ---------- Enchants, auctions, lottery ----------
constexpr int64_t LOTTERY_TICKET_PRICE = 500;
constexpr int     LOTTERY_MAX_TICKETS = 25;               // per player per round
constexpr int     LOTTERY_INTERVAL_SECONDS = 24 * 3600;   // one draw per day
constexpr int     LOTTERY_PAYOUT_PERCENT = 90;            // the rest is removed from the economy
constexpr int     AUCTION_MAX_LISTINGS = 10;              // per seller
constexpr int     AUCTION_DURATION_SECONDS = 48 * 3600;   // unsold fish go back to the seller
constexpr int     AUCTION_TAX_PERCENT = 5;                // taken from the seller's proceeds
constexpr int64_t AUCTION_MAX_PRICE = 1000000000;

// Per-player enchant levels (permanent upgrades bought with /enchant).
struct Enchants { int lucky = 0, swift = 0, greedy = 0, magnetic = 0; };

struct AuctionRow {
    int auction_id = 0;
    std::string seller_id;
    int fish_id = 0;
    double weight = 0;
    int64_t price = 0;
    int64_t listed_at = 0;
};

// Outcome of an atomic multi-step operation. `error` is a user-facing message when !ok.
struct DbResult {
    bool ok = false;
    std::string error;
    int64_t amount = 0;       // coins paid / cost
    int64_t tax = 0;
    int id = 0;               // auction id, new enchant level, total tickets...
    int fish_id = -1;
    double weight = 0;
    std::string other_user;   // e.g. the seller
};

struct LotteryState {
    int64_t pool = 0;
    int64_t draw_at = 0;
    std::string last_winner;
    int64_t last_prize = 0;
    int total_tickets = 0;
    int my_tickets = 0;
};

struct LotteryDraw {
    bool drawn = false;
    std::string winner_id;
    int64_t prize = 0;
    int total_tickets = 0;
    int participants = 0;
};

// A pending admin action queued by the fishadmin CLI for the running bot to apply.
// Tournaments / events / boss fights live in the bot's memory, so the CLI can't touch
// them directly - it drops a row here and the bot picks it up within a few seconds.
struct AdminAction {
    int64_t id = 0;
    std::string action, a1, a2, a3;
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

    // --- Enchants ---
    Enchants get_enchants(const std::string& user_id);
    DbResult upgrade_enchant(const std::string& user_id, const std::string& key); // pays the coin cost
    void set_enchant_level(const std::string& user_id, const std::string& key, int level); // admin

    // --- Auction house (fish are held in escrow while listed) ---
    DbResult auction_create(const std::string& seller_id, int inv_id, int64_t price);
    DbResult auction_buy(const std::string& buyer_id, int auction_id);
    DbResult auction_cancel(const std::string& user_id, int auction_id);
    std::vector<AuctionRow> auction_list(int limit, int offset);
    std::vector<AuctionRow> auction_by_seller(const std::string& seller_id);
    int auction_count();
    int auction_expire_older_than(int64_t cutoff_ts); // returns how many listings went back to their sellers

    // --- Lottery (one global pot) ---
    LotteryState lottery_get(const std::string& user_id);
    DbResult lottery_buy(const std::string& user_id, int count);
    LotteryDraw lottery_draw(int64_t now, bool force);

    // --- Admin action queue (written by fishadmin, consumed by the bot) ---
    void enqueue_admin_action(const std::string& action, const std::string& a1 = "",
                              const std::string& a2 = "", const std::string& a3 = "");
    std::vector<AdminAction> take_admin_actions(); // returns + deletes all pending rows, oldest first

    // --- Language preference (per user; "" = not chosen, follow the Discord client language) ---
    std::string get_lang(const std::string& user_id);
    void set_lang(const std::string& user_id, const std::string& lang); // "" clears the preference

    // --- Statistics (for /stats) ---
    const std::string& path() const { return path_; }
    // Row count of every game table, in a fixed display order.
    std::vector<std::pair<std::string,int64_t>> table_row_counts();
    struct DbInfo {
        int64_t page_size = 0, page_count = 0, freelist_count = 0;
        int64_t sqlite_mem_used = 0;
        std::string sqlite_version;
    };
    DbInfo db_info();

    // --- Privacy: permanently erase everything stored about one Discord user ---
    // Returns the number of rows removed, or -1 if the database was busy (nothing changed).
    int erase_user_data(const std::string& user_id);

    sqlite3* raw() { return db_; }

private:
    sqlite3* db_ = nullptr;
    std::string path_;
    std::mutex mtx_;
    void exec(const std::string& sql);
};
