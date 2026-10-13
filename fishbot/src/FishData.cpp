#include "FishData.hpp"
#include "I18n.hpp"
#include <algorithm>

std::string tr_rarity(Lang l, Rarity r) {
    switch (r) {
        case Rarity::Common:    return tr(l, "rarity.common");
        case Rarity::Uncommon:  return tr(l, "rarity.uncommon");
        case Rarity::Rare:      return tr(l, "rarity.rare");
        case Rarity::Epic:      return tr(l, "rarity.epic");
        case Rarity::Legendary: return tr(l, "rarity.legendary");
        case Rarity::Mythic:    return tr(l, "rarity.mythic");
    }
    return "?";
}

const char* rarity_name(Rarity r) {
    switch (r) {
        case Rarity::Common: return "Common";
        case Rarity::Uncommon: return "Uncommon";
        case Rarity::Rare: return "Rare";
        case Rarity::Epic: return "Epic";
        case Rarity::Legendary: return "Legendary";
        case Rarity::Mythic: return "Mythic";
    }
    return "?";
}

const char* rarity_emoji(Rarity r) {
    switch (r) {
        case Rarity::Common: return "⚪";
        case Rarity::Uncommon: return "🟢";
        case Rarity::Rare: return "🔵";
        case Rarity::Epic: return "🟣";
        case Rarity::Legendary: return "🟠";
        case Rarity::Mythic: return "🔴";
    }
    return "?";
}

const std::vector<Location>& all_locations() {
    static const std::vector<Location> L = {
        {"Pond",           "🪷", -1, 0, "A calm starter pond. Anyone can fish here."},
        {"River",          "🏞️", -1, 1, "Flowing water, needs at least a Bamboo Rod."},
        {"Lake",           "🌊", -1, 1, "A deep freshwater lake."},
        {"Ocean",          "🌅", 0, 2, "Open sea - requires a Rowboat or better."},
        {"Deep Sea",       "🌑", 1, 3, "Crushing depths - requires a Yacht or better."},
        {"Volcanic Vents", "🌋", 2, 5, "Superheated vents - requires a Submarine and a Void Rod."},
        {"Abyssal Trench", "🕳️", 2, 6, "The bottom of the world - requires a Submarine and an Abyssal Rod or better."},
        {"Frozen Fjord",   "🧊", 0, 3, "Ice-choked water under the aurora. Rowboat and a Carbon Fiber Rod."},
        {"Starfall Reef",  "🌠", 3, 7, "Where meteors land. Needs a Bathyscaphe and a Celestial Rod."},
    };
    return L;
}

const Location* find_location(const std::string& name) {
    for (auto& l : all_locations()) if (l.name == name) return &l;
    return nullptr;
}

const std::vector<EnchantDef>& all_enchants() {
    static const std::vector<EnchantDef> E = {
        {"lucky",    "Lucky Lure",    "🍀", "+8% chance weight for Rare+ fish per level",        5, 3000},
        {"swift",    "Swift Reel",    "⚡", "+12% reel power per level (fewer reels needed)",     5, 2500},
        {"greedy",   "Greedy Hook",   "💰", "+4% sell price on `/sell` per level",                5, 4000},
        {"magnetic", "Magnetic Bait", "🧲", "+2.5% bite chance per level",                        5, 2000},
    };
    return E;
}

const EnchantDef* find_enchant(const std::string& key) {
    for (auto& e : all_enchants()) if (e.key == key) return &e;
    return nullptr;
}

int64_t enchant_cost(const EnchantDef& e, int target_level) {
    int64_t cost = e.base_cost;
    for (int i = 1; i < target_level; i++) cost *= 3;
    return cost;
}

const std::vector<Rod>& all_rods() {
    static const std::vector<Rod> R = {
        {0, "Twig Rod",        2,  0,     1.0},
        {1, "Bamboo Rod",      4,  150,   1.1},
        {2, "Steel Rod",       7,  500,   1.2},
        {3, "Carbon Fiber Rod",11, 1500,  1.5},
        {4, "Ancient Rod",     16, 5000,  2.0},
        {5, "Void Rod",        24, 20000, 3.0},
        {6, "Abyssal Rod",     34, 60000, 3.6},
        {7, "Celestial Rod",   48, 150000, 4.5},
        {8, "Primordial Rod",  70, 400000, 6.0},
        {9, "Eclipse Rod",     95, 900000, 7.5},
        {10, "Worldroot Rod",  130, 2500000, 9.0},
        {11, "Genesis Rod",    180, 8000000, 12.0},
    };
    return R;
}

const Rod* find_rod(int tier) {
    for (auto& r : all_rods()) if (r.tier == tier) return &r;
    return nullptr;
}

