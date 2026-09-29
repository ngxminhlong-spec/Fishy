// fishadmin — command-line admin tool for FishBot.
//
// Talks directly to the same SQLite database the bot uses (fishbot.sqlite3 by
// default), so it works whether the bot is running or not. Deliberately has zero
// dependency on DPP/Discord — it only needs Database.cpp, FishData.cpp and Game.cpp,
// none of which touch the network.
//
// Usage:
//   fishadmin give coin <user_id> <amount>
//   fishadmin take coin <user_id> <amount>
//   fishadmin give xp   <user_id> <amount>
//   fishadmin give rod  <user_id> <tier>
//   fishadmin give bait <user_id> <bait_id>
//   fishadmin give boat <user_id> <tier>
//   fishadmin give pet  <user_id> <pet_id>
//   fishadmin give bag  <user_id> <fish_id> [count=1] [weight]
//   fishadmin take bag  <user_id> <fish_id> [count=1]
//   fishadmin info <user_id>
//   fishadmin list rods|bait|boats|pets|fish|giveables
//   fishadmin --db path/to/other.sqlite3 give coin 123456789 500
//
// "bag" is an alias for "fish" — both add/remove inventory items. `take` on rod/
// boat/pet resets that slot to nothing rather than needing an amount.

#include "Database.hpp"
#include "FishData.hpp"
#include "Game.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <functional>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdlib>

struct GiveResult {
    bool ok;
    std::string message;
};

using GiveFn = std::function<GiveResult(Database&, const std::string& user_id,
                                         const std::vector<std::string>& args, bool taking)>;

// ---------------- individual resource handlers ----------------
// Each of these is self-contained: validate args, mutate the DB via Database's
// public API, return a result. See give_registry() below for how a new one plugs in.

static GiveResult give_coin(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    if (args.empty()) return {false, "Usage: give coin <user_id> <amount>"};
    int64_t amount;
    try { amount = std::stoll(args[0]); } catch (...) { return {false, "Amount must be a whole number."}; }
    if (amount <= 0) return {false, "Amount must be positive."};
    UserRow u = db.get_or_create_user(uid);
    int64_t delta = taking ? -amount : amount;
    u.balance = std::max<int64_t>(0, u.balance + delta);
    db.save_user(u);
    std::ostringstream ss;
    ss << (taking ? "Took " : "Gave ") << amount << " coins " << (taking ? "from " : "to ") << uid
       << ". New balance: " << u.balance << " coins.";
    return {true, ss.str()};
}

static GiveResult give_xp(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    if (args.empty()) return {false, "Usage: give xp <user_id> <amount>"};
    int64_t amount;
    try { amount = std::stoll(args[0]); } catch (...) { return {false, "Amount must be a whole number."}; }
    if (amount <= 0) return {false, "Amount must be positive."};
    UserRow u = db.get_or_create_user(uid);
    u.xp = std::max<int64_t>(0, u.xp + (taking ? -amount : amount));
    int old_level = u.level;
    u.level = level_for_xp(u.xp);
    db.save_user(u);
    std::ostringstream ss;
    ss << (taking ? "Took " : "Gave ") << amount << " xp " << (taking ? "from " : "to ") << uid
       << ". Now level " << u.level << " (" << u.xp << " xp)"
       << (u.level != old_level ? " — level changed from " + std::to_string(old_level) + "!" : ".");
    return {true, ss.str()};
}

static GiveResult give_rod(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    UserRow u = db.get_or_create_user(uid);
    if (taking) {
        u.rod_tier = 0;
        db.save_user(u);
        return {true, "Reset " + uid + "'s rod back to the starting Twig Rod (tier 0)."};
    }
    if (args.empty()) return {false, "Usage: give rod <user_id> <tier 0-5>. See `fishadmin list rods`."};
    int tier;
    try { tier = std::stoi(args[0]); } catch (...) { return {false, "Tier must be a number."}; }
    const Rod* r = find_rod(tier);
    if (!r) return {false, "No rod tier " + std::to_string(tier) + ". See `fishadmin list rods`."};
    u.rod_tier = tier;
    db.save_user(u);
    return {true, "Gave " + uid + " the " + r->name + " (tier " + std::to_string(tier) + ", power " +
                   std::to_string(r->power) + ")."};
}

