#include "Database.hpp"
#include "FishData.hpp"
#include <random>
#include <stdexcept>
#include <cstring>
#include <ctime>

static void check(int rc, sqlite3* db) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string("sqlite error: ") + sqlite3_errmsg(db));
    }
}

Database::Database(const std::string& path) : path_(path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("cannot open database");
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    // All in-process access already goes through mtx_, so this mainly guards
    // against contention with an external process (e.g. fishadmin) touching
    // the same file — cheap insurance, not expected to matter often.
    sqlite3_busy_timeout(db_, 5000);
    init_schema();
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec failed: " + msg + " | sql=" + sql);
    }
}

void Database::init_schema() {
    std::lock_guard<std::mutex> lock(mtx_);
    exec(R"SQL(
    CREATE TABLE IF NOT EXISTS users (
        user_id TEXT PRIMARY KEY,
        balance INTEGER NOT NULL DEFAULT 100,
        rod_tier INTEGER NOT NULL DEFAULT 0,
        bait_id INTEGER NOT NULL DEFAULT -1,
        boat_tier INTEGER NOT NULL DEFAULT -1,
        location TEXT NOT NULL DEFAULT 'Pond',
        xp INTEGER NOT NULL DEFAULT 0,
        level INTEGER NOT NULL DEFAULT 1,
        daily_streak INTEGER NOT NULL DEFAULT 0,
        last_daily INTEGER NOT NULL DEFAULT 0,
        last_cast INTEGER NOT NULL DEFAULT 0,
        pet_id INTEGER NOT NULL DEFAULT -1,
        pet_level INTEGER NOT NULL DEFAULT 1,
        pet_xp INTEGER NOT NULL DEFAULT 0,
        crew_id INTEGER NOT NULL DEFAULT -1
    );

    CREATE TABLE IF NOT EXISTS inventory (
        inv_id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id TEXT NOT NULL,
        fish_id INTEGER NOT NULL,
        weight REAL NOT NULL,
        in_aquarium INTEGER NOT NULL DEFAULT 0,
        caught_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS fishdex (
        user_id TEXT NOT NULL,
        fish_id INTEGER NOT NULL,
        PRIMARY KEY (user_id, fish_id)
    );

    CREATE TABLE IF NOT EXISTS achievements (
        user_id TEXT NOT NULL,
        key TEXT NOT NULL,
        unlocked_at INTEGER NOT NULL,
        PRIMARY KEY (user_id, key)
    );

    CREATE TABLE IF NOT EXISTS quests (
        user_id TEXT PRIMARY KEY,
        quest_key TEXT NOT NULL,
        progress INTEGER NOT NULL DEFAULT 0,
        target INTEGER NOT NULL DEFAULT 1,
        claimed INTEGER NOT NULL DEFAULT 0,
        assigned_at INTEGER NOT NULL DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS trades (
        trade_id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_a TEXT NOT NULL,
        user_b TEXT NOT NULL,
        offer_a TEXT NOT NULL DEFAULT '',
        offer_b TEXT NOT NULL DEFAULT '',
        a_confirmed INTEGER NOT NULL DEFAULT 0,
        b_confirmed INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS tournament (
        user_id TEXT PRIMARY KEY,
        score REAL NOT NULL DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS crews (
        crew_id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL UNIQUE,
        owner_id TEXT NOT NULL,
        bank INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS guild_settings (
        guild_id TEXT PRIMARY KEY,
        boss_channel_id TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE IF NOT EXISTS admin_queue (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        action TEXT NOT NULL,
        a1 TEXT NOT NULL DEFAULT '',
        a2 TEXT NOT NULL DEFAULT '',
        a3 TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE IF NOT EXISTS enchants (
        user_id TEXT NOT NULL,
        key TEXT NOT NULL,
        level INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (user_id, key)
    );

    CREATE TABLE IF NOT EXISTS auctions (
        auction_id INTEGER PRIMARY KEY AUTOINCREMENT,
        seller_id TEXT NOT NULL,
        fish_id INTEGER NOT NULL,
        weight REAL NOT NULL,
        price INTEGER NOT NULL,
        listed_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS lottery_tickets (
        user_id TEXT PRIMARY KEY,
        tickets INTEGER NOT NULL DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS lottery_state (
        id INTEGER PRIMARY KEY,
        pool INTEGER NOT NULL DEFAULT 0,
        draw_at INTEGER NOT NULL DEFAULT 0,
        last_winner TEXT NOT NULL DEFAULT '',
        last_prize INTEGER NOT NULL DEFAULT 0
    );
    INSERT OR IGNORE INTO lottery_state (id) VALUES (1);

    CREATE TABLE IF NOT EXISTS user_prefs (
        user_id TEXT PRIMARY KEY,
        lang TEXT NOT NULL DEFAULT ''
    );

    CREATE INDEX IF NOT EXISTS idx_auctions_seller ON auctions(seller_id);
    CREATE INDEX IF NOT EXISTS idx_inventory_user ON inventory(user_id);
    CREATE INDEX IF NOT EXISTS idx_users_crew ON users(crew_id);
    )SQL");
}

// ---------------- Users ----------------

UserRow Database::get_or_create_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT balance,rod_tier,bait_id,boat_tier,location,xp,level,"
        "daily_streak,last_daily,last_cast,pet_id,pet_level,pet_xp,crew_id FROM users WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    UserRow u;
    u.user_id = user_id;
    if (sqlite3_step(st) == SQLITE_ROW) {
        u.balance = sqlite3_column_int64(st, 0);
        u.rod_tier = sqlite3_column_int(st, 1);
        u.bait_id = sqlite3_column_int(st, 2);
        u.boat_tier = sqlite3_column_int(st, 3);
        u.location = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        u.xp = sqlite3_column_int64(st, 5);
        u.level = sqlite3_column_int(st, 6);
        u.daily_streak = sqlite3_column_int(st, 7);
        u.last_daily = sqlite3_column_int64(st, 8);
        u.last_cast = sqlite3_column_int64(st, 9);
        u.pet_id = sqlite3_column_int(st, 10);
        u.pet_level = sqlite3_column_int(st, 11);
        u.pet_xp = sqlite3_column_int64(st, 12);
        u.crew_id = sqlite3_column_int(st, 13);
        sqlite3_finalize(st);
    } else {
        sqlite3_finalize(st);
        sqlite3_stmt* ins;
        sqlite3_prepare_v2(db_, "INSERT INTO users (user_id) VALUES (?)", -1, &ins, nullptr);
        sqlite3_bind_text(ins, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_finalize(ins);
    }
    return u;
}

void Database::save_user(const UserRow& u) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE users SET balance=?,rod_tier=?,bait_id=?,boat_tier=?,location=?,"
        "xp=?,level=?,daily_streak=?,last_daily=?,last_cast=?,pet_id=?,pet_level=?,pet_xp=?,crew_id=? WHERE user_id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, u.balance);
    sqlite3_bind_int(st, 2, u.rod_tier);
    sqlite3_bind_int(st, 3, u.bait_id);
    sqlite3_bind_int(st, 4, u.boat_tier);
    sqlite3_bind_text(st, 5, u.location.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, u.xp);
    sqlite3_bind_int(st, 7, u.level);
    sqlite3_bind_int(st, 8, u.daily_streak);
    sqlite3_bind_int64(st, 9, u.last_daily);
    sqlite3_bind_int64(st, 10, u.last_cast);
    sqlite3_bind_int(st, 11, u.pet_id);
    sqlite3_bind_int(st, 12, u.pet_level);
    sqlite3_bind_int64(st, 13, u.pet_xp);
    sqlite3_bind_int(st, 14, u.crew_id);
    sqlite3_bind_text(st, 15, u.user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<UserRow> Database::top_by_balance(int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<UserRow> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT user_id,balance FROM users ORDER BY balance DESC LIMIT ?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        UserRow u;
        u.user_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        u.balance = sqlite3_column_int64(st, 1);
        out.push_back(u);
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<UserRow> Database::top_by_fish_count(int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<UserRow> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_,
        "SELECT user_id, COUNT(*) as c FROM inventory GROUP BY user_id ORDER BY c DESC LIMIT ?",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        UserRow u;
        u.user_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        u.xp = sqlite3_column_int64(st, 1); // reused field to carry the count
        out.push_back(u);
    }
    sqlite3_finalize(st);
    return out;
}

// ---------------- Inventory ----------------

int Database::add_fish(const std::string& user_id, int fish_id, double weight) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO inventory (user_id,fish_id,weight,caught_at) VALUES (?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, fish_id);
    sqlite3_bind_double(st, 3, weight);
    sqlite3_bind_int64(st, 4, (int64_t)time(nullptr));
    sqlite3_step(st);
    int id = (int)sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(st);
    return id;
}

std::vector<InventoryItem> Database::get_inventory(const std::string& user_id, bool aquarium_only) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<InventoryItem> out;
    const char* sql = aquarium_only
        ? "SELECT inv_id,fish_id,weight,in_aquarium,caught_at FROM inventory WHERE user_id=? AND in_aquarium=1 ORDER BY caught_at DESC"
        : "SELECT inv_id,fish_id,weight,in_aquarium,caught_at FROM inventory WHERE user_id=? ORDER BY caught_at DESC";
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        InventoryItem it;
        it.inv_id = sqlite3_column_int(st, 0);
        it.user_id = user_id;
        it.fish_id = sqlite3_column_int(st, 1);
        it.weight = sqlite3_column_double(st, 2);
        it.in_aquarium = sqlite3_column_int(st, 3) != 0;
        it.caught_at = sqlite3_column_int64(st, 4);
        out.push_back(it);
    }
    sqlite3_finalize(st);
    return out;
}

std::optional<InventoryItem> Database::get_inventory_item(int inv_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT user_id,fish_id,weight,in_aquarium,caught_at FROM inventory WHERE inv_id=?",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, inv_id);
    std::optional<InventoryItem> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        InventoryItem it;
        it.inv_id = inv_id;
        it.user_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        it.fish_id = sqlite3_column_int(st, 1);
        it.weight = sqlite3_column_double(st, 2);
        it.in_aquarium = sqlite3_column_int(st, 3) != 0;
        it.caught_at = sqlite3_column_int64(st, 4);
        out = it;
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::remove_inventory_item(int inv_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "DELETE FROM inventory WHERE inv_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, inv_id);
    sqlite3_step(st);
    bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return changed;
}

bool Database::set_aquarium(int inv_id, bool value) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE inventory SET in_aquarium=? WHERE inv_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, value ? 1 : 0);
    sqlite3_bind_int(st, 2, inv_id);
    sqlite3_step(st);
    bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return changed;
}

int Database::count_fish(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM inventory WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    int c = 0;
    if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return c;
}

double Database::heaviest_catch(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT MAX(weight) FROM inventory WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    double w = 0;
    if (sqlite3_step(st) == SQLITE_ROW) w = sqlite3_column_double(st, 0);
    sqlite3_finalize(st);
    return w;
}

// ---------------- Fishdex ----------------

void Database::mark_discovered(const std::string& user_id, int fish_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT OR IGNORE INTO fishdex (user_id,fish_id) VALUES (?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, fish_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<int> Database::get_discovered(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<int> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT fish_id FROM fishdex WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(sqlite3_column_int(st, 0));
    sqlite3_finalize(st);
    return out;
}

// ---------------- Achievements ----------------

void Database::unlock_achievement(const std::string& user_id, const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT OR IGNORE INTO achievements (user_id,key,unlocked_at) VALUES (?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (int64_t)time(nullptr));
    sqlite3_step(st);
    sqlite3_finalize(st);
}

bool Database::has_achievement(const std::string& user_id, const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT 1 FROM achievements WHERE user_id=? AND key=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key.c_str(), -1, SQLITE_TRANSIENT);
    bool has = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return has;
}

std::vector<std::string> Database::get_achievements(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT key FROM achievements WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
    sqlite3_finalize(st);
    return out;
}

// ---------------- Quests ----------------

std::optional<Database::QuestRow> Database::get_active_quest(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT quest_key,progress,target,claimed,assigned_at FROM quests WHERE user_id=?",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<QuestRow> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        QuestRow q;
        q.user_id = user_id;
        q.quest_key = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        q.progress = sqlite3_column_int(st, 1);
        q.target = sqlite3_column_int(st, 2);
        q.claimed = sqlite3_column_int(st, 3) != 0;
        q.assigned_at = sqlite3_column_int64(st, 4);
        out = q;
    }
    sqlite3_finalize(st);
    return out;
}

void Database::set_quest(const QuestRow& q) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO quests (user_id,quest_key,progress,target,claimed,assigned_at) "
        "VALUES (?,?,?,?,?,?) ON CONFLICT(user_id) DO UPDATE SET "
        "quest_key=excluded.quest_key,progress=excluded.progress,target=excluded.target,"
        "claimed=excluded.claimed,assigned_at=excluded.assigned_at", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, q.user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, q.quest_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, q.progress);
    sqlite3_bind_int(st, 4, q.target);
    sqlite3_bind_int(st, 5, q.claimed ? 1 : 0);
    sqlite3_bind_int64(st, 6, q.assigned_at);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Database::bump_quest_progress(const std::string& user_id, const std::string& quest_key, int amount) {
    auto q = get_active_quest(user_id);
    if (!q || q->quest_key != quest_key || q->claimed) return;
    q->progress += amount;
    if (q->progress > q->target) q->progress = q->target;
    set_quest(*q);
}

// ---------------- Trades ----------------

int Database::create_trade(const std::string& a, const std::string& b) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO trades (user_a,user_b,created_at) VALUES (?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, b.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (int64_t)time(nullptr));
    sqlite3_step(st);
    int id = (int)sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(st);
    return id;
}

std::optional<PendingTrade> Database::get_trade(int trade_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT user_a,user_b,offer_a,offer_b,a_confirmed,b_confirmed,created_at "
        "FROM trades WHERE trade_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, trade_id);
    std::optional<PendingTrade> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        PendingTrade t;
        t.trade_id = trade_id;
        t.user_a = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        t.user_b = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        t.offer_a = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        t.offer_b = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        t.a_confirmed = sqlite3_column_int(st, 4) != 0;
        t.b_confirmed = sqlite3_column_int(st, 5) != 0;
        t.created_at = sqlite3_column_int64(st, 6);
        out = t;
    }
    sqlite3_finalize(st);
    return out;
}

void Database::update_trade(const PendingTrade& t) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE trades SET offer_a=?,offer_b=?,a_confirmed=?,b_confirmed=? WHERE trade_id=?",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, t.offer_a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, t.offer_b.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, t.a_confirmed ? 1 : 0);
    sqlite3_bind_int(st, 4, t.b_confirmed ? 1 : 0);
    sqlite3_bind_int(st, 5, t.trade_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Database::delete_trade(int trade_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "DELETE FROM trades WHERE trade_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, trade_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

// ---------------- Tournament ----------------

void Database::tournament_add_score(const std::string& user_id, double weight) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO tournament (user_id,score) VALUES (?,?) "
        "ON CONFLICT(user_id) DO UPDATE SET score=score+excluded.score", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 2, weight);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<std::pair<std::string,double>> Database::tournament_leaderboard(int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::pair<std::string,double>> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT user_id,score FROM tournament ORDER BY score DESC LIMIT ?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)),
                          sqlite3_column_double(st, 1));
    }
    sqlite3_finalize(st);
    return out;
}