const std::vector<Bait>& all_bait() {
    static const std::vector<Bait> B = {
        {0, "Worms",       10,  1.10, 1.0, ""},
        {1, "Shrimp",      25,  1.20, 1.1, ""},
        {2, "Leeches",     60,  1.30, 1.3, ""},
        {3, "Magic Lure",  200, 1.50, 1.8, ""},
        {4, "Golden Lure", 750, 1.80, 2.5, ""},
        {5, "Glow Worms",    120,   1.40, 1.4, ""},
        {6, "Squid Strips",  400,   1.60, 2.1, ""},
        {7, "Starlight Gel", 1500,  2.00, 3.0, ""},
        {8, "Abyssal Chum",  4000,  2.20, 3.6, ""},
        {9, "Mythril Lure",  12000, 2.50, 4.5, ""},
    };
    return B;
}

const Bait* find_bait(int id) {
    for (auto& b : all_bait()) if (b.id == id) return &b;
    return nullptr;
}

const std::vector<Boat>& all_boats() {
    static const std::vector<Boat> BO = {
        {0, "Rowboat",   1000},
        {1, "Yacht",     5000},
        {2, "Submarine", 20000},
        {3, "Bathyscaphe", 80000},
    };
    return BO;
}

const Boat* find_boat(int tier) {
    for (auto& b : all_boats()) if (b.tier == tier) return &b;
    return nullptr;
}

const std::vector<PetSpecies>& all_pets() {
    static const std::vector<PetSpecies> P = {
        {0, "Baby Crab",       "🦀", 500,   0.02, 1.00},
        {1, "Otter Pal",       "🦦", 1500,  0.05, 1.05},
        {2, "Seagull Scout",   "🕊️", 4000,  0.08, 1.10},
        {3, "Lucky Koi",       "🐟", 10000, 0.12, 1.20},
        {4, "Kraken Hatchling","🐙", 30000, 0.20, 1.35},
        {5, "Moon Jelly",      "🪼", 60000,  0.26, 1.45},
        {6, "Pocket Dragonet", "🐲", 150000, 0.32, 1.60},
        {7, "Star Whale Pup",  "🐋", 400000, 0.40, 1.80},
    };
    return P;
}

const PetSpecies* find_pet(int id) {
    for (auto& p : all_pets()) if (p.id == id) return &p;
    return nullptr;
}

