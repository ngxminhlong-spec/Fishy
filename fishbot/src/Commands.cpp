#include "Commands.hpp"
#include "Game.hpp"
#include "FishData.hpp"
#include "I18n.hpp"
#include "SysStats.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <ctime>

// ============================= small helpers =============================

static std::string uid(const dpp::slashcommand_t& e) {
    return e.command.get_issuing_user().id.str();
}

static std::string fmt_coins(int64_t n, const std::string& unit = "coins") {
    std::string s = std::to_string(n);
    std::string out;
    int cnt = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        out.push_back(*it);
        if (++cnt % 3 == 0 && std::next(it) != s.rend() && std::isdigit(*std::next(it))) out.push_back(',');
    }
    std::reverse(out.begin(), out.end());
    return out + " " + unit;
}

static std::string fmt_weight(double kg) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << kg << "kg";
    return ss.str();
}

static const dpp::command_data_option* find_option(const std::vector<dpp::command_data_option>& opts,
                                                     const std::string& name) {
    for (auto& o : opts) {
        if (o.name == name) return &o;
        if (!o.options.empty()) {
            auto* r = find_option(o.options, name);
            if (r) return r;
        }
    }
    return nullptr;
}

static std::string get_subcommand(const dpp::slashcommand_t& e) {
    const auto& opts = e.command.get_command_interaction().options;
    if (!opts.empty() && opts[0].type == dpp::co_sub_command) return opts[0].name;
    return "";
}

static std::optional<int64_t> get_int(const dpp::slashcommand_t& e, const std::string& name) {
    auto* o = find_option(e.command.get_command_interaction().options, name);
    if (!o) return std::nullopt;
    return std::get<int64_t>(o->value);
}

static std::optional<std::string> get_str(const dpp::slashcommand_t& e, const std::string& name) {
    auto* o = find_option(e.command.get_command_interaction().options, name);
    if (!o) return std::nullopt;
    return std::get<std::string>(o->value);
}

static std::optional<dpp::snowflake> get_user(const dpp::slashcommand_t& e, const std::string& name) {
    auto* o = find_option(e.command.get_command_interaction().options, name);
    if (!o) return std::nullopt;
    return std::get<dpp::snowflake>(o->value);
}

static void reply(const dpp::slashcommand_t& e, const std::string& content, bool ephemeral = false) {
    dpp::message m(content);
    if (ephemeral) m.set_flags(dpp::m_ephemeral);
    e.reply(m);
}

static void reply_embed(const dpp::slashcommand_t& e, const dpp::embed& em, bool ephemeral = false) {
    dpp::message m;
    m.add_embed(em);
    if (ephemeral) m.set_flags(dpp::m_ephemeral);
    e.reply(m);
}

static dpp::embed base_embed() {
    return dpp::embed().set_color(0x2b8fd6);
}

// ---- language support ----
// A player's language: their /language choice if they made one, otherwise the language
// of their Discord client (e.g. a Vietnamese Discord app gets Vietnamese automatically).
static Lang lang_of(const dpp::slashcommand_t& e, Database& db) {
    if (auto l = parse_lang(db.get_lang(uid(e)))) return *l;
    return lang_from_discord_locale(e.command.locale);
}

static std::string fmt_coins_l(Lang l, int64_t n) { return fmt_coins(n, tr(l, "unit.coins")); }

// Adds Vietnamese + Chinese descriptions so Discord shows the command list in the
// viewer's own language. (The command *name* stays the same in every language.)
static void localize(dpp::slashcommand& c, const std::string& desc_key) {
    c.add_localization("vi", c.name, tr(Lang::VI, desc_key));
    c.add_localization("zh-CN", c.name, tr(Lang::ZH, desc_key));
}

static std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

// Counts every slash command handled since the bot started (shown in /stats).
static std::atomic<uint64_t> g_commands_run{0};

// XP progress helper: rewards coins + xp, saves user, returns level-up message (or "").
static std::string grant_xp_and_maybe_levelup(Database& db, UserRow& u, int xp) {
    int old_level = u.level;
    u.xp += xp;
    u.level = level_for_xp(u.xp);
    db.save_user(u);
    if (u.level > old_level) {
        return "🎉 You leveled up to **level " + std::to_string(u.level) + "**!";
    }
    return "";
}

// ============================= command registration =============================