void Database::tournament_reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    exec("DELETE FROM tournament;");
}

// ---------------- Crews ----------------

std::optional<int> Database::create_crew(const std::string& name, const std::string& owner_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO crews (name,owner_id,created_at) VALUES (?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, owner_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (int64_t)time(nullptr));
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return std::nullopt; // most likely a UNIQUE constraint failure (name taken)
    return (int)sqlite3_last_insert_rowid(db_);
}

std::optional<CrewRow> Database::get_crew(int crew_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT name,owner_id,bank,created_at FROM crews WHERE crew_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, crew_id);
    std::optional<CrewRow> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        CrewRow c;
        c.crew_id = crew_id;
        c.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        c.owner_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        c.bank = sqlite3_column_int64(st, 2);
        c.created_at = sqlite3_column_int64(st, 3);
        out = c;
    }
    sqlite3_finalize(st);
    return out;
}

std::optional<CrewRow> Database::get_crew_by_name(const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT crew_id,owner_id,bank,created_at FROM crews WHERE name=? COLLATE NOCASE",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<CrewRow> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        CrewRow c;
        c.crew_id = sqlite3_column_int(st, 0);
        c.name = name;
        c.owner_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        c.bank = sqlite3_column_int64(st, 2);
        c.created_at = sqlite3_column_int64(st, 3);
        out = c;
    }
    sqlite3_finalize(st);
    return out;
}