const std::vector<FishSpecies>& all_fish() {
    static const std::vector<FishSpecies> F = {
        // --- Pond ---
        {0,  "Minnow",         "🐟", Rarity::Common,    "Pond", 0.05, 0.3,  3,   1,  6.7},
        {1,  "Bluegill",       "🐠", Rarity::Common,    "Pond", 0.2,  0.8,  5,   2,  6.7},
        {30, "Tadpole",        "🫐", Rarity::Common,    "Pond", 0.01, 0.05, 2,   1,  6.7},
        {31, "Goldfish",       "🐠", Rarity::Common,    "Pond", 0.1,  0.4,  6,   2,  6.7},
        {67, "Old Boot",       "🥾", Rarity::Common,    "Pond", 0.5,  1.5,  1,   1,  6.7},
        {68, "Water Strider",  "🦟", Rarity::Common,    "Pond", 0.01, 0.05, 2,   1,  6.7},
        {2,  "Pond Frog",      "🐸", Rarity::Uncommon,  "Pond", 0.05, 0.2,  12,  3,  8},
        {32, "Pond Turtle",    "🐢", Rarity::Uncommon,  "Pond", 0.5,  2.0,  16,  4,  8},
        {69, "Duckling",       "🐥", Rarity::Uncommon,  "Pond", 0.3,  0.6,  14,  3,  8},
        {3,  "Pond Carp",      "🎏", Rarity::Rare,      "Pond", 1.0,  4.0,  40,  6,  4.7},
        {33, "Snapping Turtle","🐢", Rarity::Rare,      "Pond", 2.0,  8.0,  55,  7,  4.7},
        {70, "Bullfrog King",  "🐸", Rarity::Rare,      "Pond", 0.5,  2.0,  48,  7,  4.7},
        {34, "Ancient Pond Eel","🐍",Rarity::Epic,      "Pond", 1.0,  3.0,  220, 12, 4},
        {71, "Pond Guardian",  "🛡️", Rarity::Epic,      "Pond", 3.0,  8.0,  260, 13, 4},
        {4,  "Golden Koi",     "🟡", Rarity::Legendary, "Pond", 2.0,  6.0,  400, 14, 4},
        {58, "Starlit Koi",    "✨", Rarity::Legendary, "Pond", 3.0,  6.0,  950, 18, 2, "Foggy"},

        // --- River ---
        {5,  "Trout",          "🐟", Rarity::Common,    "River", 0.3, 1.5,  8,   3,  10},
        {35, "Chub",           "🐟", Rarity::Common,    "River", 0.2, 1.0,  7,   2,  10},
        {72, "Rusty Can",      "🥫", Rarity::Common,    "River", 0.1, 0.3,  1,   1,  10},
        {73, "Water Skipper",  "🪲", Rarity::Common,    "River", 0.05,0.2,  6,   2,  10},
        {6,  "Catfish",        "🐡", Rarity::Uncommon,  "River", 1.0, 6.0,  20,  5,  8},
        {36, "Grayling",       "🐠", Rarity::Uncommon,  "River", 0.5, 2.0,  22,  5,  8},
        {74, "Rainbow Trout",  "🌈", Rarity::Uncommon,  "River", 0.5, 2.0,  24,  5,  8},
        {7,  "Salmon",         "🐟", Rarity::Rare,      "River", 2.0, 8.0,  60,  8,  7},
        {37, "Sockeye Salmon", "🐟", Rarity::Rare,      "River", 2.0, 7.0,  70,  8,  7},
        {8,  "River Pike",     "🐊", Rarity::Epic,      "River", 3.0, 10.0, 150, 11, 2.7},
        {38, "River Otterfish","🦦", Rarity::Epic,      "River", 2.0, 6.0,  170, 12, 2.7},
        {75, "River Guardian Pike","🐊",Rarity::Epic,   "River", 4.0, 12.0, 190, 13, 2.7},
        {39, "Diamond Trout",  "💎", Rarity::Legendary, "River", 1.0, 3.0,  500, 16, 2},
        {76, "Crystal Eel",    "💎", Rarity::Legendary, "River", 1.0, 3.0,  550, 17, 2},
        {9,  "Silver Eel",     "🐍", Rarity::Mythic,    "River", 1.0, 3.0,  900, 18, 2},
        {59, "Storm Salmon",   "⚡", Rarity::Epic,      "River", 3.0, 9.0,  380, 14, 3, "Stormy"},

        // --- Lake ---
        {10, "Perch",          "🐟", Rarity::Common,    "Lake", 0.2, 1.0,  6,   2,  10},
        {40, "Crappie",        "🐟", Rarity::Common,    "Lake", 0.2, 0.9,  6,   2,  10},
        {77, "Old Tire",       "🛞", Rarity::Common,    "Lake", 1.0, 3.0,  1,   1,  10},
        {78, "Lake Minnow",    "🐟", Rarity::Common,    "Lake", 0.1, 0.5,  5,   2,  10},
        {11, "Largemouth Bass","🐠", Rarity::Uncommon,  "Lake", 1.0, 4.0,  18,  5,  8},
        {41, "Walleye",        "🐠", Rarity::Uncommon,  "Lake", 1.0, 5.0,  20,  5,  8},
        {79, "Spotted Bass",   "🐠", Rarity::Uncommon,  "Lake", 1.0, 3.0,  19,  5,  8},
        {12, "Lake Sturgeon",  "🐋", Rarity::Rare,      "Lake", 5.0, 20.0, 80,  10, 4.7},
        {42, "Northern Pike",  "🐊", Rarity::Rare,      "Lake", 3.0, 12.0, 85,  10, 4.7},
        {80, "Lake Dragonfly Larva","🦟",Rarity::Rare,  "Lake", 0.2, 0.8,  60,  9,  4.7},
        {13, "Muskellunge",    "🐟", Rarity::Epic,      "Lake", 5.0, 15.0, 180, 13, 2.7},
        {43, "Lake Trout",     "🐟", Rarity::Epic,      "Lake", 4.0, 12.0, 190, 13, 2.7},
        {81, "Lake Guardian Serpent","🐍",Rarity::Epic, "Lake", 6.0, 16.0, 210, 14, 2.7},
        {44, "Mirror Carp",    "🎏", Rarity::Legendary, "Lake", 5.0, 18.0, 650, 17, 4},
        {14, "Loch Serpent",   "🐉", Rarity::Mythic,    "Lake", 10.0,30.0, 1200,20, 2},
        {60, "Aurora Serpent", "🌌", Rarity::Legendary, "Lake", 8.0, 20.0, 1650,22, 2, "Windy"},

        // --- Ocean ---
        {15, "Mackerel",       "🐟", Rarity::Common,    "Ocean", 0.5, 2.0,  15,  4,  10},
        {45, "Herring",        "🐟", Rarity::Common,    "Ocean", 0.3, 1.5,  14,  3,  10},
        {82, "Message in a Bottle","📜",Rarity::Common, "Ocean", 0.1, 0.2,  2,   1,  10},
        {83, "Flying Fish",    "🐟", Rarity::Common,    "Ocean", 0.3, 1.0,  16,  4,  10},
        {16, "Tuna",           "🐟", Rarity::Uncommon,  "Ocean", 5.0, 30.0, 60,  9,  8},
        {46, "Marlin",         "🐟", Rarity::Uncommon,  "Ocean", 8.0, 40.0, 65,  10, 8},
        {84, "Clownfish",      "🐠", Rarity::Uncommon,  "Ocean", 0.1, 0.4,  28,  5,  8},
        {17, "Swordfish",      "🗡️", Rarity::Rare,      "Ocean", 15.0,60.0, 220, 14, 4.7},
        {47, "Barracuda",      "🐟", Rarity::Rare,      "Ocean", 5.0, 20.0, 230, 15, 4.7},
        {85, "Octopus",        "🐙", Rarity::Rare,      "Ocean", 2.0, 10.0, 240, 15, 4.7},
        {18, "Great White Shark","🦈",Rarity::Epic,     "Ocean", 50.0,200.0,700, 20, 2.7},
        {48, "Hammerhead Shark","🦈",Rarity::Epic,      "Ocean", 40.0,150.0,720, 21, 2.7},
        {86, "Ocean Guardian Turtle","🐢",Rarity::Epic, "Ocean", 30.0,90.0, 740, 21, 2.7},
        {49, "Giant Manta Ray","🦋", Rarity::Legendary, "Ocean", 60.0,250.0,1800,24, 4},
        {19, "Kraken Tentacle","🐙", Rarity::Mythic,    "Ocean", 20.0,80.0, 2500,28, 2},
        {61, "Tournament Marlin King","👑",Rarity::Legendary,"Ocean", 30.0,80.0, 2600,26, 2, "", true},

        // --- Deep Sea ---
        {20, "Anglerfish",     "🎣", Rarity::Uncommon,  "Deep Sea", 0.5, 3.0,  90,  10, 8},
        {50, "Gulper Eel",     "🐍", Rarity::Uncommon,  "Deep Sea", 1.0, 4.0,  95,  11, 8},
        {87, "Lanternfish",    "💡", Rarity::Uncommon,  "Deep Sea", 0.2, 1.0,  100, 11, 8},
        {21, "Giant Squid",    "🦑", Rarity::Rare,      "Deep Sea", 10.0,50.0, 350, 16, 4.7},
        {51, "Vampire Squid",  "🦑", Rarity::Rare,      "Deep Sea", 5.0, 20.0, 360, 17, 4.7},
        {88, "Black Dragonfish","🐡",Rarity::Rare,      "Deep Sea", 2.0, 8.0,  370, 17, 4.7},
        {22, "Deep Sea Dragonfish","🐡",Rarity::Epic,   "Deep Sea", 1.0, 5.0,  600, 18, 2.7},
        {52, "Ghost Shark",    "🦈", Rarity::Epic,      "Deep Sea", 10.0,40.0, 650, 19, 2.7},
        {89, "Yeti Crab",      "🦀", Rarity::Epic,      "Deep Sea", 1.0, 4.0,  680, 19, 2.7},
        {23, "Colossal Isopod","🐛", Rarity::Legendary, "Deep Sea", 0.5, 2.0,  1000,20, 1.3},
        {53, "Abyssal Jellyfish","🪼",Rarity::Legendary,"Deep Sea", 1.0, 5.0,  1100,21, 1.3},
        {90, "Frilled Shark",  "🦈", Rarity::Legendary, "Deep Sea", 15.0,40.0, 1200,22, 1.3},
        {24, "Leviathan Spawn","🐳", Rarity::Mythic,    "Deep Sea", 30.0,100.0,4000,30, 2},
        {62, "Abyssal Champion","🏆", Rarity::Mythic,   "Deep Sea", 40.0,90.0, 5200,32, 1, "", true},

        // --- Volcanic Vents ---
        {25, "Vent Crab",      "🦀", Rarity::Rare,      "Volcanic Vents", 1.0, 5.0,  500, 18, 4.7},
        {54, "Ember Shrimp",   "🦐", Rarity::Rare,      "Volcanic Vents", 0.5, 2.0,  480, 17, 4.7},
        {91, "Sulfur Crab",    "🦀", Rarity::Rare,      "Volcanic Vents", 1.0, 4.0,  520, 18, 4.7},
        {26, "Magma Eel",      "🔥", Rarity::Epic,      "Volcanic Vents", 5.0, 20.0, 1400,24, 2},
        {55, "Lava Serpent",   "🐉", Rarity::Epic,      "Volcanic Vents", 8.0, 25.0, 1450,25, 2},
        {92, "Basalt Turtle",  "🐢", Rarity::Epic,      "Volcanic Vents", 10.0,30.0, 1480,25, 2},
        {93, "Cinder Bat",     "🦇", Rarity::Epic,      "Volcanic Vents", 0.2, 1.0,  1350,23, 2},
        {27, "Obsidian Ray",   "⚫", Rarity::Legendary, "Volcanic Vents", 10.0,40.0, 3000,28, 0.8},
        {29, "Ashfin Behemoth","🌋", Rarity::Legendary, "Volcanic Vents", 40.0,150.0,5000,32, 0.8},
        {56, "Molten Turtle",  "🐢", Rarity::Legendary, "Volcanic Vents", 15.0,50.0, 3100,29, 0.8},
        {94, "Pyroclast Serpent","🐍",Rarity::Legendary,"Volcanic Vents", 12.0,45.0, 3200,30, 0.8},
        {95, "Ember Phoenix Chick","🐣",Rarity::Legendary,"Volcanic Vents", 1.0, 3.0,  3400,27, 0.8},
        {28, "Phoenix Fish",   "🔥", Rarity::Mythic,    "Volcanic Vents", 3.0, 10.0, 8000,35, 0.7},
        {57, "Cinder Wyrm",    "🔥", Rarity::Mythic,    "Volcanic Vents", 5.0, 15.0, 8500,36, 0.7},
        {96, "Sulfur Wyrm",    "🐉", Rarity::Mythic,    "Volcanic Vents", 6.0, 18.0, 8800,37, 0.7},
        {63, "Molten Crown Wyrm","👑",Rarity::Mythic,   "Volcanic Vents", 20.0,60.0, 15000,40, 0.5, "Stormy", true},

        // --- Seasonal event fish (only spawn while /event start matches their key) ---
        {97, "Pumpkin Puffer",     "🎃", Rarity::Rare,      "Pond",     0.5, 2.0,   180,  9,  3.0, "", false, false, "halloween"},
        {98, "Candy Corn Eel",     "🍬", Rarity::Epic,      "River",    0.3, 1.5,   420,  15, 2.0, "", false, false, "halloween"},
        {99, "Jack-o'-lantern Jelly","🎃", Rarity::Legendary, "Deep Sea", 1.0, 4.0,   1900, 22, 1.0, "", false, false, "halloween"},
        {100,"Lucky Lantern Koi",  "🧧", Rarity::Legendary, "Pond",     2.0, 5.0,   1600, 19, 1.2, "", false, false, "lunar_new_year"},

        // --- Abyssal Trench (rod tier 6+, Submarine) ---
        {101, "Trench Prawn",         "🦐", Rarity::Uncommon,  "Abyssal Trench", 0.1,  0.5,    250,  30, 8.0},
        {102, "Blind Cusk Eel",       "🐍", Rarity::Uncommon,  "Abyssal Trench", 0.5,  2.0,    260,  31, 8.0},
        {103, "Hadal Snailfish",      "🐟", Rarity::Rare,      "Abyssal Trench", 0.2,  1.0,    900,  34, 4.7},
        {104, "Giant Amphipod",       "🦐", Rarity::Rare,      "Abyssal Trench", 0.1,  0.4,    950,  34, 4.7},
        {105, "Trench Viperfish",     "🐍", Rarity::Epic,      "Abyssal Trench", 1.0,  4.0,   2000,  38, 2.7},
        {106, "Dumbo Octopus",        "🐙", Rarity::Epic,      "Abyssal Trench", 2.0,  8.0,   2100,  38, 2.7},
        {107, "Abyssal Anglerking",   "🎣", Rarity::Legendary, "Abyssal Trench", 5.0, 20.0,   4200,  44, 1.3},
        {108, "Midnight Oarfish",     "🌑", Rarity::Legendary, "Abyssal Trench", 20.0, 60.0,  4400,  46, 1.3},
        {109, "Void Leviathan",       "🌌", Rarity::Mythic,    "Abyssal Trench", 30.0, 80.0,  9000,  54, 0.7},
        {110, "Primordial Kraken Spawn","🦑", Rarity::Mythic,  "Abyssal Trench", 25.0, 70.0,  9500,  56, 0.7},
        {111, "Star-Eater Eel",       "⭐", Rarity::Mythic,    "Abyssal Trench", 8.0,  20.0, 14000,  60, 0.5, "Foggy"},

        // --- More fish for the existing locations ---
        {112, "Mudskipper",           "🐸", Rarity::Common,    "Pond",   0.05, 0.2,     3,  1, 6.7},
        {113, "Painted Turtle",       "🐢", Rarity::Uncommon,  "Pond",   0.4,  1.2,    15,  4, 8.0},
        {114, "Pond Sprite Salamander","🦎", Rarity::Rare,     "Pond",   0.3,  1.0,    55,  7, 4.7},
        {115, "Lily Dragon",          "🐉", Rarity::Epic,      "Pond",   2.0,  6.0,   280, 13, 4.0},
        {116, "Stickleback",          "🐟", Rarity::Common,    "River",  0.02, 0.1,     4,  1, 10.0},
        {117, "River Crayfish",       "🦞", Rarity::Uncommon,  "River",  0.1,  0.4,    23,  4, 8.0},
        {118, "Sturgeon Elder",       "🐠", Rarity::Rare,      "River",  8.0, 25.0,    75,  9, 7.0},
        {119, "Torrent Eel",          "🐍", Rarity::Epic,      "River",  3.0,  9.0,   200, 13, 2.7},
        {120, "Freshwater Clam",      "🐚", Rarity::Common,    "Lake",   0.1,  0.5,     4,  1, 10.0},
        {121, "Lake Whitefish",       "🐟", Rarity::Uncommon,  "Lake",   1.0,  3.0,    21,  5, 8.0},
        {122, "Ghost Catfish",        "👻", Rarity::Rare,      "Lake",   4.0, 14.0,    88, 10, 4.7},
        {123, "Lakebed Hydra",        "🐉", Rarity::Epic,      "Lake",   6.0, 15.0,   220, 14, 2.7},
        {124, "Sea Urchin",           "🦔", Rarity::Common,    "Ocean",  0.2,  0.6,    12,  3, 10.0},
        {125, "Pufferfish",           "🐡", Rarity::Uncommon,  "Ocean",  0.5,  2.0,    32,  6, 8.0},
        {126, "Opah",                 "🐠", Rarity::Rare,      "Ocean",  20.0, 80.0,  235, 16, 4.7},
        {127, "Sailfish",             "🐟", Rarity::Epic,      "Ocean",  30.0, 90.0,  760, 22, 2.7},
        {128, "Rainbow Nautilus",     "🐚", Rarity::Legendary, "Ocean",  1.0,  4.0,  1900, 24, 1.3},
        {129, "Hatchetfish",          "🐟", Rarity::Uncommon,  "Deep Sea", 0.1, 0.5,  105, 11, 8.0},
        {130, "Barreleye",            "👁️", Rarity::Rare,      "Deep Sea", 0.3, 1.2,  380, 17, 4.7},
        {131, "Glass Siphonophore",   "🎐", Rarity::Epic,      "Deep Sea", 5.0, 20.0, 700, 20, 2.7},
        {132, "Sleeper Shark",        "🦈", Rarity::Legendary, "Deep Sea", 40.0, 120.0, 1250, 23, 1.3},
        {133, "Tube Worm",            "🐛", Rarity::Rare,      "Volcanic Vents", 0.5, 2.0,   500, 17, 4.7},
        {134, "Magma Crab King",      "🦀", Rarity::Epic,      "Volcanic Vents", 6.0, 20.0, 1500, 25, 2.0},
        {135, "Lava Gar",             "🔥", Rarity::Legendary, "Volcanic Vents", 10.0, 35.0, 3300, 29, 0.8},
        {136, "Solar Flare Dragonet", "🐲", Rarity::Mythic,    "Volcanic Vents", 4.0, 12.0, 9000, 38, 0.7},

        // --- Frozen Fjord (rowboat, rod tier 3) ---
        {137, "Capelin",              "🐟", Rarity::Common,    "Frozen Fjord", 0.05, 0.2,   16,  3, 10.0},
        {138, "Arctic Char",          "🐟", Rarity::Common,    "Frozen Fjord", 0.5,  3.0,   18,  4, 10.0},
        {139, "Krill Swarm",          "🦐", Rarity::Common,    "Frozen Fjord", 0.02, 0.1,   14,  3, 10.0},
        {140, "Frozen Mitten",        "🧤", Rarity::Common,    "Frozen Fjord", 0.1,  0.3,    2,  1, 10.0},
        {141, "Atlantic Cod",         "🐟", Rarity::Uncommon,  "Frozen Fjord", 2.0,  12.0,  30,  8, 8.0},
        {142, "Halibut",              "🐟", Rarity::Uncommon,  "Frozen Fjord", 5.0,  40.0,  32,  9, 8.0},
        {143, "Snow Crab",            "🦀", Rarity::Uncommon,  "Frozen Fjord", 0.5,  3.0,   55,  8, 8.0},
        {144, "Glacier Pike",         "🐊", Rarity::Rare,      "Frozen Fjord", 3.0,  14.0, 150, 13, 4.7},
        {145, "Wolffish",             "🐟", Rarity::Rare,      "Frozen Fjord", 2.0,  10.0, 160, 14, 4.7},
        {146, "Ice Lobster",          "🦞", Rarity::Rare,      "Frozen Fjord", 1.0,  5.0,  170, 14, 4.7},
        {147, "Greenland Shark",      "🦈", Rarity::Epic,      "Frozen Fjord", 80.0, 250.0,140, 20, 2.7},
        {148, "Giant Sea Spider",     "🕷️", Rarity::Epic,      "Frozen Fjord", 0.5,  3.0,  600, 19, 2.7},
        {149, "Frostbite Sturgeon",   "🐟", Rarity::Epic,      "Frozen Fjord", 20.0, 80.0, 190, 21, 2.7},
        {150, "Crystal Char",         "💎", Rarity::Legendary, "Frozen Fjord", 1.0,  4.0, 1700, 24, 1.3},
        {151, "Aurora Narwhal",       "🐋", Rarity::Legendary, "Frozen Fjord", 40.0, 100.0,110, 26, 1.3},
        {152, "Frost Wyrm",           "🐉", Rarity::Mythic,    "Frozen Fjord", 10.0, 40.0, 450, 32, 0.7},
        {153, "Glacier Leviathan",    "🧊", Rarity::Mythic,    "Frozen Fjord", 30.0, 90.0, 150, 30, 0.7},
        {154, "Snowstorm Salmon",     "❄️", Rarity::Rare,      "Frozen Fjord", 3.0,  9.0,  260, 15, 3.0, "Windy"},
        {155, "Fjord Phantom",        "👻", Rarity::Mythic,    "Frozen Fjord", 5.0,  20.0, 900, 34, 0.5, "Foggy"},

        // --- Starfall Reef (Bathyscaphe, rod tier 7) ---
        {156, "Stardust Tetra",       "✨", Rarity::Uncommon,  "Starfall Reef", 0.1, 0.5,    700, 50, 8.0},
        {157, "Comet Shrimp",         "☄️", Rarity::Uncommon,  "Starfall Reef", 0.1, 0.6,    720, 52, 8.0},
        {158, "Meteor Eel",           "🌠", Rarity::Rare,      "Starfall Reef", 1.0, 5.0,   1800, 58, 4.7},
        {159, "Moonlit Ray",          "🌙", Rarity::Rare,      "Starfall Reef", 5.0, 20.0,  1900, 60, 4.7},
        {160, "Nebula Jelly",         "🪼", Rarity::Rare,      "Starfall Reef", 1.0, 6.0,   1950, 60, 4.7},
        {161, "Eclipse Angler",       "🌑", Rarity::Epic,      "Starfall Reef", 3.0, 10.0,  3800, 68, 2.7},
        {162, "Solar Mantis Shrimp",  "🦐", Rarity::Epic,      "Starfall Reef", 1.0, 4.0,   3900, 68, 2.7},
        {163, "Constellation Koi",    "🎏", Rarity::Epic,      "Starfall Reef", 4.0, 12.0,  4000, 70, 2.7},
        {164, "Supernova Pike",       "🌟", Rarity::Legendary, "Starfall Reef", 10.0, 30.0, 7500, 82, 1.3},
        {165, "Cosmic Whale Calf",    "🐋", Rarity::Legendary, "Starfall Reef", 40.0, 100.0,3000, 86, 1.3},
        {166, "Galaxy Leviathan",     "🌌", Rarity::Mythic,    "Starfall Reef", 20.0, 60.0,12000, 95, 0.7},
        {167, "World-Tree Serpent",   "🐍", Rarity::Mythic,    "Starfall Reef", 15.0, 40.0,14000, 98, 0.7},
        {168, "Pulsar Eel",           "⚡", Rarity::Legendary, "Starfall Reef", 6.0,  18.0, 9000, 84, 1.0, "Stormy"},
        {169, "Void Whale",           "🕳️", Rarity::Mythic,    "Starfall Reef", 60.0, 150.0,6500, 100, 0.5, "Foggy"},

        // --- Extra catches for the older spots ---
        {170, "Dace",                 "🐟", Rarity::Common,    "Pond",   0.05, 0.3,    4,  1, 6.7},
        {171, "Lily Pad Frog",        "🐸", Rarity::Uncommon,  "Pond",   0.05, 0.2,   13,  3, 8.0},
        {172, "Moss-back Terrapin",   "🐢", Rarity::Rare,      "Pond",   1.0,  4.0,   58,  7, 4.7},
        {173, "Firefly Koi",          "🎏", Rarity::Legendary, "Pond",   2.0,  5.0,  1050, 17, 2.0, "", false, false, ""},
        {174, "Smelt",                "🐟", Rarity::Common,    "River",  0.03, 0.15,   5,  1, 10.0},
        {175, "River Lamprey",        "🐍", Rarity::Uncommon,  "River",  0.2,  0.9,   25,  5, 8.0},
        {176, "Blue Catfish",         "🐡", Rarity::Rare,      "River",  8.0,  30.0,  78,  9, 7.0},
        {177, "Rapids Drake",         "🐉", Rarity::Legendary, "River",  4.0,  12.0,  640, 18, 2.0},
        {178, "Bluegill Sunfish",     "🐠", Rarity::Common,    "Lake",   0.1,  0.6,    6,  2, 10.0},
        {179, "Lake Snapper",         "🐊", Rarity::Rare,      "Lake",   2.0,  9.0,   90, 10, 4.7},
        {180, "Mist Sturgeon",        "🌫️", Rarity::Legendary, "Lake",   10.0, 25.0, 700, 18, 2.0},
        {181, "Anchovy",              "🐟", Rarity::Common,    "Ocean",  0.01, 0.05,  10,  2, 10.0},
        {182, "Mahi-mahi",            "🐠", Rarity::Uncommon,  "Ocean",  3.0,  15.0,  38,  8, 8.0},
        {183, "Sea Turtle",           "🐢", Rarity::Rare,      "Ocean",  20.0, 70.0, 230, 15, 4.7},
        {184, "Oarfish",              "🐍", Rarity::Epic,      "Ocean",  30.0, 100.0,730, 22, 2.7},
        {185, "Moonfish Prince",      "🌕", Rarity::Legendary, "Ocean",  10.0, 40.0, 1850,24, 1.3},
        {186, "Cusk-eel",             "🐍", Rarity::Uncommon,  "Deep Sea", 0.5, 3.0,  100, 11, 8.0},
        {187, "Stoplight Loosejaw",   "🐟", Rarity::Rare,      "Deep Sea", 0.2, 0.8,  375, 17, 4.7},
        {188, "Siphon Titan",         "🎐", Rarity::Epic,      "Deep Sea", 8.0, 30.0,  690, 20, 2.7},
        {189, "Abyss Pearl Clam",     "🦪", Rarity::Legendary, "Deep Sea", 2.0, 8.0,  1150,22, 1.3},
        {190, "Brimstone Shrimp",     "🦐", Rarity::Rare,      "Volcanic Vents", 0.3, 1.5,  490, 17, 4.7},
        {191, "Smoker Crab",          "🦀", Rarity::Epic,      "Volcanic Vents", 4.0, 14.0,1420,24, 2.0},
        {192, "Obsidian Gar",         "⚫", Rarity::Legendary, "Volcanic Vents", 8.0, 28.0,3150,29, 0.8},
        {193, "Caldera Drake",        "🐲", Rarity::Mythic,    "Volcanic Vents", 6.0, 20.0,8700,37, 0.7},
        {194, "Trench Lanternshark",  "🦈", Rarity::Uncommon,  "Abyssal Trench", 0.5, 2.5,  255, 31, 8.0},
        {195, "Ghost Amphipod",       "🦐", Rarity::Rare,      "Abyssal Trench", 0.1, 0.5,  930, 34, 4.7},
        {196, "Glasshead Barreleye",  "👁️", Rarity::Epic,      "Abyssal Trench", 0.5, 2.0, 2050, 38, 2.7},
        {197, "Hadal Colossus Squid", "🦑", Rarity::Legendary, "Abyssal Trench", 15.0, 45.0,4300, 45, 1.3},
        {198, "Pitch-black Leviathan","🌑", Rarity::Mythic,    "Abyssal Trench", 25.0, 70.0,9200, 55, 0.7},

        // --- More seasonal fish (start the event from the CLI with the matching key) ---
        {199, "Snowglobe Betta",      "🔮", Rarity::Rare,      "Frozen Fjord", 0.3, 1.2,  300, 12, 3.0, "", false, false, "winter_fest"},
        {200, "Gingerbread Crab",     "🍪", Rarity::Epic,      "Pond",    0.5, 2.0,  500, 14, 2.0, "", false, false, "winter_fest"},
        {201, "Tinsel Eel",           "🎄", Rarity::Legendary, "Ocean",   2.0, 8.0, 2000, 24, 1.0, "", false, false, "winter_fest"},
        {202, "Sunburst Guppy",       "🌞", Rarity::Rare,      "Lake",    0.1, 0.5,  200,  8, 3.0, "", false, false, "summer_splash"},
        {203, "Watermelon Pufferfish","🍉", Rarity::Epic,      "Ocean",   0.5, 2.5,  450, 18, 2.0, "", false, false, "summer_splash"},
        {204, "Coconut Crab Lord",    "🥥", Rarity::Legendary, "Volcanic Vents", 4.0, 12.0, 3500, 29, 1.0, "", false, false, "summer_splash"},

        // --- World boss, Starfall edition ---
        {205, "Starfall Colossus",    "🌠👑", Rarity::Mythic,  "Starfall Reef", 120.0, 300.0, 20000, 100, 0, "", false, true},

        // --- World Boss exclusives (never spawn from a normal /fish cast) ---
        {64, "Kraken King",    "🐙👑",Rarity::Mythic,   "Ocean", 60.0,150.0,  6000, 50, 0, "", false, true},
        {65, "Leviathan Alpha","🐳👑",Rarity::Mythic,   "Deep Sea", 80.0,200.0, 8000, 55, 0, "", false, true},
        {66, "Inferno Wyrm King","🔥👑",Rarity::Mythic, "Volcanic Vents", 50.0,120.0, 12000,60, 0, "", false, true},
    };
    return F;
}

const FishSpecies* find_fish(int id) {
    for (auto& f : all_fish()) if (f.id == id) return &f;
    return nullptr;
}

std::vector<const FishSpecies*> fish_for_location(const std::string& location) {
    std::vector<const FishSpecies*> out;
    for (auto& f : all_fish()) if (f.location == location) out.push_back(&f);
    return out;
}
