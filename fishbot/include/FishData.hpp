#pragma once
#include <string>
#include <vector>

enum class Rarity { Common, Uncommon, Rare, Epic, Legendary, Mythic };

const char* rarity_name(Rarity r);
const char* rarity_emoji(Rarity r);

struct FishSpecies {
    int id;
    std::string name;
    std::string emoji;
    Rarity rarity;
    std::string location;      // which Location::name this fish appears in
    double min_weight;
    double max_weight;
    int base_value;            // coins per kg baseline, before market fluctuation
    int strength;               // how many successful reels needed vs rod power
    double spawn_weight;        // relative chance within its location's pool
    std::string required_weather = ""; // "" = any weather; else must match weather_name()
    bool requires_tournament = false;   // only spawns while a tournament is active at this location
    bool boss_only = false;             // never appears from normal /fish casts — only as a World Boss reward
    std::string event_key = "";         // "" = always eligible; else only spawns while /event start matches this key
};

struct Location {
    std::string name;
    std::string emoji;
    int min_boat_tier;   // -1 = no boat needed
    int min_rod_tier;    // minimum rod tier allowed to fish here at all
    std::string desc;
};

struct Rod {
    int tier;
    std::string name;
    int power;          // reel strength
    int price;
    double rare_bonus;  // multiplier applied to rare+ spawn weights
};

// Permanent per-player upgrades bought with coins (/enchant). Levels are stored in the DB.
struct EnchantDef {
    std::string key;        // "lucky", "swift", "greedy", "magnetic"
    std::string name;
    std::string emoji;
    std::string desc;       // what each level does
    int max_level;
    int64_t base_cost;      // cost of level 1; each next level costs 3x the previous
};
const std::vector<EnchantDef>& all_enchants();
const EnchantDef* find_enchant(const std::string& key);
int64_t enchant_cost(const EnchantDef& e, int target_level);

struct Bait {
    int id;
    std::string name;
    int price;
    double bite_bonus;   // multiplier to bite chance
    double rare_bonus;   // multiplier to rare+ spawn weights
    std::string location_hint; // "" = any
};

struct Boat {
    int tier;
    std::string name;
    int price;
};

struct PetSpecies {
    int id;
    std::string name;
    std::string emoji;
    int price;
    double catch_bonus;  // added to bite chance
    double value_bonus;  // multiplier to sell value
};

const std::vector<FishSpecies>& all_fish();
const FishSpecies* find_fish(int id);
std::vector<const FishSpecies*> fish_for_location(const std::string& location);

const std::vector<Location>& all_locations();
const Location* find_location(const std::string& name);

const std::vector<Rod>& all_rods();
const Rod* find_rod(int tier);

const std::vector<Bait>& all_bait();
const Bait* find_bait(int id);

const std::vector<Boat>& all_boats();
const Boat* find_boat(int tier);

const std::vector<PetSpecies>& all_pets();
const PetSpecies* find_pet(int id);