bool Database::join_crew(const std::string& user_id, int crew_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE users SET crew_id=? WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, crew_id);
    sqlite3_bind_text(st, 2, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    bool changed = sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return changed;
}

void Database::leave_crew(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE users SET crew_id=-1 WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<std::string> Database::get_crew_member_ids(int crew_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT user_id FROM users WHERE crew_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, crew_id);
    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
    sqlite3_finalize(st);
    return out;
}

int Database::count_crew_members(int crew_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM users WHERE crew_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, crew_id);
    int c = 0;
    if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return c;
}

void Database::adjust_crew_bank(int crew_id, int64_t delta) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE crews SET bank=bank+? WHERE crew_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, delta);
    sqlite3_bind_int(st, 2, crew_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Database::set_crew_owner(int crew_id, const std::string& new_owner_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "UPDATE crews SET owner_id=? WHERE crew_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, new_owner_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, crew_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Database::delete_crew(int crew_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st1;
    sqlite3_prepare_v2(db_, "UPDATE users SET crew_id=-1 WHERE crew_id=?", -1, &st1, nullptr);
    sqlite3_bind_int(st1, 1, crew_id);
    sqlite3_step(st1);
    sqlite3_finalize(st1);

    sqlite3_stmt* st2;
    sqlite3_prepare_v2(db_, "DELETE FROM crews WHERE crew_id=?", -1, &st2, nullptr);
    sqlite3_bind_int(st2, 1, crew_id);
    sqlite3_step(st2);
    sqlite3_finalize(st2);
}

std::vector<CrewRow> Database::top_crews_by_bank(int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<CrewRow> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT crew_id,name,owner_id,bank,created_at FROM crews ORDER BY bank DESC LIMIT ?",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        CrewRow c;
        c.crew_id = sqlite3_column_int(st, 0);
        c.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        c.owner_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        c.bank = sqlite3_column_int64(st, 3);
        c.created_at = sqlite3_column_int64(st, 4);
        out.push_back(c);
    }
    sqlite3_finalize(st);
    return out;
}