void register_commands(dpp::cluster& bot) {
    std::vector<dpp::slashcommand> cmds;
    dpp::snowflake app_id = bot.me.id;

    cmds.emplace_back(dpp::slashcommand("fish", "Cast your line at your current location!", app_id));
    localize(cmds.back(), "cmd.fish.desc");

    {
        dpp::slashcommand c("shop", "Browse and buy rods, bait, boats and pets.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "view", "See everything available in the shop."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "rod", "Buy a fishing rod.")
            .add_option(dpp::command_option(dpp::co_integer, "tier", "Rod tier to buy (0-8)", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "bait", "Buy bait.")
            .add_option(dpp::command_option(dpp::co_integer, "id", "Bait id to buy (0-4)", true))
            .add_option(dpp::command_option(dpp::co_integer, "amount", "How many to buy", false)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "boat", "Buy a boat.")
            .add_option(dpp::command_option(dpp::co_integer, "tier", "Boat tier to buy (0-2)", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "pet", "Adopt a fishing pet.")
            .add_option(dpp::command_option(dpp::co_integer, "id", "Pet id to adopt (0-4)", true)));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("sell", "Sell fish from your inventory for coins.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "one", "Sell a single fish by inventory id.")
            .add_option(dpp::command_option(dpp::co_integer, "inv_id", "Inventory id (see /inventory)", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "all", "Sell your entire inventory (except aquarium fish)."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("trade", "Trade coins and fish with another player.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "start", "Start a trade with another user.")
            .add_option(dpp::command_option(dpp::co_user, "user", "Who to trade with", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "additem", "Add a fish to your side of a trade.")
            .add_option(dpp::command_option(dpp::co_integer, "trade_id", "Trade id", true))
            .add_option(dpp::command_option(dpp::co_integer, "inv_id", "Your fish's inventory id", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "addcoins", "Add coins to your side of a trade.")
            .add_option(dpp::command_option(dpp::co_integer, "trade_id", "Trade id", true))
            .add_option(dpp::command_option(dpp::co_integer, "amount", "Coins to offer", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "confirm", "Confirm your side of a trade.")
            .add_option(dpp::command_option(dpp::co_integer, "trade_id", "Trade id", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "cancel", "Cancel a trade.")
            .add_option(dpp::command_option(dpp::co_integer, "trade_id", "Trade id", true)));
        cmds.push_back(c);
    }

    cmds.emplace_back(dpp::slashcommand("inventory", "View your caught fish.", app_id));
    cmds.emplace_back(dpp::slashcommand("balance", "Check your coin balance.", app_id));
    localize(cmds.back(), "cmd.balance.desc");
    cmds.emplace_back(dpp::slashcommand("daily", "Claim your daily coin reward.", app_id));
    localize(cmds.back(), "cmd.daily.desc");

    {
        dpp::slashcommand c("leaderboard", "See the top players.", app_id);
        c.add_option(dpp::command_option(dpp::co_string, "type", "Which leaderboard", false)
            .add_choice(dpp::command_option_choice("Richest", std::string("balance")))
            .add_choice(dpp::command_option_choice("Most fish caught", std::string("fish")))
            .add_choice(dpp::command_option_choice("Current tournament", std::string("tournament"))));
        cmds.push_back(c);
    }

    cmds.emplace_back(dpp::slashcommand("profile", "View your (or someone else's) angler profile.", app_id))
        .add_option(dpp::command_option(dpp::co_user, "user", "Whose profile to view", false));

    cmds.emplace_back(dpp::slashcommand("fishdex", "See which fish species you've discovered.", app_id));
    cmds.emplace_back(dpp::slashcommand("achievements", "View your unlocked achievements.", app_id));

    {
        dpp::slashcommand c("quest", "View or claim your daily quest.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "view", "See your current quest."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "claim", "Claim your reward if complete."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("location", "View or travel to a fishing location.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "view", "List all locations."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "travel", "Travel somewhere new.")
            .add_option(dpp::command_option(dpp::co_string, "name", "Location name", true)
                .add_choice(dpp::command_option_choice("Pond", std::string("Pond")))
                .add_choice(dpp::command_option_choice("River", std::string("River")))
                .add_choice(dpp::command_option_choice("Lake", std::string("Lake")))
                .add_choice(dpp::command_option_choice("Ocean", std::string("Ocean")))
                .add_choice(dpp::command_option_choice("Deep Sea", std::string("Deep Sea")))
                .add_choice(dpp::command_option_choice("Volcanic Vents", std::string("Volcanic Vents")))
                .add_choice(dpp::command_option_choice("Abyssal Trench", std::string("Abyssal Trench")))));
        cmds.push_back(c);
    }

    cmds.emplace_back(dpp::slashcommand("market", "Check today's fish market prices.", app_id));
    cmds.emplace_back(dpp::slashcommand("weather", "See the current fishing weather.", app_id));
    cmds.emplace_back(dpp::slashcommand("pet", "Check on your fishing companion.", app_id));

    cmds.emplace_back(dpp::slashcommand("gift", "Send coins to another player.", app_id))
        .add_option(dpp::command_option(dpp::co_user, "user", "Who to gift", true))
        .add_option(dpp::command_option(dpp::co_integer, "amount", "How many coins", true));

    {
        dpp::slashcommand c("aquarium", "Keep fish on display instead of selling them.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "view", "View your aquarium."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "add", "Move a fish into your aquarium.")
            .add_option(dpp::command_option(dpp::co_integer, "inv_id", "Inventory id", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "remove", "Move a fish out of your aquarium.")
            .add_option(dpp::command_option(dpp::co_integer, "inv_id", "Inventory id", true)));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("tournament", "Fishing tournament status (admins run them from the CLI).", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "status", "See if a tournament is running."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("event", "Seasonal limited-time fish event status (admins run them from the CLI).", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "status", "See if a seasonal event is running."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("crew", "Form a fishing crew with other players.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "create", "Found a new crew.")
            .add_option(dpp::command_option(dpp::co_string, "name", "Crew name (must be unique)", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "join", "Join an existing crew.")
            .add_option(dpp::command_option(dpp::co_string, "name", "Crew name", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "leave", "Leave your current crew."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "info", "View a crew's info.")
            .add_option(dpp::command_option(dpp::co_string, "name", "Crew name (defaults to your own)", false)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "deposit", "Deposit coins into your crew's bank.")
            .add_option(dpp::command_option(dpp::co_integer, "amount", "Coins to deposit", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "withdraw", "[Owner] Withdraw coins from the crew bank.")
            .add_option(dpp::command_option(dpp::co_integer, "amount", "Coins to withdraw", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "kick", "[Owner] Remove a member from your crew.")
            .add_option(dpp::command_option(dpp::co_user, "user", "Member to remove", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "disband", "[Owner] Disband your crew."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "leaderboard", "See the richest crews."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("boss", "World Boss fights — everyone in the server can help!", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "status", "See if a World Boss is active."));
        cmds.push_back(c);
    }

    cmds.emplace_back(dpp::slashcommand("help", "List everything the bot can do.", app_id));
    localize(cmds.back(), "cmd.help.desc");

    cmds.emplace_back(dpp::slashcommand("stats", "Show bot, server and database statistics.", app_id));
    localize(cmds.back(), "cmd.stats.desc");
    cmds.emplace_back(dpp::slashcommand("terms", "Read the Terms of Service.", app_id));
    localize(cmds.back(), "cmd.terms.desc");
    cmds.emplace_back(dpp::slashcommand("privacy", "Read the Privacy Policy.", app_id));
    localize(cmds.back(), "cmd.privacy.desc");
    {
        dpp::slashcommand c("language", "Choose the language the bot uses with you.", app_id);
        c.add_option(dpp::command_option(dpp::co_string, "language", "Language to use", false)
            .add_choice(dpp::command_option_choice("English", std::string("en")))
            .add_choice(dpp::command_option_choice("Tiếng Việt", std::string("vi")))
            .add_choice(dpp::command_option_choice("中文（简体）", std::string("zh")))
            .add_choice(dpp::command_option_choice("Auto (follow Discord)", std::string("auto"))));
        localize(c, "cmd.language.desc");
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("auction", "Buy and sell fish with other players.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "browse", "See what's for sale.")
            .add_option(dpp::command_option(dpp::co_integer, "page", "Page number", false)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "list", "Put one of your fish up for sale.")
            .add_option(dpp::command_option(dpp::co_integer, "inv_id", "Inventory id (see /inventory)", true))
            .add_option(dpp::command_option(dpp::co_integer, "price", "Asking price in coins", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "buy", "Buy a listing.")
            .add_option(dpp::command_option(dpp::co_integer, "id", "Listing number", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "cancel", "Take one of your listings back.")
            .add_option(dpp::command_option(dpp::co_integer, "id", "Listing number", true)));
        c.add_option(dpp::command_option(dpp::co_sub_command, "mine", "See your active listings."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("lottery", "Daily lottery: buy tickets, one winner takes the pot.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "status", "See the jackpot and your tickets."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "buy", "Buy lottery tickets.")
            .add_option(dpp::command_option(dpp::co_integer, "count", "How many tickets", true)));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("enchant", "Permanent upgrades bought with coins.", app_id);
        c.add_option(dpp::command_option(dpp::co_sub_command, "list", "See your enchants and what the next level costs."));
        c.add_option(dpp::command_option(dpp::co_sub_command, "upgrade", "Upgrade an enchant by one level.")
            .add_option(dpp::command_option(dpp::co_string, "name", "Which enchant", true)
                .add_choice(dpp::command_option_choice("Lucky Lure", std::string("lucky")))
                .add_choice(dpp::command_option_choice("Swift Reel", std::string("swift")))
                .add_choice(dpp::command_option_choice("Greedy Hook", std::string("greedy")))
                .add_choice(dpp::command_option_choice("Magnetic Bait", std::string("magnetic")))));
        cmds.push_back(c);
    }

    // Server-only: no user-install, no DMs / group DMs.
    for (auto& c : cmds) {
        c.set_interaction_contexts({dpp::itc_guild});
        c.integration_types = {dpp::ait_guild_install};
    }

    bot.global_bulk_command_create(cmds);
}

// ============================= /fish =============================

static void cmd_fish(dpp::cluster& bot, const dpp::slashcommand_t& e, Database& db) {
    std::string id = uid(e);
    UserRow u = db.get_or_create_user(id);

    int64_t now = (int64_t)time(nullptr);
    const int COOLDOWN = 8; // seconds between casts
    if (now - u.last_cast < COOLDOWN) {
        reply(e, "⏳ Your line just went out — wait " + std::to_string(COOLDOWN - (now - u.last_cast)) + "s before casting again.", true);
        return;
    }

    const Location* loc = find_location(u.location);
    if (!loc) { reply(e, "Unknown location — try `/location travel`.", true); return; }
    if (u.rod_tier < loc->min_rod_tier) {
        reply(e, "🎣 You need at least a stronger rod to fish in **" + loc->name + "**. Check `/shop view`.", true);
        return;
    }
    if (loc->min_boat_tier >= 0 && u.boat_tier < loc->min_boat_tier) {
        reply(e, "🚤 You need a boat to fish in **" + loc->name + "**. Check `/shop boat`.", true);
        return;
    }

    u.last_cast = now;
    db.save_user(u);

    double pet_bonus = 0.0;
    if (u.pet_id >= 0) {
        if (auto* p = find_pet(u.pet_id)) pet_bonus = p->catch_bonus * (1.0 + 0.05 * (u.pet_level - 1));
    }

    CastOutcome outcome = resolve_cast(u.rod_tier, u.bait_id, u.location, pet_bonus, db.get_enchants(id));
    db.bump_quest_progress(id, "cast_10", 1);

    if (!outcome.bit) {
        dpp::message m("🌊 You cast your line into **" + loc->name + "**... but nothing bit this time.");
        e.reply(m);
        return;
    }

    const Rod* rod = find_rod(u.rod_tier);
    ReelSession sess;
    sess.user_id = id;
    sess.fish = outcome.fish;
    sess.weight = outcome.weight;
    sess.rod_power = rod ? rod->power : 2;
    sess.reels_needed = outcome.reels_required;
    sess.reels_done = 0;
    // Give ~3.5s per reel needed (click-to-click round trip, plus reaction time),
    // with a floor of 12s. Previously this was a flat 12s regardless of reels_needed,
    // which made high-strength fish on a weak rod (e.g. 6 reels needed) nearly
    // impossible to land even before accounting for any network latency.
    int window_seconds = std::max(12, 5 + sess.reels_needed * 4);
    sess.expires_at = now + window_seconds;
    sess.tournament_catch = WorldState::instance().tournament_active() &&
                             WorldState::instance().tournament_location() == u.location;

    // Give the session a unique key up front (user + timestamp + a random tiebreaker)
    // rather than waiting on the created message's id, which isn't reliably available
    // from an interaction-response callback.
    std::uniform_int_distribution<int> tie(0, 999999);
    std::string session_key = id + "_" + std::to_string(now) + "_" + std::to_string(tie(rng()));
    ReelSessionManager::instance().put(session_key, sess);

    dpp::message m;
    m.add_embed(base_embed()
        .set_title("Something's biting! " + std::string(outcome.fish->emoji))
        .set_description("A **" + outcome.fish->name + "** (" + rarity_name(outcome.fish->rarity) +
                          ") is on the line!\nClick **Reel!** " + std::to_string(outcome.reels_required) +
                          " time(s) before it gets away (" + std::to_string(window_seconds) + "s).")
        .set_color(0x44c767));
    dpp::component row;
    row.add_component(dpp::component().set_type(dpp::cot_button).set_label("Reel! 🎣")
        .set_style(dpp::cos_primary).set_id("reel_" + session_key));
    m.add_component(row);
    e.reply(m);
}

// ============================= reel button =============================

static void handle_reel_click(dpp::cluster& bot, const dpp::button_click_t& e, Database& db,
                               const std::string& session_key) {
    auto sess_opt = ReelSessionManager::instance().get(session_key);
    if (!sess_opt) {
        e.reply(dpp::ir_update_message, dpp::message("🐟 The fish got away — this line went slack."));
        return;
    }
    ReelSession sess = *sess_opt;
    std::string clicker = e.command.get_issuing_user().id.str();
    if (clicker != sess.user_id) {
        e.reply(dpp::ir_channel_message_with_source,
                dpp::message("This isn't your fish to reel in!").set_flags(dpp::m_ephemeral));
        return;
    }

    int64_t now = (int64_t)time(nullptr);
    if (now > sess.expires_at) {
        ReelSessionManager::instance().erase(session_key);
        dpp::message m("💨 The **" + sess.fish->name + "** slipped off the hook. Better luck next cast!");
        e.reply(dpp::ir_update_message, m);
        return;
    }

    sess.reels_done++;
    if (sess.reels_done < sess.reels_needed) {
        ReelSessionManager::instance().update(session_key, sess);
        dpp::message m;
        m.add_embed(base_embed()
            .set_title("Reeling it in... " + std::string(sess.fish->emoji))
            .set_description("Progress: **" + std::to_string(sess.reels_done) + " / " +
                              std::to_string(sess.reels_needed) + "**\nKeep clicking Reel!")
            .set_color(0xf5c542));
        dpp::component row;
        row.add_component(dpp::component().set_type(dpp::cot_button).set_label("Reel! 🎣")
            .set_style(dpp::cos_primary).set_id("reel_" + session_key));
        m.add_component(row);
        e.reply(dpp::ir_update_message, m);
        return;
    }

    // Success! Land the fish.
    ReelSessionManager::instance().erase(session_key);
    UserRow u = db.get_or_create_user(sess.user_id);
    db.add_fish(sess.user_id, sess.fish->id, sess.weight);
    bool first_time = !db.has_achievement(sess.user_id, "seen_" + std::to_string(sess.fish->id));
    db.mark_discovered(sess.user_id, sess.fish->id);
    db.unlock_achievement(sess.user_id, "seen_" + std::to_string(sess.fish->id)); // internal, not a real award

    if (sess.tournament_catch) db.tournament_add_score(sess.user_id, sess.weight);

    db.bump_quest_progress(sess.user_id, "catch_5", 1);
    if (sess.fish->rarity >= Rarity::Uncommon) db.bump_quest_progress(sess.user_id, "catch_3_uncommon", 1);
    if (sess.fish->rarity >= Rarity::Rare) db.bump_quest_progress(sess.user_id, "catch_1_rare", 1);
    if (u.location == "River") db.bump_quest_progress(sess.user_id, "catch_river", 1);
    if (u.location == "Ocean") db.bump_quest_progress(sess.user_id, "catch_ocean", 1);

    std::string levelup = grant_xp_and_maybe_levelup(db, u, xp_for_catch(*sess.fish));
    auto unlocked = check_achievements(db, sess.user_id, sess.fish, sess.weight);

    std::ostringstream desc;
    desc << "You landed a **" << sess.fish->name << "** " << sess.fish->emoji
         << " (" << rarity_name(sess.fish->rarity) << ") weighing **" << fmt_weight(sess.weight) << "**!"
         << (first_time ? "\n✨ New Fishdex entry!" : "");
    if (!levelup.empty()) desc << "\n" << levelup;
    for (auto& a : unlocked) desc << "\n🏆 Achievement unlocked: **" << a << "**";
    int est = market_sell_price(*sess.fish, sess.weight);
    desc << "\nEstimated sell value: " << fmt_coins(est) << " (check `/market`).";

    dpp::message m;
    m.add_embed(base_embed()
        .set_title("Caught it! " + std::string(sess.fish->emoji))
        .set_description(desc.str())
        .set_color(0x2ecc71));
    e.reply(dpp::ir_update_message, m);
}

// ============================= /shop =============================

static void cmd_shop(dpp::cluster& bot, const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    UserRow u = db.get_or_create_user(id);

    if (sub == "view" || sub.empty()) {
        std::ostringstream rods, baits, boats, pets;
        for (auto& r : all_rods())
            rods << (r.tier == u.rod_tier ? "**[owned] " : "") << r.name << (r.tier == u.rod_tier ? "**" : "")
                 << " — power " << r.power << ", " << fmt_coins(r.price) << " (tier " << r.tier << ")\n";
        for (auto& b : all_bait())
            baits << b.name << " — " << fmt_coins(b.price) << " each (id " << b.id << ")\n";
        for (auto& bo : all_boats())
            boats << (bo.tier == u.boat_tier ? "**[owned] " : "") << bo.name << (bo.tier == u.boat_tier ? "**" : "")
                  << " — " << fmt_coins(bo.price) << " (tier " << bo.tier << ")\n";
        for (auto& p : all_pets())
            pets << p.emoji << " " << p.name << " — " << fmt_coins(p.price) << " (id " << p.id << ")\n";

        dpp::embed em = base_embed().set_title("🛒 Fishing Shop")
            .add_field("Rods (`/shop rod tier:`)", rods.str(), false)
            .add_field("Bait (`/shop bait id:`)", baits.str(), false)
            .add_field("Boats (`/shop boat tier:`)", boats.str(), false)
            .add_field("Pets (`/shop pet id:`)", pets.str(), false);
        reply_embed(e, em);
        return;
    }

    if (sub == "rod") {
        int tier = (int)get_int(e, "tier").value_or(-1);
        const Rod* r = find_rod(tier);
        if (!r) { reply(e, "No such rod tier.", true); return; }
        if (tier <= u.rod_tier) { reply(e, "You already own an equal or better rod.", true); return; }
        if (u.balance < r->price) { reply(e, "You can't afford the **" + r->name + "** (" + fmt_coins(r->price) + ").", true); return; }
        u.balance -= r->price;
        u.rod_tier = tier;
        db.save_user(u);
        reply(e, "🎣 Bought the **" + r->name + "**! It's now equipped.");
        return;
    }

    if (sub == "bait") {
        int id_ = (int)get_int(e, "id").value_or(-1);
        int amount = (int)get_int(e, "amount").value_or(1);
        amount = std::max(1, amount);
        const Bait* b = find_bait(id_);
        if (!b) { reply(e, "No such bait.", true); return; }
        int64_t cost = (int64_t)b->price * amount;
        if (u.balance < cost) { reply(e, "You can't afford " + std::to_string(amount) + "x " + b->name + ".", true); return; }
        u.balance -= cost;
        u.bait_id = id_; // simplified: owning any amount equips it (no separate bait inventory count)
        db.save_user(u);
        reply(e, "🪱 Bought " + std::to_string(amount) + "x **" + b->name + "** and equipped it.");
        return;
    }

    if (sub == "boat") {
        int tier = (int)get_int(e, "tier").value_or(-1);
        const Boat* b = find_boat(tier);
        if (!b) { reply(e, "No such boat tier.", true); return; }
        if (tier <= u.boat_tier) { reply(e, "You already own an equal or better boat.", true); return; }
        if (u.balance < b->price) { reply(e, "You can't afford the **" + b->name + "** (" + fmt_coins(b->price) + ").", true); return; }
        u.balance -= b->price;
        u.boat_tier = tier;
        db.save_user(u);
        reply(e, "🚤 Bought the **" + b->name + "**! You can now reach deeper waters.");
        return;
    }

    if (sub == "pet") {
        int id_ = (int)get_int(e, "id").value_or(-1);
        const PetSpecies* p = find_pet(id_);
        if (!p) { reply(e, "No such pet.", true); return; }
        if (u.balance < p->price) { reply(e, "You can't afford **" + p->name + "**.", true); return; }
        u.balance -= p->price;
        u.pet_id = id_;
        u.pet_level = 1;
        u.pet_xp = 0;
        db.save_user(u);
        reply(e, p->emoji + " Adopted **" + p->name + "**! It'll help you fish from now on.");
        return;
    }
}

// ============================= /sell =============================

static void cmd_sell(dpp::cluster& bot, const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    UserRow u = db.get_or_create_user(id);
    Enchants en = db.get_enchants(id);   // Greedy Hook raises sell prices

    if (sub == "one") {
        int inv_id = (int)get_int(e, "inv_id").value_or(-1);
        auto item = db.get_inventory_item(inv_id);
        if (!item || item->user_id != id) { reply(e, "You don't own that inventory item.", true); return; }
        if (item->in_aquarium) { reply(e, "That fish is on display in your aquarium — remove it first with `/aquarium remove`.", true); return; }
        const FishSpecies* f = find_fish(item->fish_id);
        if (!f) { reply(e, "Unknown fish data — contact the bot owner.", true); return; }
        int price = apply_sell_bonus(market_sell_price(*f, item->weight), en.greedy);
        db.remove_inventory_item(inv_id);
        u.balance += price;
        db.save_user(u);
        db.bump_quest_progress(id, "sell_300", price);
        reply(e, "💰 Sold your **" + f->name + "** (" + fmt_weight(item->weight) + ") for " + fmt_coins(price) + ".");
        return;
    }

    if (sub == "all") {
        auto inv = db.get_inventory(id);
        int64_t total = 0;
        int count = 0;
        for (auto& item : inv) {
            if (item.in_aquarium) continue;
            const FishSpecies* f = find_fish(item.fish_id);
            if (!f) continue;
            total += apply_sell_bonus(market_sell_price(*f, item.weight), en.greedy);
            db.remove_inventory_item(item.inv_id);
            count++;
        }
        u.balance += total;
        db.save_user(u);
        if (count > 0) db.bump_quest_progress(id, "sell_300", (int)total);
        reply(e, count == 0 ? "You have nothing to sell (aquarium fish don't count)."
                             : "💰 Sold " + std::to_string(count) + " fish for a total of " + fmt_coins(total) + ".");
        return;
    }
}

// ============================= /trade =============================

static std::string serialize_offer(int64_t coins, const std::vector<int>& fish_inv_ids) {
    std::ostringstream ss;
    ss << "coins:" << coins << ";fish:";
    for (size_t i = 0; i < fish_inv_ids.size(); i++) {
        if (i) ss << ",";
        ss << fish_inv_ids[i];
    }
    return ss.str();
}

static void parse_offer(const std::string& s, int64_t& coins, std::vector<int>& fish_ids) {
    coins = 0;
    fish_ids.clear();
    auto pos = s.find("coins:");
    auto semi = s.find(';');
    if (pos != std::string::npos && semi != std::string::npos)
        coins = std::stoll(s.substr(pos + 6, semi - (pos + 6)));
    auto fpos = s.find("fish:");
    if (fpos == std::string::npos) return;
    std::string rest = s.substr(fpos + 5);
    std::stringstream ss(rest);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) fish_ids.push_back(std::stoi(tok));
    }
}

static void cmd_trade(dpp::cluster& bot, const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);

    if (sub == "start") {
        auto other = get_user(e, "user");
        if (!other) { reply(e, "Specify a user.", true); return; }
        std::string other_id = other->str();
        if (other_id == id) { reply(e, "You can't trade with yourself.", true); return; }
        int tid = db.create_trade(id, other_id);
        reply(e, "🤝 Trade #" + std::to_string(tid) + " started with <@" + other_id + ">.\n"
                  "Both sides use `/trade addcoins`, `/trade additem`, then `/trade confirm` with trade_id " +
                  std::to_string(tid) + ".");
        return;
    }

    int trade_id = (int)get_int(e, "trade_id").value_or(-1);
    auto t = db.get_trade(trade_id);
    if (!t) { reply(e, "No such trade.", true); return; }
    if (id != t->user_a && id != t->user_b) { reply(e, "You're not part of this trade.", true); return; }
    bool is_a = (id == t->user_a);

    if (sub == "addcoins") {
        int64_t amount = get_int(e, "amount").value_or(0);
        UserRow u = db.get_or_create_user(id);
        if (amount < 0 || amount > u.balance) { reply(e, "Invalid amount.", true); return; }
        int64_t coins; std::vector<int> fish;
        parse_offer(is_a ? t->offer_a : t->offer_b, coins, fish);
        coins = amount;
        (is_a ? t->offer_a : t->offer_b) = serialize_offer(coins, fish);
        t->a_confirmed = t->b_confirmed = false; // any change resets confirmations
        db.update_trade(*t);
        reply(e, "Updated your coin offer to " + fmt_coins(amount) + " in trade #" + std::to_string(trade_id) + ".");
        return;
    }

    if (sub == "additem") {
        int inv_id = (int)get_int(e, "inv_id").value_or(-1);
        auto item = db.get_inventory_item(inv_id);
        if (!item || item->user_id != id) { reply(e, "You don't own that inventory item.", true); return; }
        int64_t coins; std::vector<int> fish;
        parse_offer(is_a ? t->offer_a : t->offer_b, coins, fish);
        if (std::find(fish.begin(), fish.end(), inv_id) == fish.end()) fish.push_back(inv_id);
        (is_a ? t->offer_a : t->offer_b) = serialize_offer(coins, fish);
        t->a_confirmed = t->b_confirmed = false;
        db.update_trade(*t);
        reply(e, "Added inventory item #" + std::to_string(inv_id) + " to your offer.");
        return;
    }

    if (sub == "cancel") {
        db.delete_trade(trade_id);
        reply(e, "Trade #" + std::to_string(trade_id) + " cancelled.");
        return;
    }

    if (sub == "confirm") {
        if (is_a) t->a_confirmed = true; else t->b_confirmed = true;
        db.update_trade(*t);
        if (t->a_confirmed && t->b_confirmed) {
            // execute the trade atomically-ish
            int64_t coins_a, coins_b; std::vector<int> fish_a, fish_b;
            parse_offer(t->offer_a, coins_a, fish_a);
            parse_offer(t->offer_b, coins_b, fish_b);
            UserRow ua = db.get_or_create_user(t->user_a);
            UserRow ub = db.get_or_create_user(t->user_b);
            if (ua.balance < coins_a || ub.balance < coins_b) {
                reply(e, "Trade failed — someone can't cover their coin offer anymore.", true);
                db.delete_trade(trade_id);
                return;
            }
            bool ok = true;
            for (int inv : fish_a) { auto it = db.get_inventory_item(inv); if (!it || it->user_id != t->user_a) ok = false; }
            for (int inv : fish_b) { auto it = db.get_inventory_item(inv); if (!it || it->user_id != t->user_b) ok = false; }
            if (!ok) {
                reply(e, "Trade failed — an offered fish is no longer available.", true);
                db.delete_trade(trade_id);
                return;
            }
            ua.balance = ua.balance - coins_a + coins_b;
            ub.balance = ub.balance - coins_b + coins_a;
            db.save_user(ua);
            db.save_user(ub);
            for (int inv : fish_a) {
                auto it = db.get_inventory_item(inv);
                db.remove_inventory_item(inv);
                db.add_fish(t->user_b, it->fish_id, it->weight);
            }
            for (int inv : fish_b) {
                auto it = db.get_inventory_item(inv);
                db.remove_inventory_item(inv);
                db.add_fish(t->user_a, it->fish_id, it->weight);
            }
            db.delete_trade(trade_id);
            reply(e, "✅ Trade #" + std::to_string(trade_id) + " completed between <@" + t->user_a +
                     "> and <@" + t->user_b + ">!");
        } else {
            reply(e, "Your side of trade #" + std::to_string(trade_id) + " is confirmed. Waiting on the other player.");
        }
        return;
    }
}

// ============================= misc read commands =============================

static void cmd_inventory(const dpp::slashcommand_t& e, Database& db) {
    std::string id = uid(e);
    auto inv = db.get_inventory(id);
    if (inv.empty()) { reply(e, "Your inventory is empty. Go `/fish`!", true); return; }
    std::ostringstream ss;
    int shown = 0;
    for (auto& item : inv) {
        if (shown >= 20) { ss << "...and " << (inv.size() - shown) << " more."; break; }
        const FishSpecies* f = find_fish(item.fish_id);
        if (!f) continue;
        ss << "#" << item.inv_id << " " << f->emoji << " " << f->name << " (" << fmt_weight(item.weight) << ")"
           << (item.in_aquarium ? " 🏺 [aquarium]" : "") << "\n";
        shown++;
    }
    reply_embed(e, base_embed().set_title("🎒 Inventory (" + std::to_string(inv.size()) + " fish)").set_description(ss.str()), true);
}

static void cmd_balance(const dpp::slashcommand_t& e, Database& db) {
    Lang L = lang_of(e, db);
    UserRow u = db.get_or_create_user(uid(e));
    reply(e, trf(L, "balance.show", {{"coins", fmt_coins_l(L, u.balance)}}));
}

static void cmd_daily(const dpp::slashcommand_t& e, Database& db) {
    Lang L = lang_of(e, db);
    UserRow u = db.get_or_create_user(uid(e));
    int64_t now = (int64_t)time(nullptr);
    int64_t since = now - u.last_daily;
    if (u.last_daily != 0 && since < 20 * 3600) {
        reply(e, trf(L, "daily.cooldown", {{"hours", std::to_string((20 * 3600 - since) / 3600 + 1)}}), true);
        return;
    }
    if (since > 48 * 3600 && u.last_daily != 0) u.daily_streak = 0; // streak broken
    u.daily_streak++;
    int64_t reward = 100 + (int64_t)std::min(u.daily_streak, 30) * 20;
    u.balance += reward;
    u.last_daily = now;
    db.save_user(u);
    if (u.daily_streak % 10 == 0) db.unlock_achievement(uid(e), "ten_quests"); // streak milestone bonus tie-in
    reply(e, trf(L, "daily.claimed", {{"coins", fmt_coins_l(L, reward)}, {"streak", std::to_string(u.daily_streak)}}));
}

static void cmd_leaderboard(const dpp::slashcommand_t& e, Database& db) {
    std::string type = get_str(e, "type").value_or("balance");
    std::ostringstream ss;
    if (type == "fish") {
        auto rows = db.top_by_fish_count(10);
        int i = 1;
        for (auto& r : rows) ss << i++ << ". <@" << r.user_id << "> — " << r.xp << " fish\n";
        reply_embed(e, base_embed().set_title("🏆 Most Fish Caught").set_description(ss.str().empty() ? "No data yet." : ss.str()));
    } else if (type == "tournament") {
        auto rows = db.tournament_leaderboard(10);
        int i = 1;
        for (auto& r : rows) ss << i++ << ". <@" << r.first << "> — " << fmt_weight(r.second) << " total weight\n";
        reply_embed(e, base_embed().set_title("🏆 Tournament Leaderboard").set_description(ss.str().empty() ? "No tournament activity yet." : ss.str()));
    } else {
        auto rows = db.top_by_balance(10);
        int i = 1;
        for (auto& r : rows) ss << i++ << ". <@" << r.user_id << "> — " << fmt_coins(r.balance) << "\n";
        reply_embed(e, base_embed().set_title("🏆 Richest Anglers").set_description(ss.str().empty() ? "No data yet." : ss.str()));
    }
}

static void cmd_profile(const dpp::slashcommand_t& e, Database& db) {
    auto target = get_user(e, "user");
    std::string id = target ? target->str() : uid(e);
    UserRow u = db.get_or_create_user(id);
    const Rod* rod = find_rod(u.rod_tier);
    const Boat* boat = u.boat_tier >= 0 ? find_boat(u.boat_tier) : nullptr;
    const Bait* bait = u.bait_id >= 0 ? find_bait(u.bait_id) : nullptr;
    const PetSpecies* pet = u.pet_id >= 0 ? find_pet(u.pet_id) : nullptr;
    int64_t next_lvl_xp = xp_needed_for_level(u.level + 1);

    dpp::embed em = base_embed().set_title("🎣 Angler Profile: <@" + id + ">")
        .add_field("Level", std::to_string(u.level) + " (" + std::to_string(u.xp) + " / " + std::to_string(next_lvl_xp) + " xp)", true)
        .add_field("Balance", fmt_coins(u.balance), true)
        .add_field("Location", u.location, true)
        .add_field("Rod", rod ? rod->name : "None", true)
        .add_field("Boat", boat ? boat->name : "None", true)
        .add_field("Bait", bait ? bait->name : "None", true)
        .add_field("Pet", pet ? (pet->emoji + " " + pet->name + " (Lv." + std::to_string(u.pet_level) + ")") : "None", true)
        .add_field("Fish Caught", std::to_string(db.count_fish(id)), true)
        .add_field("Heaviest Catch", fmt_weight(db.heaviest_catch(id)), true);
    reply_embed(e, em);
}

static void cmd_fishdex(const dpp::slashcommand_t& e, Database& db) {
    std::string id = uid(e);
    auto discovered = db.get_discovered(id);
    std::ostringstream ss;
    for (auto& f : all_fish()) {
        bool got = std::find(discovered.begin(), discovered.end(), f.id) != discovered.end();
        ss << (got ? f.emoji : "❔") << " " << (got ? f.name : "???") << " — " << rarity_name(f.rarity)
           << " (" << f.location << ")" << (!f.event_key.empty() ? " 🎉" : "") << "\n";
    }
    reply_embed(e, base_embed().set_title("📖 Fishdex — " + std::to_string(discovered.size()) + " / " +
        std::to_string(all_fish().size()) + " discovered").set_description(ss.str()), true);
}

static void cmd_achievements(const dpp::slashcommand_t& e, Database& db) {
    std::string id = uid(e);
    auto have = db.get_achievements(id);
    std::ostringstream ss;
    for (auto& a : achievement_pool()) {
        bool unlocked = std::find(have.begin(), have.end(), a.key) != have.end();
        ss << (unlocked ? "✅" : "❌") << " **" << a.name << "** — " << a.description << "\n";
    }
    reply_embed(e, base_embed().set_title("🏆 Achievements").set_description(ss.str()), true);
}

static void cmd_quest(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    auto q = db.get_active_quest(id);
    int64_t now = (int64_t)time(nullptr);
    bool needs_new = !q || (now - q->assigned_at > 24 * 3600);
    if (needs_new) {
        auto& def = random_quest();
        Database::QuestRow nq;
        nq.user_id = id;
        nq.quest_key = def.key;
        nq.progress = 0;
        nq.target = def.target;
        nq.claimed = false;
        nq.assigned_at = now;
        db.set_quest(nq);
        q = nq;
    }

    const QuestDef* def = find_quest(q->quest_key);
    if (sub == "claim") {
        if (q->claimed) { reply(e, "You've already claimed today's quest.", true); return; }
        if (q->progress < q->target) { reply(e, "Quest not complete yet: " + std::to_string(q->progress) + "/" + std::to_string(q->target), true); return; }
        UserRow u = db.get_or_create_user(id);
        u.balance += def->reward_coins;
        db.save_user(u);
        q->claimed = true;
        db.set_quest(*q);
        reply(e, "🎁 Quest complete! You earned **" + fmt_coins(def->reward_coins) + "**.");
        return;
    }

    reply_embed(e, base_embed().set_title("📜 Daily Quest")
        .set_description((def ? def->description : q->quest_key) + "\nProgress: **" +
                          std::to_string(q->progress) + " / " + std::to_string(q->target) + "**" +
                          (q->claimed ? "\n✅ Reward claimed." :
                           (q->progress >= q->target ? "\nUse `/quest claim` to collect your reward!" : ""))), true);
}

static void cmd_location(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    UserRow u = db.get_or_create_user(id);

    if (sub == "travel") {
        std::string name = get_str(e, "name").value_or("");
        const Location* loc = find_location(name);
        if (!loc) { reply(e, "Unknown location.", true); return; }
        if (u.rod_tier < loc->min_rod_tier) { reply(e, "You need a better rod to fish there. Check `/shop view`.", true); return; }
        if (loc->min_boat_tier >= 0 && u.boat_tier < loc->min_boat_tier) { reply(e, "You need a boat to reach there. Check `/shop boat`.", true); return; }
        u.location = loc->name;
        db.save_user(u);
        reply(e, loc->emoji + " You travel to **" + loc->name + "**. " + loc->desc);
        return;
    }

    std::ostringstream ss;
    for (auto& l : all_locations()) {
        ss << l.emoji << " **" << l.name << "** — " << l.desc
           << " (needs rod tier " << l.min_rod_tier << (l.min_boat_tier >= 0 ? ", boat tier " + std::to_string(l.min_boat_tier) : "") << ")\n";
    }
    reply_embed(e, base_embed().set_title("🗺️ Fishing Locations").set_description(ss.str()));
}

static void cmd_market(const dpp::slashcommand_t& e) {
    std::ostringstream ss;
    for (auto& f : all_fish()) {
        double mult = market_multiplier(f.id);
        ss << f.emoji << " " << f.name << " — " << (int)std::round(f.base_value * mult) << " coins/kg"
           << (mult > 1.1 ? " 📈" : (mult < 0.9 ? " 📉" : "")) << "\n";
    }
    reply_embed(e, base_embed().set_title("📊 Today's Fish Market").set_description(ss.str()));
}

static void cmd_weather(const dpp::slashcommand_t& e) {
    Weather w = WorldState::instance().weather();
    reply(e, std::string(weather_emoji(w)) + " Current weather: **" + weather_name(w) + "** — " +
        (w == Weather::Stormy ? "rare deep-water fish are more likely to bite!" :
         w == Weather::Rainy ? "fish are biting a bit more than usual." :
         "fishing conditions are normal."));
}

static void cmd_pet(const dpp::slashcommand_t& e, Database& db) {
    UserRow u = db.get_or_create_user(uid(e));
    if (u.pet_id < 0) { reply(e, "You don't have a pet yet — adopt one with `/shop pet`.", true); return; }
    const PetSpecies* p = find_pet(u.pet_id);
    reply_embed(e, base_embed().set_title(p->emoji + " " + p->name)
        .add_field("Level", std::to_string(u.pet_level), true)
        .add_field("XP", std::to_string(u.pet_xp), true)
        .add_field("Catch bonus", std::to_string((int)std::round(p->catch_bonus * 100)) + "% (scales with level)", true));
}

static void cmd_gift(const dpp::slashcommand_t& e, Database& db) {
    auto target = get_user(e, "user");
    int64_t amount = get_int(e, "amount").value_or(0);
    if (!target) { reply(e, "Specify a user.", true); return; }
    std::string tid = target->str();
    std::string id = uid(e);
    if (tid == id) { reply(e, "You can't gift yourself.", true); return; }
    if (amount <= 0) { reply(e, "Amount must be positive.", true); return; }
    UserRow u = db.get_or_create_user(id);
    if (u.balance < amount) { reply(e, "You don't have that much.", true); return; }
    UserRow t = db.get_or_create_user(tid);
    u.balance -= amount;
    t.balance += amount;
    db.save_user(u);
    db.save_user(t);
    reply(e, "🎁 Gifted " + fmt_coins(amount) + " to <@" + tid + ">.");
}

static void cmd_aquarium(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);

    if (sub == "add" || sub == "remove") {
        int inv_id = (int)get_int(e, "inv_id").value_or(-1);
        auto item = db.get_inventory_item(inv_id);
        if (!item || item->user_id != id) { reply(e, "You don't own that inventory item.", true); return; }
        db.set_aquarium(inv_id, sub == "add");
        reply(e, sub == "add" ? "🏺 Added to your aquarium display." : "Removed from your aquarium (sellable again).");
        return;
    }

    auto items = db.get_inventory(id, true);
    if (items.empty()) { reply(e, "Your aquarium is empty. Use `/aquarium add`.", true); return; }
    std::ostringstream ss;
    for (auto& item : items) {
        const FishSpecies* f = find_fish(item.fish_id);
        if (!f) continue;
        ss << f->emoji << " " << f->name << " (" << fmt_weight(item.weight) << ")\n";
    }
    reply_embed(e, base_embed().set_title("🏺 Your Aquarium").set_description(ss.str()));
}

static void cmd_tournament(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    auto& world = WorldState::instance();

    (void)db;
    (void)sub;

    if (!world.tournament_active()) { reply(e, "No tournament is currently running."); return; }
    int64_t remaining = world.tournament_ends_at() - (int64_t)time(nullptr);
    reply(e, "🏁 Tournament active at **" + world.tournament_location() + "**, ends in " +
        std::to_string(std::max<int64_t>(0, remaining) / 60) + " minute(s).");
}

static void cmd_event(const dpp::slashcommand_t& e, Database& db) {
    (void)db;
    std::string sub = get_subcommand(e);
    auto& world = WorldState::instance();

    (void)sub;

    if (!world.event_active()) { reply(e, "No seasonal event is currently running."); return; }
    int64_t remaining = world.event_ends_at() - (int64_t)time(nullptr);
    reply(e, "🎉 **" + world.event_name() + "** active (key: `" + world.event_key() + "`), ends in " +
        std::to_string(std::max<int64_t>(0, remaining) / 3600) + " hour(s).");
}

// ============================= /crew =============================

static void cmd_crew(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    UserRow u = db.get_or_create_user(id);

    if (sub == "create") {
        if (u.crew_id >= 0) { reply(e, "Leave your current crew first with `/crew leave`.", true); return; }
        std::string name = get_str(e, "name").value_or("");
        if (name.empty() || name.size() > 32) { reply(e, "Crew names must be 1-32 characters.", true); return; }
        auto crew_id = db.create_crew(name, id);
        if (!crew_id) { reply(e, "That crew name is already taken.", true); return; }
        db.join_crew(id, *crew_id);
        db.unlock_achievement(id, "crew_founder");
        reply(e, "⚓ Founded the crew **" + name + "**! Others can join with `/crew join name:" + name + "`.");
        return;
    }

    if (sub == "join") {
        if (u.crew_id >= 0) { reply(e, "Leave your current crew first with `/crew leave`.", true); return; }
        std::string name = get_str(e, "name").value_or("");
        auto crew = db.get_crew_by_name(name);
        if (!crew) { reply(e, "No crew named **" + name + "**.", true); return; }
        db.join_crew(id, crew->crew_id);
        reply(e, "⚓ Joined **" + crew->name + "**!");
        return;
    }

    if (sub == "leave") {
        if (u.crew_id < 0) { reply(e, "You're not in a crew.", true); return; }
        auto crew = db.get_crew(u.crew_id);
        if (crew && crew->owner_id == id && db.count_crew_members(u.crew_id) > 1) {
            reply(e, "You're the owner of a crew with other members — `/crew kick` everyone else first, or `/crew disband`.", true);
            return;
        }
        if (crew && crew->owner_id == id) { db.delete_crew(u.crew_id); reply(e, "Crew disbanded (you were the only member)."); return; }
        db.leave_crew(id);
        reply(e, "You left your crew.");
        return;
    }

    if (sub == "info") {
        std::string name = get_str(e, "name").value_or("");
        std::optional<CrewRow> crew;
        if (!name.empty()) crew = db.get_crew_by_name(name);
        else if (u.crew_id >= 0) crew = db.get_crew(u.crew_id);
        if (!crew) { reply(e, "No such crew — or you're not in one, so specify a name.", true); return; }
        auto members = db.get_crew_member_ids(crew->crew_id);
        std::ostringstream ss;
        for (auto& m : members) ss << "<@" << m << ">" << (m == crew->owner_id ? " 👑" : "") << "\n";
        reply_embed(e, base_embed().set_title("⚓ Crew: " + crew->name)
            .add_field("Owner", "<@" + crew->owner_id + ">", true)
            .add_field("Bank", fmt_coins(crew->bank), true)
            .add_field("Members (" + std::to_string(members.size()) + ")", ss.str().empty() ? "None" : ss.str(), false));
        return;
    }

    if (sub == "deposit") {
        if (u.crew_id < 0) { reply(e, "You're not in a crew.", true); return; }
        int64_t amount = get_int(e, "amount").value_or(0);
        if (amount <= 0 || amount > u.balance) { reply(e, "Invalid amount.", true); return; }
        u.balance -= amount;
        db.save_user(u);
        db.adjust_crew_bank(u.crew_id, amount);
        reply(e, "Deposited " + fmt_coins(amount) + " into the crew bank.");
        return;
    }

    if (sub == "withdraw") {
        if (u.crew_id < 0) { reply(e, "You're not in a crew.", true); return; }
        auto crew = db.get_crew(u.crew_id);
        if (!crew || crew->owner_id != id) { reply(e, "Only the crew owner can withdraw.", true); return; }
        int64_t amount = get_int(e, "amount").value_or(0);
        if (amount <= 0 || amount > crew->bank) { reply(e, "Invalid amount.", true); return; }
        db.adjust_crew_bank(u.crew_id, -amount);
        u.balance += amount;
        db.save_user(u);
        reply(e, "Withdrew " + fmt_coins(amount) + " from the crew bank.");
        return;
    }

    if (sub == "kick") {
        if (u.crew_id < 0) { reply(e, "You're not in a crew.", true); return; }
        auto crew = db.get_crew(u.crew_id);
        if (!crew || crew->owner_id != id) { reply(e, "Only the crew owner can kick members.", true); return; }
        auto target = get_user(e, "user");
        if (!target) { reply(e, "Specify a user.", true); return; }
        std::string tid = target->str();
        if (tid == id) { reply(e, "You can't kick yourself — use `/crew disband` instead.", true); return; }
        UserRow t = db.get_or_create_user(tid);
        if (t.crew_id != u.crew_id) { reply(e, "That user isn't in your crew.", true); return; }
        db.leave_crew(tid);
        reply(e, "<@" + tid + "> was removed from the crew.");
        return;
    }

    if (sub == "disband") {
        if (u.crew_id < 0) { reply(e, "You're not in a crew.", true); return; }
        auto crew = db.get_crew(u.crew_id);
        if (!crew || crew->owner_id != id) { reply(e, "Only the crew owner can disband it.", true); return; }
        db.delete_crew(u.crew_id);
        reply(e, "Crew **" + crew->name + "** has been disbanded.");
        return;
    }

    if (sub == "leaderboard") {
        auto crews = db.top_crews_by_bank(10);
        std::ostringstream ss;
        int i = 1;
        for (auto& c : crews) ss << i++ << ". **" << c.name << "** — " << fmt_coins(c.bank) << "\n";
        reply_embed(e, base_embed().set_title("⚓ Richest Crews").set_description(ss.str().empty() ? "No crews yet." : ss.str()));
        return;
    }
}

// ============================= /boss + World Boss button =============================

static void cmd_boss(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string guild_id = e.command.guild_id.str();

    (void)db;
    (void)sub;

    // status
    auto sess = BossSessionManager::instance().get(guild_id);
    if (!sess) { reply(e, "No World Boss is currently active here."); return; }
    std::vector<std::pair<std::string,double>> ranked(sess->contributions.begin(), sess->contributions.end());
    std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b){ return a.second > b.second; });
    std::ostringstream ss;
    int shown = 0;
    for (auto& [uid_, dmg] : ranked) {
        if (shown++ >= 5) break;
        ss << "<@" << uid_ << "> — " << (int)std::round(dmg) << " dmg\n";
    }
    int64_t remaining = sess->expires_at - (int64_t)time(nullptr);
    reply_embed(e, base_embed().set_title(sess->fish->name + " " + sess->fish->emoji)
        .set_description("HP: " + render_health_bar(sess->current_hp, sess->total_hp) +
                          "\nTime left: " + std::to_string(std::max<int64_t>(0, remaining) / 60) + " min")
        .add_field("Top damage", ss.str().empty() ? "No hits yet." : ss.str(), false));
}

static void announce_boss_end(dpp::cluster& bot, const std::string& channel_id, const dpp::message& m) {
    dpp::message msg = m;
    msg.channel_id = dpp::snowflake(channel_id);
    bot.message_create(msg);
}

// Called both from the button handler (instant feedback) and the sweep timer (if nobody
// was around to click when time ran out).
static void resolve_boss_timeout(dpp::cluster& bot, Database& db, const std::string& guild_id) {
    auto sess_opt = BossSessionManager::instance().get(guild_id);
    if (!sess_opt) return;
    BossSession sess = *sess_opt;
    BossSessionManager::instance().erase(guild_id);

    std::ostringstream summary;
    summary << "💨 The **" << sess.fish->name << "** slipped away after 10 minutes!";
    if (!sess.contributions.empty()) {
        summary << " Everyone who attacked it gets a small consolation reward.";
        for (auto& [uid_, dmg_] : sess.contributions) {
            UserRow p = db.get_or_create_user(uid_);
            p.balance += 50;
            grant_xp_and_maybe_levelup(db, p, 10);
        }
    }
    dpp::message m;
    m.add_embed(base_embed().set_title("Boss Escaped " + sess.fish->emoji).set_description(summary.str()).set_color(0x95a5a6));
    announce_boss_end(bot, sess.channel_id, m);
}

static void handle_boss_click(dpp::cluster& bot, const dpp::button_click_t& e, Database& db, const std::string& guild_id) {
    auto sess_opt = BossSessionManager::instance().get(guild_id);
    if (!sess_opt) {
        e.reply(dpp::ir_channel_message_with_source,
                dpp::message("This World Boss fight has already ended.").set_flags(dpp::m_ephemeral));
        return;
    }
    BossSession sess = *sess_opt;
    std::string clicker = e.command.get_issuing_user().id.str();
    int64_t now = (int64_t)time(nullptr);

    if (now > sess.expires_at) {
        resolve_boss_timeout(bot, db, guild_id);
        e.reply(dpp::ir_update_message, dpp::message("💨 Too slow — the boss just slipped away!"));
        return;
    }

    auto it = sess.last_hit.find(clicker);
    if (it != sess.last_hit.end() && now - it->second < 2) {
        e.reply(dpp::ir_channel_message_with_source,
                dpp::message("Wait a moment before attacking again!").set_flags(dpp::m_ephemeral));
        return;
    }
    sess.last_hit[clicker] = now;

    UserRow u = db.get_or_create_user(clicker);
    double pet_bonus = 0.0;
    if (u.pet_id >= 0) {
        if (auto* p = find_pet(u.pet_id)) pet_bonus = p->catch_bonus * (1.0 + 0.05 * (u.pet_level - 1));
    }
    double dmg = boss_damage_per_hit(u.rod_tier, pet_bonus);
    sess.current_hp -= dmg;
    sess.contributions[clicker] += dmg;

    if (sess.current_hp > 0) {
        BossSessionManager::instance().update(guild_id, sess);
        dpp::message m;
        m.add_embed(base_embed().set_title(sess.fish->name + " " + sess.fish->emoji)
            .set_description("HP: " + render_health_bar(sess.current_hp, sess.total_hp) +
                              "\n<@" + clicker + "> hits for **" + std::to_string((int)std::round(dmg)) + "** damage!")
            .set_color(0xe74c3c));
        dpp::component row;
        row.add_component(dpp::component().set_type(dpp::cot_button).set_label("Attack! ⚔️")
            .set_style(dpp::cos_danger).set_id("boss_" + guild_id));
        m.add_component(row);
        e.reply(dpp::ir_update_message, m);
        return;
    }

    // Boss defeated!
    BossSessionManager::instance().erase(guild_id);
    std::string top_user;
    double top_dmg = -1;
    double total_dmg = 0;
    for (auto& [uid_, dmg_] : sess.contributions) {
        total_dmg += dmg_;
        if (dmg_ > top_dmg) { top_dmg = dmg_; top_user = uid_; }
    }

    int64_t coin_pool = (int64_t)std::round(sess.total_hp * 5);
    std::ostringstream summary;
    summary << "🎉 The **" << sess.fish->name << "** has been defeated by " << sess.contributions.size() << " angler(s)!\n";
    for (auto& [uid_, dmg_] : sess.contributions) {
        double share = total_dmg > 0 ? dmg_ / total_dmg : 0;
        int64_t coins = (int64_t)std::round(coin_pool * share);
        int xp = (int)std::round(50 * share) + 10;
        UserRow participant = db.get_or_create_user(uid_);
        participant.balance += coins;
        grant_xp_and_maybe_levelup(db, participant, xp);
        db.unlock_achievement(uid_, "boss_slayer");
        if (uid_ == top_user) {
            db.add_fish(uid_, sess.fish->id, sess.weight);
            db.mark_discovered(uid_, sess.fish->id);
            db.unlock_achievement(uid_, "boss_champion");
        }
    }
    summary << "🏆 Top damage: <@" << top_user << "> (" << (int)std::round(top_dmg) << " dmg) — claims the "
            << sess.fish->name << "!\n💰 " << fmt_coins(coin_pool) << " split among all participants by contribution.";

    dpp::message m;
    m.add_embed(base_embed().set_title("Boss Defeated! " + sess.fish->emoji).set_description(summary.str()).set_color(0x2ecc71));
    e.reply(dpp::ir_update_message, m);
}

static void cmd_help(const dpp::slashcommand_t& e, Database& db) {
    Lang L = lang_of(e, db);
    dpp::embed em = base_embed().set_title(tr(L, "help.title")).set_description(tr(L, "help.body"));
    e.reply(dpp::message().add_embed(em).set_flags(dpp::m_ephemeral));
}

// ============================= /stats, /terms, /privacy, /language =============================

static std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

// "• **3,095,552** bits / • **386,944** bytes / • 377.88 KB / • 0.3690 MB / • 0.000360 GB"
static std::string size_lines(Lang L, uint64_t bytes) {
    SizeBreakdown b = size_breakdown(bytes);
    std::ostringstream o;
    o << "• **" << fmt_number(b.bits) << "** " << tr(L, "stats.bits") << "\n"
      << "• **" << fmt_number(b.bytes) << "** " << tr(L, "stats.bytes") << "\n"
      << "• **" << fixed(b.kb, 2) << "** KB\n"
      << "• **" << fixed(b.mb, 4) << "** MB\n"
      << "• **" << fixed(b.gb, 6) << "** GB";
    return o.str();
}

static void cmd_stats(dpp::cluster& bot, const dpp::slashcommand_t& e, Database& db) {
    Lang L = lang_of(e, db);
    const std::string na = tr(L, "stats.na");
    auto line = [](const std::string& label, const std::string& value) {
        return "**" + label + ":** " + value + "\n";
    };

    ProcStats p = read_proc_stats(250);   // blocks ~250 ms to measure CPU
    HostStats h = read_host_stats();
    auto counts = db.table_row_counts();
    auto info = db.db_info();

    auto count_of = [&](const std::string& table) -> int64_t {
        for (auto& c : counts) if (c.first == table) return c.second;
        return 0;
    };

    // ---- Bot ----
    std::string bot_txt;
    bot_txt += line(tr(L, "stats.uptime"), fmt_uptime(bot_uptime_seconds()));
    bot_txt += line(tr(L, "stats.servers"), fmt_number(dpp::get_guild_cache()->count()));
    bot_txt += line(tr(L, "stats.players"), fmt_number((uint64_t)count_of("users")));
    bot_txt += line(tr(L, "stats.fish_total"), fmt_number((uint64_t)count_of("inventory")));
    bot_txt += line(tr(L, "stats.cmds"), fmt_number(g_commands_run.load()));
    bot_txt += line(tr(L, "stats.ping"), std::to_string((long)std::lround(bot.rest_ping * 1000.0)) + " ms");
    bot_txt += line(tr(L, "stats.shards"), std::to_string(bot.numshards));
#ifdef DPP_VERSION_TEXT
    bot_txt += line(tr(L, "stats.lib"), DPP_VERSION_TEXT);
#endif

    // ---- Process & CPU ----
    std::string proc_txt;
    if (p.supported) {
        proc_txt += line(tr(L, "stats.pid"), std::to_string(p.pid));
        proc_txt += line(tr(L, "stats.threads"), std::to_string(p.threads));
        proc_txt += line(tr(L, "stats.cpu"),
            trf(L, "stats.cpu.fmt", {{"core", fixed(p.cpu_percent_core, 1)},
                                     {"total", fixed(p.cpu_percent_total, 1)},
                                     {"cores", std::to_string(h.cores > 0 ? h.cores : 1)}}));
        proc_txt += line(tr(L, "stats.cputime"), fmt_uptime(p.cpu_time_ms / 1000));
    } else {
        proc_txt = na;
    }

    // ---- Memory (this bot process) ----
    std::string mem_txt;
    if (p.supported) {
        std::string ram = fmt_bytes(p.rss_bytes);
        if (h.mem_total > 0)
            ram += " (" + fixed(100.0 * (double)p.rss_bytes / (double)h.mem_total, 2) + "%)";
        mem_txt += line(tr(L, "stats.ram"), ram);
        mem_txt += line(tr(L, "stats.vv"), fmt_bytes(p.vsize_bytes));
        mem_txt += line(tr(L, "stats.vvpeak"), fmt_bytes(p.vpeak_bytes));
        mem_txt += line(tr(L, "stats.swap"), fmt_bytes(p.swap_bytes));
    } else {
        mem_txt = na;
    }

    // ---- Host ----
    std::string host_txt;
    if (h.supported) {
        if (!h.os.empty()) host_txt += line(tr(L, "stats.os"), h.os);
        if (!h.cpu_model.empty()) host_txt += line(tr(L, "stats.cpumodel"), h.cpu_model);
        host_txt += line(tr(L, "stats.cores"), std::to_string(h.cores));
        host_txt += line(tr(L, "stats.load"),
                         fixed(h.load1, 2) + " / " + fixed(h.load5, 2) + " / " + fixed(h.load15, 2));
        uint64_t used = h.mem_total > h.mem_available ? h.mem_total - h.mem_available : 0;
        host_txt += line(tr(L, "stats.hostram"), fmt_bytes(used) + " / " + fmt_bytes(h.mem_total));
        if (h.swap_total > 0) {
            uint64_t sused = h.swap_total > h.swap_free ? h.swap_total - h.swap_free : 0;
            host_txt += line(tr(L, "stats.hostswap"), fmt_bytes(sused) + " / " + fmt_bytes(h.swap_total));
        }
        host_txt += line(tr(L, "stats.hostup"), fmt_uptime(h.uptime_seconds));
    } else {
        host_txt = na;
    }

    // ---- Database size ----
    // SQLite runs in WAL mode, so recent writes live in the -wal file until a checkpoint;
    // the real size on disk is main + wal + shm.
    uint64_t db_main = file_size_or_zero(db.path());
    uint64_t db_side = file_size_or_zero(db.path() + "-wal") + file_size_or_zero(db.path() + "-shm");
    uint64_t db_total = db_main + db_side;
    std::string db_txt;
    db_txt += line(tr(L, "stats.dbmain"), fmt_bytes(db_main));
    db_txt += line(tr(L, "stats.dbwal"), fmt_bytes(db_side));
    db_txt += "**" + tr(L, "stats.dbtotal") + ":**\n" + size_lines(L, db_total);

    std::string dbinfo_txt;
    dbinfo_txt += line(tr(L, "stats.sqlite"), info.sqlite_version);
    dbinfo_txt += line(tr(L, "stats.pages"), fmt_number((uint64_t)info.page_size) + " B × " +
                                              fmt_number((uint64_t)info.page_count));
    dbinfo_txt += line(tr(L, "stats.freepages"), fmt_number((uint64_t)info.freelist_count));
    dbinfo_txt += line(tr(L, "stats.sqlmem"), fmt_bytes((uint64_t)info.sqlite_mem_used));

    std::string rows_txt;
    for (auto& c : counts) rows_txt += "`" + c.first + "` " + fmt_number((uint64_t)c.second) + "\n";

    dpp::embed em = base_embed()
        .set_title(tr(L, "stats.title"))
        .add_field(tr(L, "stats.sec.bot"), bot_txt, true)
        .add_field(tr(L, "stats.sec.proc"), proc_txt, true)
        .add_field(tr(L, "stats.sec.mem"), mem_txt, true)
        .add_field(tr(L, "stats.sec.host"), host_txt, false)
        .add_field(tr(L, "stats.sec.db"), db_txt, true)
        .add_field(tr(L, "stats.sec.dbinfo"), dbinfo_txt, true)
        .add_field(tr(L, "stats.sec.rows"), rows_txt, true)
        .set_footer(dpp::embed_footer().set_text(tr(L, "stats.footer")))
        .set_timestamp(time(nullptr));
    reply_embed(e, em, true);
}

// Date shown on the in-bot Terms / Privacy summaries - bump it when you change the text.
static const char* const LEGAL_LAST_UPDATED = "2026-10-05";

static void send_legal(const dpp::slashcommand_t& e, Database& db,
                       const std::string& title_key, const std::string& body_key, const char* url_env) {
    Lang L = lang_of(e, db);
    std::string url = env_or(url_env);
    std::string full = url.empty() ? "" : trf(L, "legal.full", {{"url", url}});
    std::string contact = env_or("BOT_CONTACT", tr(L, "legal.contact.default"));
    std::string body = trf(L, body_key, {{"date", LEGAL_LAST_UPDATED}, {"contact", contact}, {"full", full}});
    dpp::embed em = base_embed().set_title(tr(L, title_key)).set_description(body);
    reply_embed(e, em, true);
}

static void cmd_terms(const dpp::slashcommand_t& e, Database& db) {
    send_legal(e, db, "terms.title", "terms.body", "TOS_URL");
}

static void cmd_privacy(const dpp::slashcommand_t& e, Database& db) {
    send_legal(e, db, "privacy.title", "privacy.body", "PRIVACY_URL");
}

static void cmd_language(const dpp::slashcommand_t& e, Database& db) {
    std::string id = uid(e);
    Lang discord_lang = lang_from_discord_locale(e.command.locale);
    auto choice = get_str(e, "language");

    if (!choice) {   // just show the current setting
        std::string saved = db.get_lang(id);
        auto l = parse_lang(saved);
        Lang cur = l.value_or(discord_lang);
        reply(e, "**" + tr(cur, "lang.title") + "**\n" +
                 trf(cur, "lang.show", {{"lang", lang_native_name(cur)},
                                        {"mode", tr(cur, l ? "lang.mode.manual" : "lang.mode.auto")}}), true);
        return;
    }
    if (*choice == "auto") {
        db.set_lang(id, "");
        reply(e, trf(discord_lang, "lang.auto_set", {{"lang", lang_native_name(discord_lang)}}), true);
        return;
    }
    auto l = parse_lang(*choice);
    if (!l) { reply(e, "⚠️ en / vi / zh", true); return; }
    db.set_lang(id, lang_code(*l));
    reply(e, trf(*l, "lang.set", {{"lang", lang_native_name(*l)}}), true);
}

// ============================= dispatcher =============================

void handle_slashcommand(dpp::cluster& bot, const dpp::slashcommand_t& event, Database& db) {
    // Reject anything not authorised by a server install (e.g. the bot was
    // installed to someone's account and used in a server it isn't a member of).
    if (!event.command.is_guild_interaction()) {
        dpp::message m("This bot only works in servers it has been added to. Ask a server admin to invite it.");
        m.set_flags(dpp::m_ephemeral);
        event.reply(m);
        return;
    }
    const std::string& cmd = event.command.get_command_name();
    g_commands_run++;
    if (cmd == "fish") cmd_fish(bot, event, db);
    else if (cmd == "shop") cmd_shop(bot, event, db);
    else if (cmd == "sell") cmd_sell(bot, event, db);
    else if (cmd == "trade") cmd_trade(bot, event, db);
    else if (cmd == "inventory") cmd_inventory(event, db);
    else if (cmd == "balance") cmd_balance(event, db);
    else if (cmd == "daily") cmd_daily(event, db);
    else if (cmd == "leaderboard") cmd_leaderboard(event, db);
    else if (cmd == "profile") cmd_profile(event, db);
    else if (cmd == "fishdex") cmd_fishdex(event, db);
    else if (cmd == "achievements") cmd_achievements(event, db);
    else if (cmd == "quest") cmd_quest(event, db);
    else if (cmd == "location") cmd_location(event, db);
    else if (cmd == "market") cmd_market(event);
    else if (cmd == "weather") cmd_weather(event);
    else if (cmd == "pet") cmd_pet(event, db);
    else if (cmd == "gift") cmd_gift(event, db);
    else if (cmd == "aquarium") cmd_aquarium(event, db);
    else if (cmd == "tournament") cmd_tournament(event, db);
    else if (cmd == "event") cmd_event(event, db);
    else if (cmd == "auction") cmd_auction(event, db);
    else if (cmd == "lottery") cmd_lottery(event, db);
    else if (cmd == "enchant") cmd_enchant(event, db);
    else if (cmd == "crew") cmd_crew(event, db);
    else if (cmd == "boss") cmd_boss(event, db);
    else if (cmd == "help") cmd_help(event, db);
    else if (cmd == "stats") cmd_stats(bot, event, db);
    else if (cmd == "terms") cmd_terms(event, db);
    else if (cmd == "privacy") cmd_privacy(event, db);
    else if (cmd == "language") cmd_language(event, db);
}

void handle_button(dpp::cluster& bot, const dpp::button_click_t& event, Database& db) {
    if (!event.command.is_guild_interaction()) {
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message("This bot only works in servers it has been added to.").set_flags(dpp::m_ephemeral));
        return;
    }
    const std::string& cid = event.custom_id;
    if (cid.rfind("reel_", 0) == 0) {
        std::string session_key = cid.substr(5);
        handle_reel_click(bot, event, db, session_key);
    } else if (cid.rfind("boss_", 0) == 0) {
        std::string guild_id = cid.substr(5);
        handle_boss_click(bot, event, db, guild_id);
    }
}

// ===== BEGIN player economy commands (/enchant, /auction, /lottery) =====

static std::string fmt_duration(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    int64_t h = seconds / 3600, m = (seconds % 3600) / 60;
    std::ostringstream ss;
    if (h > 0) ss << h << "h ";
    ss << m << "m";
    return ss.str();
}

// ---------------- /enchant ----------------

static void cmd_enchant(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    UserRow u = db.get_or_create_user(id);

    if (sub == "upgrade") {
        std::string key = get_str(e, "name").value_or("");
        const EnchantDef* def = find_enchant(key);
        if (!def) { reply(e, "Unknown enchant.", true); return; }
        DbResult r = db.upgrade_enchant(id, key);
        if (!r.ok) {
            if (r.error == "not_enough_coins")
                reply(e, "You need " + fmt_coins(r.amount) + " coins for the next level of **" + def->name +
                         "** (you have " + fmt_coins(u.balance) + ").", true);
            else
                reply(e, r.error, true);
            return;
        }
        reply(e, def->emoji + " **" + def->name + "** is now level " + std::to_string(r.id) + "/" +
                 std::to_string(def->max_level) + " (paid " + fmt_coins(r.amount) + ").");
        return;
    }

    Enchants en = db.get_enchants(id);
    auto level_of = [&](const std::string& key) {
        if (key == "lucky") return en.lucky;
        if (key == "swift") return en.swift;
        if (key == "greedy") return en.greedy;
        return en.magnetic;
    };
    std::ostringstream ss;
    for (auto& d : all_enchants()) {
        int lv = level_of(d.key);
        ss << d.emoji << " **" << d.name << "** — level " << lv << "/" << d.max_level << "\n" << d.desc << "\n";
        if (lv >= d.max_level) ss << "MAX LEVEL\n\n";
        else ss << "Next level: " << fmt_coins(enchant_cost(d, lv + 1)) << " coins\n\n";
    }
    ss << "Upgrade with `/enchant upgrade`. Enchants are permanent and stay when you change rods.";
    reply_embed(e, base_embed().set_title("✨ Your Enchants").set_description(ss.str()));
}

// ---------------- /auction ----------------

static std::string auction_line(const AuctionRow& a, bool show_seller) {
    const FishSpecies* f = find_fish(a.fish_id);
    std::ostringstream ss;
    ss << "`#" << a.auction_id << "` " << (f ? f->emoji : std::string("🐟")) << " **"
       << (f ? f->name : std::string("Unknown fish")) << "** " << fmt_weight(a.weight)
       << " — " << fmt_coins(a.price) << " coins";
    if (f) ss << " (market value " << fmt_coins(market_sell_price(*f, a.weight)) << ")";
    if (show_seller) ss << " — seller <@" << a.seller_id << ">";
    ss << "\n";
    return ss.str();
}

static void cmd_auction(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    db.get_or_create_user(id);   // make sure this player exists

    if (sub == "list") {
        int inv_id = (int)get_int(e, "inv_id").value_or(-1);
        int64_t price = get_int(e, "price").value_or(0);
        DbResult r = db.auction_create(id, inv_id, price);
        if (!r.ok) { reply(e, r.error, true); return; }
        const FishSpecies* f = find_fish(r.fish_id);
        int64_t tax = price * AUCTION_TAX_PERCENT / 100;
        reply(e, "🔨 Listed your **" + (f ? f->name : std::string("fish")) + "** (" + fmt_weight(r.weight) + ") for " +
                 fmt_coins(price) + " coins as listing `#" + std::to_string(r.id) + "`.\nYou receive " +
                 fmt_coins(price - tax) + " when it sells (" + std::to_string(AUCTION_TAX_PERCENT) +
                 "% auction tax). Unsold fish return to you after " + std::to_string(AUCTION_DURATION_SECONDS / 3600) + "h.");
        return;
    }

    if (sub == "buy") {
        int aid = (int)get_int(e, "id").value_or(-1);
        DbResult r = db.auction_buy(id, aid);
        if (!r.ok) {
            if (r.error == "not_enough_coins") reply(e, "You need " + fmt_coins(r.amount) + " coins for that listing.", true);
            else reply(e, r.error, true);
            return;
        }
        const FishSpecies* f = find_fish(r.fish_id);
        reply(e, "🛒 You bought a **" + (f ? f->name : std::string("fish")) + "** (" + fmt_weight(r.weight) + ") for " +
                 fmt_coins(r.amount) + " coins! It's in your `/inventory`.");
        return;
    }

    if (sub == "cancel") {
        int aid = (int)get_int(e, "id").value_or(-1);
        DbResult r = db.auction_cancel(id, aid);
        if (!r.ok) { reply(e, r.error, true); return; }
        reply(e, "↩️ Listing `#" + std::to_string(aid) + "` cancelled — the fish is back in your inventory.");
        return;
    }

    if (sub == "mine") {
        auto mine = db.auction_by_seller(id);
        if (mine.empty()) { reply(e, "You have no active listings. Use `/auction list`.", true); return; }
        std::ostringstream ss;
        for (auto& a : mine) ss << auction_line(a, false);
        reply_embed(e, base_embed().set_title("🔨 Your listings").set_description(ss.str()), true);
        return;
    }

    // browse (default)
    const int PAGE = 10;
    int page = std::max(1, (int)get_int(e, "page").value_or(1));
    int total = db.auction_count();
    int pages = std::max(1, (total + PAGE - 1) / PAGE);
    page = std::min(page, pages);
    auto rows = db.auction_list(PAGE, (page - 1) * PAGE);
    if (rows.empty()) { reply(e, "The auction house is empty. Be the first: `/auction list`."); return; }
    std::ostringstream ss;
    for (auto& a : rows) ss << auction_line(a, true);
    ss << "\nPage " << page << "/" << pages << " — " << total << " listing(s). Buy with `/auction buy id:<number>`.";
    reply_embed(e, base_embed().set_title("🔨 Auction House").set_description(ss.str()));
}

// ---------------- /lottery ----------------

static void cmd_lottery(const dpp::slashcommand_t& e, Database& db) {
    std::string sub = get_subcommand(e);
    std::string id = uid(e);
    db.get_or_create_user(id);

    if (sub == "buy") {
        int count = (int)get_int(e, "count").value_or(1);
        DbResult r = db.lottery_buy(id, count);
        if (!r.ok) {
            if (r.error == "not_enough_coins") reply(e, "That costs " + fmt_coins(r.amount) + " coins - you can't afford it.", true);
            else reply(e, r.error, true);
            return;
        }
        reply(e, "🎟️ Bought " + std::to_string(count) + " ticket(s) for " + fmt_coins(r.amount) + " coins. You now hold " +
                 std::to_string(r.id) + " ticket(s) this round. Good luck!");
        return;
    }

    LotteryState st = db.lottery_get(id);
    std::ostringstream ss;
    ss << "💰 **Jackpot:** " << fmt_coins(st.pool * LOTTERY_PAYOUT_PERCENT / 100) << " coins (grows with every ticket)\n"
       << "🎟️ **Tickets sold:** " << st.total_tickets << "\n"
       << "👤 **Your tickets:** " << st.my_tickets << "/" << LOTTERY_MAX_TICKETS;
    if (st.my_tickets > 0 && st.total_tickets > 0)
        ss << " (" << std::fixed << std::setprecision(1) << (100.0 * st.my_tickets / st.total_tickets) << "% to win)";
    ss << "\n⏳ **Next draw in:** " << fmt_duration(st.draw_at - (int64_t)time(nullptr)) << "\n";
    if (!st.last_winner.empty())
        ss << "🏆 **Last winner:** <@" << st.last_winner << "> won " << fmt_coins(st.last_prize) << " coins\n";
    ss << "\nTickets cost " << fmt_coins(LOTTERY_TICKET_PRICE) << " coins each (max " << LOTTERY_MAX_TICKETS
       << " per round). " << (100 - LOTTERY_PAYOUT_PERCENT) << "% of the pot is kept by the house. Buy with `/lottery buy`.";
    reply_embed(e, base_embed().set_title("🎟️ Daily Lottery").set_description(ss.str()));
}

// ===== END player economy commands =====

// ============================= CLI-queued admin actions =============================

// Starts a World Boss in `channel_id` and announces it. False if one is already active
// for this server or no boss species exists.
static bool spawn_boss(dpp::cluster& bot, const std::string& guild_id, const std::string& channel_id) {
    if (BossSessionManager::instance().has_active(guild_id)) return false;
    const FishSpecies* boss = pick_boss_fish();
    if (!boss) return false;
    BossSession sess;
    sess.guild_id = guild_id;
    sess.channel_id = channel_id;
    sess.fish = boss;
    sess.total_hp = boss_hp_for(*boss);
    sess.current_hp = sess.total_hp;
    std::uniform_real_distribution<double> wdist(boss->min_weight, boss->max_weight);
    sess.weight = std::round(wdist(rng()) * 100.0) / 100.0;
    sess.expires_at = (int64_t)time(nullptr) + 10 * 60;
    BossSessionManager::instance().put(guild_id, sess);

    dpp::message m;
    m.channel_id = dpp::snowflake(channel_id);
    m.add_embed(base_embed().set_title("\u26A0\uFE0F A WORLD BOSS APPEARS: " + boss->name + " " + boss->emoji)
        .set_description("Everyone in the server can click **Attack!** to help take it down.\n"
                          "HP: " + render_health_bar(sess.current_hp, sess.total_hp) +
                          "\nTime limit: 10 minutes.")
        .set_color(0xe74c3c));
    dpp::component row;
    row.add_component(dpp::component().set_type(dpp::cot_button).set_label("Attack!")
        .set_style(dpp::cos_danger).set_id("boss_" + guild_id));
    m.add_component(row);
    bot.message_create(m);
    return true;
}

static void announce_to_boss_channels(dpp::cluster& bot, Database& db, const std::string& text) {
    for (auto& gc : db.all_boss_channels()) {
        dpp::message m(text);
        m.channel_id = dpp::snowflake(gc.second);
        bot.message_create(m);
    }
}

// Runs the lottery draw (if due, or forced) and announces the winner.
static void run_lottery_draw(dpp::cluster& bot, Database& db, bool force) {
    LotteryDraw d = db.lottery_draw((int64_t)time(nullptr), force);
    if (!d.drawn) {
        if (force) std::cout << "[admin] lottery_draw: nobody has tickets, nothing to draw\n";
        return;
    }
    std::cout << "[lottery] winner " << d.winner_id << " won " << d.prize << " coins (" << d.total_tickets
              << " tickets, " << d.participants << " players)\n";
    announce_to_boss_channels(bot, db, "🎟️ **Lottery draw!** <@" + d.winner_id + "> won **" + fmt_coins(d.prize) +
        "** coins (" + std::to_string(d.total_tickets) + " tickets from " + std::to_string(d.participants) +
        " player(s)). A new round has started - grab tickets with `/lottery buy`!");
}

static int parse_int_or(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

// Applies one action queued by `fishadmin`. Never throws: a bad row must not take the bot down.
static void handle_admin_action(dpp::cluster& bot, Database& db, const AdminAction& a) {
    auto& world = WorldState::instance();
    try {
        if (a.action == "tournament_start") {
            int minutes = std::max(1, parse_int_or(a.a2, 30));
            if (!find_location(a.a1)) { std::cout << "[admin] tournament_start: unknown location '" << a.a1 << "'\n"; return; }
            db.tournament_reset();
            world.start_tournament(a.a1, minutes);
            std::cout << "[admin] tournament started at " << a.a1 << " for " << minutes << " min\n";
            announce_to_boss_channels(bot, db, "\U0001F3C1 A fishing tournament has started at **" + a.a1 + "** for " +
                std::to_string(minutes) + " minutes! Catches there count toward `/leaderboard type:Current tournament`.");
        } else if (a.action == "tournament_end") {
            world.end_tournament();
            std::cout << "[admin] tournament ended\n";
            announce_to_boss_channels(bot, db, "\U0001F3C1 The tournament has ended.");
        } else if (a.action == "event_start") {
            int hours = std::max(1, parse_int_or(a.a3, 24));
            world.start_event(a.a1, a.a2.empty() ? a.a1 : a.a2, hours);
            std::cout << "[admin] event '" << a.a1 << "' started for " << hours << " h\n";
            announce_to_boss_channels(bot, db, "\U0001F389 **" + (a.a2.empty() ? a.a1 : a.a2) + "** has started for " +
                std::to_string(hours) + " hour(s)! Event fish can now appear on any normal `/fish` cast.");
        } else if (a.action == "event_end") {
            world.end_event();
            std::cout << "[admin] event ended\n";
            announce_to_boss_channels(bot, db, "\U0001F389 The seasonal event has ended.");
        } else if (a.action == "lottery_draw") {
            run_lottery_draw(bot, db, true);
        } else if (a.action == "boss_summon") {
            auto ch = db.get_boss_channel(a.a1);
            if (!ch) { std::cout << "[admin] boss_summon: no boss channel set for server " << a.a1 << "\n"; return; }
            if (spawn_boss(bot, a.a1, *ch)) std::cout << "[admin] World Boss summoned in server " << a.a1 << "\n";
            else std::cout << "[admin] boss_summon: a boss is already active there (or none configured)\n";
        } else {
            std::cout << "[admin] unknown queued action '" << a.action << "'\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "[admin] action '" << a.action << "' failed: " << ex.what() << "\n";
    }
}

// ============================= timers =============================

void start_timers(dpp::cluster& bot, Database& db) {
    WorldState::instance().randomize_weather();

    // Apply admin actions queued by the fishadmin CLI.
    bot.start_timer([&bot, &db](dpp::timer) {
        for (auto& a : db.take_admin_actions()) handle_admin_action(bot, db, a);
    }, 5);

    // Lottery: draw when due (checked every minute). Auctions: return unsold fish after 48h.
    bot.start_timer([&bot, &db](dpp::timer) { run_lottery_draw(bot, db, false); }, 60);
    bot.start_timer([&db](dpp::timer) {
        db.auction_expire_older_than((int64_t)time(nullptr) - AUCTION_DURATION_SECONDS);
    }, 600);

    // Weather changes every 15 minutes.
    bot.start_timer([](dpp::timer) {
        WorldState::instance().randomize_weather();
    }, 15 * 60);

    // Auto-end tournaments once their timer runs out.
    bot.start_timer([&db](dpp::timer) {
        auto& w = WorldState::instance();
        if (w.tournament_active() && (int64_t)time(nullptr) >= w.tournament_ends_at()) {
            w.end_tournament();
        }
    }, 30);

    // Auto-end seasonal events once their timer runs out.
    bot.start_timer([&db](dpp::timer) {
        auto& w = WorldState::instance();
        if (w.event_active() && (int64_t)time(nullptr) >= w.event_ends_at()) {
            w.end_event();
        }
    }, 60);

    // Sweep for World Bosses whose timer expired without being defeated.
    bot.start_timer([&bot, &db](dpp::timer) {
        int64_t now = (int64_t)time(nullptr);
        for (auto& guild_id : BossSessionManager::instance().active_guild_ids()) {
            auto sess = BossSessionManager::instance().get(guild_id);
            if (sess && now > sess->expires_at) {
                resolve_boss_timeout(bot, db, guild_id);
            }
        }
    }, 30);

    // Roughly every 2 hours, give each server with a configured boss channel a 1-in-4
    // chance of a fresh World Boss spawning automatically (if none is already active).
    bot.start_timer([&bot, &db](dpp::timer) {
        std::uniform_int_distribution<int> chance(0, 3);
        for (auto& [guild_id, channel_id] : db.all_boss_channels()) {
            if (BossSessionManager::instance().has_active(guild_id)) continue;
            if (chance(rng()) != 0) continue;
            spawn_boss(bot, guild_id, channel_id);
        }
    }, 2 * 3600);
}