static GiveResult give_bait(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    UserRow u = db.get_or_create_user(uid);
    if (taking) {
        u.bait_id = -1;
        db.save_user(u);
        return {true, "Unequipped bait for " + uid + "."};
    }
    if (args.empty()) return {false, "Usage: give bait <user_id> <bait_id>. See `fishadmin list bait`."};
    int id;
    try { id = std::stoi(args[0]); } catch (...) { return {false, "bait_id must be a number."}; }
    const Bait* b = find_bait(id);
    if (!b) return {false, "No bait id " + std::to_string(id) + ". See `fishadmin list bait`."};
    u.bait_id = id;
    db.save_user(u);
    return {true, "Gave " + uid + " " + b->name + " and equipped it."};
}

static GiveResult give_boat(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    UserRow u = db.get_or_create_user(uid);
    if (taking) {
        u.boat_tier = -1;
        db.save_user(u);
        return {true, "Took " + uid + "'s boat away (shore-only now)."};
    }
    if (args.empty()) return {false, "Usage: give boat <user_id> <tier 0-2>. See `fishadmin list boats`."};
    int tier;
    try { tier = std::stoi(args[0]); } catch (...) { return {false, "Tier must be a number."}; }
    const Boat* b = find_boat(tier);
    if (!b) return {false, "No boat tier " + std::to_string(tier) + ". See `fishadmin list boats`."};
    u.boat_tier = tier;
    db.save_user(u);
    return {true, "Gave " + uid + " the " + b->name + " (tier " + std::to_string(tier) + ")."};
}

static GiveResult give_pet(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    UserRow u = db.get_or_create_user(uid);
    if (taking) {
        u.pet_id = -1;
        u.pet_level = 1;
        u.pet_xp = 0;
        db.save_user(u);
        return {true, "Removed " + uid + "'s pet."};
    }
    if (args.empty()) return {false, "Usage: give pet <user_id> <pet_id>. See `fishadmin list pets`."};
    int id;
    try { id = std::stoi(args[0]); } catch (...) { return {false, "pet_id must be a number."}; }
    const PetSpecies* p = find_pet(id);
    if (!p) return {false, "No pet id " + std::to_string(id) + ". See `fishadmin list pets`."};
    u.pet_id = id;
    u.pet_level = 1;
    u.pet_xp = 0;
    db.save_user(u);
    return {true, "Gave " + uid + " a " + p->name + " " + p->emoji + " companion."};
}

// "bag" == the player's fish inventory. This is the one to copy if you want to add
// a brand new kind of item later — everything else follows the same shape.
static GiveResult give_bag(Database& db, const std::string& uid, const std::vector<std::string>& args, bool taking) {
    if (args.empty()) return {false, "Usage: give bag <user_id> <fish_id> [count=1] [weight]. See `fishadmin list fish`."};
    int fish_id;
    try { fish_id = std::stoi(args[0]); } catch (...) { return {false, "fish_id must be a number."}; }
    const FishSpecies* f = find_fish(fish_id);
    if (!f) return {false, "No fish id " + std::to_string(fish_id) + ". See `fishadmin list fish`."};
    int count = 1;
    if (args.size() >= 2) {
        try { count = std::max(1, std::stoi(args[1])); } catch (...) { return {false, "count must be a number."}; }
    }

    if (taking) {
        auto inv = db.get_inventory(uid);
        int removed = 0;
        for (auto& item : inv) {
            if (removed >= count) break;
            if (item.fish_id == fish_id) {
                db.remove_inventory_item(item.inv_id);
                removed++;
            }
        }
        return {true, "Removed " + std::to_string(removed) + "x " + f->name + " from " + uid + "'s bag."};
    }

    double fixed_weight = -1.0;
    if (args.size() >= 3) {
        try { fixed_weight = std::stod(args[2]); } catch (...) { return {false, "weight must be a number."}; }
    }

    std::mt19937 gen{std::random_device{}()};
    std::uniform_real_distribution<double> wdist(f->min_weight, f->max_weight);
    for (int i = 0; i < count; i++) {
        double w = fixed_weight > 0 ? fixed_weight : std::round(wdist(gen) * 100.0) / 100.0;
        db.add_fish(uid, fish_id, w);
    }
    db.mark_discovered(uid, fish_id);
    return {true, "Added " + std::to_string(count) + "x " + f->name + " " + f->emoji + " to " + uid + "'s bag."};
}