// ---------------- Guild (Discord server) settings ----------------

void Database::set_boss_channel(const std::string& guild_id, const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO guild_settings (guild_id,boss_channel_id) VALUES (?,?) "
        "ON CONFLICT(guild_id) DO UPDATE SET boss_channel_id=excluded.boss_channel_id", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, guild_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, channel_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::optional<std::string> Database::get_boss_channel(const std::string& guild_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT boss_channel_id FROM guild_settings WHERE guild_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, guild_id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        std::string v = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        if (!v.empty()) out = v;
    }
    sqlite3_finalize(st);
    return out;
}

void Database::clear_boss_channel(const std::string& guild_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "DELETE FROM guild_settings WHERE guild_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, guild_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Database::enqueue_admin_action(const std::string& action, const std::string& a1,
                                    const std::string& a2, const std::string& a3) {
    std::lock_guard<std::mutex> lock(mtx_);
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "INSERT INTO admin_queue (action,a1,a2,a3) VALUES (?,?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, a1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, a2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, a3.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<AdminAction> Database::take_admin_actions() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<AdminAction> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT id,action,a1,a2,a3 FROM admin_queue ORDER BY id", -1, &st, nullptr);
    auto text = [&](int col) {
        const unsigned char* t = sqlite3_column_text(st, col);
        return std::string(t ? reinterpret_cast<const char*>(t) : "");
    };
    while (sqlite3_step(st) == SQLITE_ROW) {
        AdminAction a;
        a.id = sqlite3_column_int64(st, 0);
        a.action = text(1); a.a1 = text(2); a.a2 = text(3); a.a3 = text(4);
        out.push_back(std::move(a));
    }
    sqlite3_finalize(st);
    if (!out.empty()) {
        // ids only grow, so this removes exactly what we read; newer rows wait for the next poll
        std::string sql = "DELETE FROM admin_queue WHERE id <= " + std::to_string(out.back().id);
        sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr);
    }
    return out;
}

std::vector<std::pair<std::string,std::string>> Database::all_boss_channels() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::pair<std::string,std::string>> out;
    sqlite3_stmt* st;
    sqlite3_prepare_v2(db_, "SELECT guild_id,boss_channel_id FROM guild_settings WHERE boss_channel_id != ''",
        -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)),
                          reinterpret_cast<const char*>(sqlite3_column_text(st, 1)));
    }
    sqlite3_finalize(st);
    return out;
}

// ======================= Enchants, auctions, lottery =======================
// Every multi-step operation below runs inside one SQLite transaction (and under mtx_),
// so coins and fish can never be duplicated or lost halfway.

