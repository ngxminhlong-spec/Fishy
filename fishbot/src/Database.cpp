#include "Database.hpp"
#include <stdexcept>
#include <cstring>
#include <ctime>

static void check(int rc, sqlite3* db) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string("sqlite error: ") + sqlite3_errmsg(db));
    }
}

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("cannot open database");
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
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