// To add a brand new giveable resource: write one handler above in the same shape
// (Database&, user_id, args, taking) -> GiveResult, then add ONE line here. The CLI
// dispatch, `list giveables`, and error messages all key off this map automatically —
// nothing else in this file needs to change.
static const std::map<std::string, GiveFn>& give_registry() {
    static const std::map<std::string, GiveFn> R = {
        {"coin", give_coin},
        {"xp",   give_xp},
        {"rod",  give_rod},
        {"bait", give_bait},
        {"boat", give_boat},
        {"pet",  give_pet},
        {"fish", give_bag},
        {"bag",  give_bag}, // alias
    };
    return R;
}

// ---------------- listings ----------------

static void list_rods() {
    std::cout << "tier  name                 power  price\n";
    for (auto& r : all_rods())
        std::cout << r.tier << "     " << r.name << std::string(std::max<int>(1, 21 - (int)r.name.size()), ' ')
                   << r.power << "      " << r.price << "\n";
}

static void list_bait() {
    std::cout << "id  name          price\n";
    for (auto& b : all_bait())
        std::cout << b.id << "   " << b.name << std::string(std::max<int>(1, 14 - (int)b.name.size()), ' ') << b.price << "\n";
}

static void list_boats() {
    std::cout << "tier  name       price\n";
    for (auto& b : all_boats())
        std::cout << b.tier << "     " << b.name << std::string(std::max<int>(1, 11 - (int)b.name.size()), ' ') << b.price << "\n";
}

static void list_pets() {
    std::cout << "id  name              price\n";
    for (auto& p : all_pets())
        std::cout << p.id << "   " << p.emoji << " " << p.name << std::string(std::max<int>(1, 15 - (int)p.name.size()), ' ') << p.price << "\n";
}

static void list_fish() {
    std::cout << "id   name                     rarity      location            tags\n";
    for (auto& f : all_fish()) {
        std::string tags;
        if (f.boss_only) tags += "[boss] ";
        if (f.requires_tournament) tags += "[tournament] ";
        if (!f.required_weather.empty()) tags += "[weather:" + f.required_weather + "] ";
        std::string id_str = std::to_string(f.id);
        std::string rarity_str = rarity_name(f.rarity);
        std::cout << f.id << std::string(std::max<int>(1, 5 - (int)id_str.size()), ' ')
                   << f.name << std::string(std::max<int>(1, 25 - (int)f.name.size()), ' ')
                   << rarity_str << std::string(std::max<int>(1, 12 - (int)rarity_str.size()), ' ')
                   << f.location << std::string(std::max<int>(1, 20 - (int)f.location.size()), ' ')
                   << tags << "\n";
    }
}

static void list_giveables() {
    std::cout << "Giveable types (use with `give`/`take`):\n";
    for (auto& [name, fn] : give_registry()) {
        (void)fn;
        std::cout << "  " << name << "\n";
    }
}

// ---------------- info ----------------