namespace {

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &st_, nullptr); }
    ~Stmt() { if (st_) sqlite3_finalize(st_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    Stmt& text(int i, const std::string& v) { sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
    Stmt& i64(int i, int64_t v) { sqlite3_bind_int64(st_, i, v); return *this; }
    Stmt& dbl(int i, double v) { sqlite3_bind_double(st_, i, v); return *this; }
    bool step() { return sqlite3_step(st_) == SQLITE_ROW; }   // true while a row is available
    void run() { sqlite3_step(st_); }
    int64_t col_i64(int c) { return sqlite3_column_int64(st_, c); }
    double col_dbl(int c) { return sqlite3_column_double(st_, c); }
    std::string col_text(int c) {
        const unsigned char* t = sqlite3_column_text(st_, c);
        return std::string(t ? reinterpret_cast<const char*>(t) : "");
    }
private:
    sqlite3_stmt* st_ = nullptr;
};

class Tx {
public:
    explicit Tx(sqlite3* db) : db_(db), ok_(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK) {}
    ~Tx() { if (ok_ && !done_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    bool ok() const { return ok_; }
    void commit() { sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr); done_ = true; }
private:
    sqlite3* db_;
    bool ok_;
    bool done_ = false;
};

const char* const BUSY_MSG = "The database is busy right now - try again in a moment.";

int64_t scalar_i64(sqlite3* db, const char* sql, const std::string& a, int64_t fallback = 0) {
    Stmt s(db, sql);
    s.text(1, a);
    return s.step() ? s.col_i64(0) : fallback;
}

void give_fish_back(sqlite3* db, const std::string& user, int fish_id, double weight, int64_t now) {
    Stmt s(db, "INSERT INTO inventory (user_id,fish_id,weight,caught_at) VALUES (?,?,?,?)");
    s.text(1, user).i64(2, fish_id).dbl(3, weight).i64(4, now);
    s.run();
}

bool add_balance(sqlite3* db, const std::string& user, int64_t delta) {
    Stmt s(db, "UPDATE users SET balance=balance+? WHERE user_id=?");
    s.i64(1, delta).text(2, user);
    s.run();
    return sqlite3_changes(db) > 0;
}

} // namespace

// ---------------- Enchants ----------------

Enchants Database::get_enchants(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    Enchants e;
    Stmt s(db_, "SELECT key,level FROM enchants WHERE user_id=?");
    s.text(1, user_id);
    while (s.step()) {
        std::string k = s.col_text(0);
        int lv = (int)s.col_i64(1);
        if (k == "lucky") e.lucky = lv;
        else if (k == "swift") e.swift = lv;
        else if (k == "greedy") e.greedy = lv;
        else if (k == "magnetic") e.magnetic = lv;
    }
    return e;
}

DbResult Database::upgrade_enchant(const std::string& user_id, const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    DbResult r;
    const EnchantDef* def = find_enchant(key);
    if (!def) { r.error = "Unknown enchant."; return r; }
    Tx tx(db_);
    if (!tx.ok()) { r.error = BUSY_MSG; return r; }

    int level = 0;
    {
        Stmt s(db_, "SELECT level FROM enchants WHERE user_id=? AND key=?");
        s.text(1, user_id).text(2, key);
        if (s.step()) level = (int)s.col_i64(0);
    }
    if (level >= def->max_level) { r.error = def->name + " is already at max level."; return r; }

    int64_t cost = enchant_cost(*def, level + 1);
    int64_t balance = scalar_i64(db_, "SELECT balance FROM users WHERE user_id=?", user_id, -1);
    if (balance < 0) { r.error = "Player not found."; return r; }
    if (balance < cost) { r.amount = cost; r.error = "not_enough_coins"; return r; }

    if (!add_balance(db_, user_id, -cost)) { r.error = "Player not found."; return r; }
    Stmt up(db_, "INSERT INTO enchants (user_id,key,level) VALUES (?,?,?) "
                 "ON CONFLICT(user_id,key) DO UPDATE SET level=excluded.level");
    up.text(1, user_id).text(2, key).i64(3, level + 1);
    up.run();
    tx.commit();
    r.ok = true; r.amount = cost; r.id = level + 1;
    return r;
}

void Database::set_enchant_level(const std::string& user_id, const std::string& key, int level) {
    std::lock_guard<std::mutex> lock(mtx_);
    const EnchantDef* def = find_enchant(key);
    if (!def) return;
    level = std::max(0, std::min(level, def->max_level));
    Stmt up(db_, "INSERT INTO enchants (user_id,key,level) VALUES (?,?,?) "
                 "ON CONFLICT(user_id,key) DO UPDATE SET level=excluded.level");
    up.text(1, user_id).text(2, key).i64(3, level);
    up.run();
}

// ---------------- Auction house ----------------

DbResult Database::auction_create(const std::string& seller_id, int inv_id, int64_t price) {
    std::lock_guard<std::mutex> lock(mtx_);
    DbResult r;
    if (price < 1 || price > AUCTION_MAX_PRICE) {
        r.error = "Price must be between 1 and " + std::to_string(AUCTION_MAX_PRICE) + " coins.";
        return r;
    }
    Tx tx(db_);
    if (!tx.ok()) { r.error = BUSY_MSG; return r; }

    if (scalar_i64(db_, "SELECT COUNT(*) FROM auctions WHERE seller_id=?", seller_id) >= AUCTION_MAX_LISTINGS) {
        r.error = "You already have " + std::to_string(AUCTION_MAX_LISTINGS) + " active listings (the maximum).";
        return r;
    }

    int fish_id = 0; double weight = 0; bool in_aq = false;
    {
        Stmt s(db_, "SELECT fish_id,weight,in_aquarium FROM inventory WHERE inv_id=? AND user_id=?");
        s.i64(1, inv_id).text(2, seller_id);
        if (!s.step()) { r.error = "You don't own that inventory item."; return r; }
        fish_id = (int)s.col_i64(0); weight = s.col_dbl(1); in_aq = s.col_i64(2) != 0;
    }
    if (in_aq) { r.error = "That fish is on display in your aquarium - remove it first with `/aquarium remove`."; return r; }

    { Stmt d(db_, "DELETE FROM inventory WHERE inv_id=?"); d.i64(1, inv_id); d.run(); }
    {
        Stmt ins(db_, "INSERT INTO auctions (seller_id,fish_id,weight,price,listed_at) VALUES (?,?,?,?,?)");
        ins.text(1, seller_id).i64(2, fish_id).dbl(3, weight).i64(4, price).i64(5, (int64_t)time(nullptr));
        ins.run();
    }
    r.id = (int)sqlite3_last_insert_rowid(db_);
    tx.commit();
    r.ok = true; r.amount = price; r.fish_id = fish_id; r.weight = weight;
    return r;
}

DbResult Database::auction_buy(const std::string& buyer_id, int auction_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    DbResult r;
    Tx tx(db_);
    if (!tx.ok()) { r.error = BUSY_MSG; return r; }

    std::string seller; int fish_id = 0; double weight = 0; int64_t price = 0;
    {
        Stmt s(db_, "SELECT seller_id,fish_id,weight,price FROM auctions WHERE auction_id=?");
        s.i64(1, auction_id);
        if (!s.step()) { r.error = "That listing no longer exists (already sold, cancelled or expired)."; return r; }
        seller = s.col_text(0); fish_id = (int)s.col_i64(1); weight = s.col_dbl(2); price = s.col_i64(3);
    }
    if (seller == buyer_id) { r.error = "That's your own listing - use `/auction cancel` to take it back."; return r; }

    int64_t balance = scalar_i64(db_, "SELECT balance FROM users WHERE user_id=?", buyer_id, -1);
    if (balance < 0) { r.error = "Player not found."; return r; }
    if (balance < price) { r.amount = price; r.error = "not_enough_coins"; return r; }

    int64_t tax = price * AUCTION_TAX_PERCENT / 100;
    if (!add_balance(db_, buyer_id, -price)) { r.error = "Player not found."; return r; }
    if (!add_balance(db_, seller, price - tax)) { r.error = "The seller's account is missing - purchase cancelled."; return r; }
    give_fish_back(db_, buyer_id, fish_id, weight, (int64_t)time(nullptr));
    { Stmt d(db_, "DELETE FROM auctions WHERE auction_id=?"); d.i64(1, auction_id); d.run(); }
    tx.commit();

    r.ok = true; r.amount = price; r.tax = tax; r.id = auction_id;
    r.fish_id = fish_id; r.weight = weight; r.other_user = seller;
    return r;
}

DbResult Database::auction_cancel(const std::string& user_id, int auction_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    DbResult r;
    Tx tx(db_);
    if (!tx.ok()) { r.error = BUSY_MSG; return r; }

    std::string seller; int fish_id = 0; double weight = 0;
    {
        Stmt s(db_, "SELECT seller_id,fish_id,weight FROM auctions WHERE auction_id=?");
        s.i64(1, auction_id);
        if (!s.step()) { r.error = "That listing no longer exists."; return r; }
        seller = s.col_text(0); fish_id = (int)s.col_i64(1); weight = s.col_dbl(2);
    }
    if (seller != user_id) { r.error = "That listing isn't yours."; return r; }

    give_fish_back(db_, user_id, fish_id, weight, (int64_t)time(nullptr));
    { Stmt d(db_, "DELETE FROM auctions WHERE auction_id=?"); d.i64(1, auction_id); d.run(); }
    tx.commit();
    r.ok = true; r.id = auction_id; r.fish_id = fish_id; r.weight = weight;
    return r;
}

static AuctionRow read_auction_row(Stmt& s) {
    AuctionRow a;
    a.auction_id = (int)s.col_i64(0);
    a.seller_id = s.col_text(1);
    a.fish_id = (int)s.col_i64(2);
    a.weight = s.col_dbl(3);
    a.price = s.col_i64(4);
    a.listed_at = s.col_i64(5);
    return a;
}

std::vector<AuctionRow> Database::auction_list(int limit, int offset) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<AuctionRow> out;
    Stmt s(db_, "SELECT auction_id,seller_id,fish_id,weight,price,listed_at FROM auctions "
                "ORDER BY listed_at DESC, auction_id DESC LIMIT ? OFFSET ?");
    s.i64(1, limit).i64(2, offset);
    while (s.step()) out.push_back(read_auction_row(s));
    return out;
}

std::vector<AuctionRow> Database::auction_by_seller(const std::string& seller_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<AuctionRow> out;
    Stmt s(db_, "SELECT auction_id,seller_id,fish_id,weight,price,listed_at FROM auctions "
                "WHERE seller_id=? ORDER BY listed_at DESC");
    s.text(1, seller_id);
    while (s.step()) out.push_back(read_auction_row(s));
    return out;
}

int Database::auction_count() {
    std::lock_guard<std::mutex> lock(mtx_);
    Stmt s(db_, "SELECT COUNT(*) FROM auctions");
    return s.step() ? (int)s.col_i64(0) : 0;
}

int Database::auction_expire_older_than(int64_t cutoff_ts) {
    std::lock_guard<std::mutex> lock(mtx_);
    Tx tx(db_);
    if (!tx.ok()) return 0;
    std::vector<AuctionRow> expired;
    {
        Stmt s(db_, "SELECT auction_id,seller_id,fish_id,weight,price,listed_at FROM auctions WHERE listed_at < ?");
        s.i64(1, cutoff_ts);
        while (s.step()) expired.push_back(read_auction_row(s));
    }
    int64_t now = (int64_t)time(nullptr);
    for (auto& a : expired) {
        give_fish_back(db_, a.seller_id, a.fish_id, a.weight, now);
        Stmt d(db_, "DELETE FROM auctions WHERE auction_id=?");
        d.i64(1, a.auction_id);
        d.run();
    }
    tx.commit();
    return (int)expired.size();
}

// ---------------- Lottery ----------------

// Caller holds mtx_. Schedules the first draw lazily so a fresh install doesn't draw instantly.
static int64_t lottery_ensure_draw_time(sqlite3* db) {
    int64_t draw_at = 0;
    { Stmt s(db, "SELECT draw_at FROM lottery_state WHERE id=1"); if (s.step()) draw_at = s.col_i64(0); }
    if (draw_at == 0) {
        draw_at = (int64_t)time(nullptr) + LOTTERY_INTERVAL_SECONDS;
        Stmt u(db, "UPDATE lottery_state SET draw_at=? WHERE id=1");
        u.i64(1, draw_at);
        u.run();
    }
    return draw_at;
}

LotteryState Database::lottery_get(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    LotteryState st;
    st.draw_at = lottery_ensure_draw_time(db_);
    {
        Stmt s(db_, "SELECT pool,last_winner,last_prize FROM lottery_state WHERE id=1");
        if (s.step()) { st.pool = s.col_i64(0); st.last_winner = s.col_text(1); st.last_prize = s.col_i64(2); }
    }
    {
        Stmt s(db_, "SELECT COALESCE(SUM(tickets),0) FROM lottery_tickets");
        if (s.step()) st.total_tickets = (int)s.col_i64(0);
    }
    st.my_tickets = (int)scalar_i64(db_, "SELECT tickets FROM lottery_tickets WHERE user_id=?", user_id);
    return st;
}

DbResult Database::lottery_buy(const std::string& user_id, int count) {
    std::lock_guard<std::mutex> lock(mtx_);
    DbResult r;
    if (count < 1) { r.error = "Buy at least 1 ticket."; return r; }
    Tx tx(db_);
    if (!tx.ok()) { r.error = BUSY_MSG; return r; }
    lottery_ensure_draw_time(db_);

    int have = (int)scalar_i64(db_, "SELECT tickets FROM lottery_tickets WHERE user_id=?", user_id);
    if (have + count > LOTTERY_MAX_TICKETS) {
        r.error = "Max " + std::to_string(LOTTERY_MAX_TICKETS) + " tickets per round - you have " +
                  std::to_string(have) + ".";
        return r;
    }
    int64_t cost = (int64_t)count * LOTTERY_TICKET_PRICE;
    int64_t balance = scalar_i64(db_, "SELECT balance FROM users WHERE user_id=?", user_id, -1);
    if (balance < 0) { r.error = "Player not found."; return r; }
    if (balance < cost) { r.amount = cost; r.error = "not_enough_coins"; return r; }

    add_balance(db_, user_id, -cost);
    { Stmt p(db_, "UPDATE lottery_state SET pool=pool+? WHERE id=1"); p.i64(1, cost); p.run(); }
    {
        Stmt t(db_, "INSERT INTO lottery_tickets (user_id,tickets) VALUES (?,?) "
                    "ON CONFLICT(user_id) DO UPDATE SET tickets=tickets+excluded.tickets");
        t.text(1, user_id).i64(2, count);
        t.run();
    }
    tx.commit();
    r.ok = true; r.amount = cost; r.id = have + count;
    return r;
}

LotteryDraw Database::lottery_draw(int64_t now, bool force) {
    std::lock_guard<std::mutex> lock(mtx_);
    LotteryDraw d;
    Tx tx(db_);
    if (!tx.ok()) return d;

    int64_t draw_at = lottery_ensure_draw_time(db_);
    if (!force && now < draw_at) return d;

    int64_t pool = 0;
    { Stmt s(db_, "SELECT pool FROM lottery_state WHERE id=1"); if (s.step()) pool = s.col_i64(0); }

    std::vector<std::pair<std::string,int64_t>> entries;
    int64_t total = 0;
    {
        Stmt s(db_, "SELECT user_id,tickets FROM lottery_tickets WHERE tickets>0 ORDER BY user_id");
        while (s.step()) { entries.emplace_back(s.col_text(0), s.col_i64(1)); total += entries.back().second; }
    }

    int64_t next_draw = now + LOTTERY_INTERVAL_SECONDS;
    if (total == 0) {   // nobody played: keep the pot, just schedule the next round
        Stmt u(db_, "UPDATE lottery_state SET draw_at=? WHERE id=1");
        u.i64(1, next_draw);
        u.run();
        tx.commit();
        return d;
    }

    static std::mt19937_64 gen{std::random_device{}()};
    int64_t pick = (int64_t)(gen() % (uint64_t)total);
    std::string winner = entries.back().first;
    for (auto& e : entries) {
        if (pick < e.second) { winner = e.first; break; }
        pick -= e.second;
    }

    int64_t prize = pool * LOTTERY_PAYOUT_PERCENT / 100;
    add_balance(db_, winner, prize);
    sqlite3_exec(db_, "DELETE FROM lottery_tickets", nullptr, nullptr, nullptr);
    {
        Stmt u(db_, "UPDATE lottery_state SET pool=0, draw_at=?, last_winner=?, last_prize=? WHERE id=1");
        u.i64(1, next_draw).text(2, winner).i64(3, prize);
        u.run();
    }
    tx.commit();

    d.drawn = true; d.winner_id = winner; d.prize = prize;
    d.total_tickets = (int)total; d.participants = (int)entries.size();
    return d;
}

// ======================= Language preference, stats, privacy erase =======================

std::string Database::get_lang(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    Stmt s(db_, "SELECT lang FROM user_prefs WHERE user_id=?");
    s.text(1, user_id);
    return s.step() ? s.col_text(0) : std::string();
}

void Database::set_lang(const std::string& user_id, const std::string& lang) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (lang.empty()) {
        Stmt d(db_, "DELETE FROM user_prefs WHERE user_id=?");
        d.text(1, user_id);
        d.run();
        return;
    }
    Stmt s(db_, "INSERT INTO user_prefs (user_id, lang) VALUES (?, ?) "
                "ON CONFLICT(user_id) DO UPDATE SET lang=excluded.lang");
    s.text(1, user_id).text(2, lang);
    s.run();
}

std::vector<std::pair<std::string,int64_t>> Database::table_row_counts() {
    std::lock_guard<std::mutex> lock(mtx_);
    // Fixed whitelist: table names are never taken from user input.
    static const char* const tables[] = {
        "users", "inventory", "fishdex", "achievements", "quests", "trades", "tournament",
        "crews", "guild_settings", "enchants", "auctions", "lottery_tickets", "user_prefs", "admin_queue"
    };
    std::vector<std::pair<std::string,int64_t>> out;
    for (const char* t : tables) {
        std::string sql = std::string("SELECT COUNT(*) FROM ") + t;
        Stmt s(db_, sql.c_str());
        out.emplace_back(t, s.step() ? s.col_i64(0) : 0);
    }
    return out;
}

Database::DbInfo Database::db_info() {
    std::lock_guard<std::mutex> lock(mtx_);
    DbInfo i;
    { Stmt s(db_, "PRAGMA page_size");      if (s.step()) i.page_size = s.col_i64(0); }
    { Stmt s(db_, "PRAGMA page_count");     if (s.step()) i.page_count = s.col_i64(0); }
    { Stmt s(db_, "PRAGMA freelist_count"); if (s.step()) i.freelist_count = s.col_i64(0); }
    i.sqlite_mem_used = sqlite3_memory_used();
    i.sqlite_version = sqlite3_libversion();
    return i;
}