static void print_info(Database& db, const std::string& uid) {
    UserRow u = db.get_or_create_user(uid);
    const Rod* rod = find_rod(u.rod_tier);
    const Boat* boat = u.boat_tier >= 0 ? find_boat(u.boat_tier) : nullptr;
    const Bait* bait = u.bait_id >= 0 ? find_bait(u.bait_id) : nullptr;
    const PetSpecies* pet = u.pet_id >= 0 ? find_pet(u.pet_id) : nullptr;

    std::cout << "User " << uid << "\n";
    std::cout << "  Balance:   " << u.balance << " coins\n";
    std::cout << "  Level:     " << u.level << " (" << u.xp << " xp)\n";
    std::cout << "  Location:  " << u.location << "\n";
    std::cout << "  Rod:       " << (rod ? rod->name : "none") << " (tier " << u.rod_tier << ")\n";
    std::cout << "  Boat:      " << (boat ? boat->name : "none") << "\n";
    std::cout << "  Bait:      " << (bait ? bait->name : "none") << "\n";
    std::cout << "  Pet:       " << (pet ? (pet->name + " (Lv." + std::to_string(u.pet_level) + ")") : "none") << "\n";
    std::cout << "  Crew id:   " << (u.crew_id >= 0 ? std::to_string(u.crew_id) : "none") << "\n";
    std::cout << "  Fish in bag: " << db.count_fish(uid) << " (heaviest: " << db.heaviest_catch(uid) << "kg)\n";
}

// ---------------- usage / main ----------------

static void print_usage() {
    std::cout <<
        "fishadmin — CLI admin tool for FishBot\n\n"
        "Usage:\n"
        "  fishadmin [--db path] give <type> <user_id> <value...>\n"
        "  fishadmin [--db path] take <type> <user_id> <value...>\n"
        "  fishadmin [--db path] info <user_id>\n"
        "  fishadmin [--db path] list <rods|bait|boats|pets|fish|giveables>\n\n"
        "Examples:\n"
        "  fishadmin give coin 123456789012345678 5000\n"
        "  fishadmin give rod  123456789012345678 3\n"
        "  fishadmin give bag  123456789012345678 27 2        # 2x Ghost Shark\n"
        "  fishadmin give bag  123456789012345678 4 1 5.5      # 1x Golden Koi, exactly 5.5kg\n"
        "  fishadmin take bag  123456789012345678 27 1\n"
        "  fishadmin take coin 123456789012345678 200\n"
        "  fishadmin info 123456789012345678\n\n"
        "Run `fishadmin list giveables` for every type `give`/`take` accepts.\n"
        "--db defaults to ./fishbot.sqlite3 (the same file the bot itself uses).\n";
}

int main(int argc, char** argv) {
    std::string db_path = "fishbot.sqlite3";
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--db" && i + 1 < argc) { db_path = argv[++i]; continue; }
        args.push_back(a);
    }

    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return args.empty() ? 1 : 0;
    }

    try {
        Database db(db_path);
        const std::string& cmd = args[0];

        if (cmd == "give" || cmd == "take") {
            if (args.size() < 3) {
                std::cerr << "Usage: " << cmd << " <type> <user_id> <value...>\n\n";
                list_giveables();
                return 1;
            }
            const std::string& type = args[1];
            const std::string& user_id = args[2];
            std::vector<std::string> rest(args.begin() + 3, args.end());
            auto& reg = give_registry();
            auto it = reg.find(type);
            if (it == reg.end()) {
                std::cerr << "Unknown type '" << type << "'.\n\n";
                list_giveables();
                return 1;
            }
            GiveResult res = it->second(db, user_id, rest, cmd == "take");
            std::cout << (res.ok ? "" : "Error: ") << res.message << "\n";
            return res.ok ? 0 : 1;
        }

        if (cmd == "info") {
            if (args.size() < 2) { std::cerr << "Usage: info <user_id>\n"; return 1; }
            print_info(db, args[1]);
            return 0;
        }

        if (cmd == "list") {
            if (args.size() < 2) { std::cerr << "Usage: list <rods|bait|boats|pets|fish|giveables>\n"; return 1; }
            const std::string& what = args[1];
            if (what == "rods") list_rods();
            else if (what == "bait") list_bait();
            else if (what == "boats") list_boats();
            else if (what == "pets") list_pets();
            else if (what == "fish") list_fish();
            else if (what == "giveables") list_giveables();
            else { std::cerr << "Unknown list target '" << what << "'.\n"; return 1; }
            return 0;
        }

        std::cerr << "Unknown command '" << cmd << "'.\n\n";
        print_usage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