int Database::erase_user_data(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    Tx tx(db_);
    if (!tx.ok()) return -1;

    int removed = 0;
    auto del = [&](const char* sql, int binds) {
        Stmt s(db_, sql);
        for (int i = 1; i <= binds; i++) s.text(i, user_id);
        s.run();
        removed += sqlite3_changes(db_);
    };

    // A crew this user owns: hand it to another member, or delete it if they were alone.
    {
        Stmt s(db_, "SELECT c.crew_id FROM crews c WHERE c.owner_id=?");
        s.text(1, user_id);
        std::vector<int64_t> owned;
        while (s.step()) owned.push_back(s.col_i64(0));
        for (int64_t crew_id : owned) {
            Stmt m(db_, "SELECT user_id FROM users WHERE crew_id=? AND user_id!=? ORDER BY user_id LIMIT 1");
            m.i64(1, crew_id).text(2, user_id);
            if (m.step()) {
                std::string heir = m.col_text(0);
                Stmt u(db_, "UPDATE crews SET owner_id=? WHERE crew_id=?");
                u.text(1, heir).i64(2, crew_id);
                u.run();
            } else {
                Stmt dc(db_, "DELETE FROM crews WHERE crew_id=?");
                dc.i64(1, crew_id);
                dc.run();
                removed += sqlite3_changes(db_);
            }
        }
    }

    del("DELETE FROM inventory WHERE user_id=?", 1);
    del("DELETE FROM fishdex WHERE user_id=?", 1);
    del("DELETE FROM achievements WHERE user_id=?", 1);
    del("DELETE FROM quests WHERE user_id=?", 1);
    del("DELETE FROM tournament WHERE user_id=?", 1);
    del("DELETE FROM enchants WHERE user_id=?", 1);
    del("DELETE FROM auctions WHERE seller_id=?", 1);        // listed fish go with the account
    del("DELETE FROM lottery_tickets WHERE user_id=?", 1);
    del("DELETE FROM user_prefs WHERE user_id=?", 1);
    del("DELETE FROM trades WHERE user_a=? OR user_b=?", 2);
    del("DELETE FROM users WHERE user_id=?", 1);

    // The lottery remembers the last winner's ID; blank it if that was this user.
    {
        Stmt s(db_, "UPDATE lottery_state SET last_winner='' WHERE last_winner=?");
        s.text(1, user_id);
        s.run();
        removed += sqlite3_changes(db_);
    }

    tx.commit();
    return removed;
}
