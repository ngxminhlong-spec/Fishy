#include "Commands.hpp"
#include "CommandUtil.hpp"
#include "FishData.hpp"
#include "Game.hpp"
#include "Gifs.hpp"
#include "Handlers.hpp"
#include <atomic>
#include <functional>
#include <iostream>
#include <map>

namespace {

std::atomic<uint64_t> g_commands_run{0};

using Handler = std::function<void(dpp::cluster&, const Slash&, Database&)>;

const std::map<std::string, Handler>& routes() {
    static const std::map<std::string, Handler> table = {
        {"fish", cmd_fish},           {"shop", cmd_shop},         {"sell", cmd_sell},
        {"trade", cmd_trade},         {"inventory", cmd_inventory}, {"balance", cmd_balance},
        {"daily", cmd_daily},         {"leaderboard", cmd_leaderboard}, {"profile", cmd_profile},
        {"fishdex", cmd_fishdex},     {"achievements", cmd_achievements}, {"quest", cmd_quest},
        {"location", cmd_location},   {"market", cmd_market},     {"weather", cmd_weather},
        {"pet", cmd_pet},             {"gift", cmd_gift},         {"aquarium", cmd_aquarium},
        {"tournament", cmd_tournament}, {"event", cmd_event},     {"auction", cmd_auction},
        {"lottery", cmd_lottery},     {"enchant", cmd_enchant},   {"crew", cmd_crew},
        {"boss", cmd_boss},           {"help", cmd_help},         {"stats", cmd_stats},
        {"terms", cmd_terms},         {"privacy", cmd_privacy},   {"language", cmd_language},
    };
    return table;
}

void localize(dpp::slashcommand& c, const std::string& desc_key) {
    c.add_localization("vi", c.name, tr(Lang::VI, desc_key));
    c.add_localization("zh-CN", c.name, tr(Lang::ZH, desc_key));
}

std::string tier_range(int last) { return "(0-" + std::to_string(last) + ")"; }

// Sends a message to every server's boss channel.
void broadcast(dpp::cluster& bot, Database& db, const dpp::embed& em) {
    for (auto& [guild, channel] : db.all_boss_channels()) {
        dpp::message m;
        m.channel_id = dpp::snowflake(channel);
        m.add_embed(em);
        bot.message_create(m);
    }
}

int to_int(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

void draw_lottery(dpp::cluster& bot, Database& db, bool force) {
    LotteryDraw d = db.lottery_draw(cmd::now(), force);
    if (!d.drawn) {
        if (force) std::cout << "[admin] lottery: nobody has tickets, nothing to draw\n";
        return;
    }
    std::cout << "[lottery] " << d.winner_id << " won " << d.prize << " (" << d.total_tickets
              << " tickets, " << d.participants << " players)\n";
    auto em = style::card(style::GOLD, "🎟️ Lottery draw",
        "<@" + d.winner_id + "> won **" + cmd::coins(d.prize) + "**!\n" +
        std::to_string(d.total_tickets) + " tickets from " + std::to_string(d.participants) +
        " player(s). A new round is open, grab tickets with `/lottery buy`.");
    style::add_gif(em, "lottery");
    broadcast(bot, db, em);
}

// Runs one action queued by fishadmin. A bad row must never take the bot down.
void run_admin_action(dpp::cluster& bot, Database& db, const AdminAction& a) {
    auto& world = WorldState::instance();
    try {
        if (a.action == "tournament_start") {
            int minutes = std::max(1, to_int(a.a2, 30));
            if (!find_location(a.a1)) { std::cout << "[admin] tournament_start: unknown location '" << a.a1 << "'\n"; return; }
            db.tournament_reset();
            world.start_tournament(a.a1, minutes);
            std::cout << "[admin] tournament at " << a.a1 << " for " << minutes << " min\n";
            broadcast(bot, db, style::card(style::GOLD, "🏁 Tournament started",
                "Fish at **" + a.a1 + "** for the next " + std::to_string(minutes) +
                " minutes. Standings: `/leaderboard type:Current tournament`"));
        } else if (a.action == "tournament_end") {
            world.end_tournament();
            std::cout << "[admin] tournament ended\n";
            broadcast(bot, db, style::card(style::GREY, "🏁 Tournament over", "Thanks for fishing."));
        } else if (a.action == "event_start") {
            int hours = std::max(1, to_int(a.a3, 24));
            std::string name = a.a2.empty() ? a.a1 : a.a2;
            world.start_event(a.a1, name, hours);
            std::cout << "[admin] event '" << a.a1 << "' for " << hours << " h\n";
            broadcast(bot, db, style::card(style::PURPLE, "🎉 " + name,
                "Limited-time fish are out for " + std::to_string(hours) + " hour(s). They can bite on any normal `/fish` cast."));
        } else if (a.action == "event_end") {
            world.end_event();
            std::cout << "[admin] event ended\n";
            broadcast(bot, db, style::card(style::GREY, "🎉 Event over", "The seasonal fish have swum off."));
        } else if (a.action == "lottery_draw") {
            draw_lottery(bot, db, true);
        } else if (a.action == "boss_summon") {
            auto channel = db.get_boss_channel(a.a1);
            if (!channel) { std::cout << "[admin] boss_summon: no boss channel for server " << a.a1 << "\n"; return; }
            if (spawn_boss(bot, a.a1, *channel)) std::cout << "[admin] boss summoned in " << a.a1 << "\n";
            else std::cout << "[admin] boss_summon: one is already active there\n";
        } else {
            std::cout << "[admin] unknown action '" << a.action << "'\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "[admin] '" << a.action << "' failed: " << ex.what() << "\n";
    }
}

}  // namespace

void count_command() { g_commands_run++; }
uint64_t commands_run() { return g_commands_run.load(); }

// --- registration ---

void register_commands(dpp::cluster& bot) {
    std::vector<dpp::slashcommand> cmds;
    dpp::snowflake app = bot.me.id;
    using dpp::command_option;
    using dpp::command_option_choice;

    auto plain = [&](const std::string& name, const std::string& desc, const char* i18n_key = nullptr) {
        dpp::slashcommand c(name, desc, app);
        if (i18n_key) localize(c, i18n_key);
        cmds.push_back(c);
    };

    plain("fish", "Cast your line at your current location!", "cmd.fish.desc");

    {
        dpp::slashcommand c("shop", "Browse and buy rods, bait, boats and pets.", app);
        c.add_option(command_option(dpp::co_sub_command, "view", "See everything in stock."));
        c.add_option(command_option(dpp::co_sub_command, "rod", "Buy a fishing rod.")
            .add_option(command_option(dpp::co_integer, "tier", "Rod tier " + tier_range(all_rods().back().tier), true)));
        c.add_option(command_option(dpp::co_sub_command, "bait", "Buy bait. It's used up one per cast.")
            .add_option(command_option(dpp::co_integer, "id", "Bait id " + tier_range(all_bait().back().id), true))
            .add_option(command_option(dpp::co_integer, "amount", "How many (default 1)", false)));
        c.add_option(command_option(dpp::co_sub_command, "boat", "Buy a boat.")
            .add_option(command_option(dpp::co_integer, "tier", "Boat tier " + tier_range(all_boats().back().tier), true)));
        c.add_option(command_option(dpp::co_sub_command, "pet", "Adopt a fishing pet.")
            .add_option(command_option(dpp::co_integer, "id", "Pet id " + tier_range(all_pets().back().id), true)));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("sell", "Sell fish for coins.", app);
        c.add_option(command_option(dpp::co_sub_command, "one", "Sell a single fish.")
            .add_option(command_option(dpp::co_integer, "inv_id", "Inventory id (see /inventory)", true)));
        c.add_option(command_option(dpp::co_sub_command, "all", "Sell everything except aquarium fish."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("trade", "Trade coins and fish with another player.", app);
        c.add_option(command_option(dpp::co_sub_command, "start", "Open a trade with someone.")
            .add_option(command_option(dpp::co_user, "user", "Who to trade with", true)));
        c.add_option(command_option(dpp::co_sub_command, "view", "See both sides of a trade.")
            .add_option(command_option(dpp::co_integer, "trade_id", "Trade id", true)));
        c.add_option(command_option(dpp::co_sub_command, "additem", "Add a fish to your side.")
            .add_option(command_option(dpp::co_integer, "trade_id", "Trade id", true))
            .add_option(command_option(dpp::co_integer, "inv_id", "Inventory id of your fish", true)));
        c.add_option(command_option(dpp::co_sub_command, "addcoins", "Set the coins on your side.")
            .add_option(command_option(dpp::co_integer, "trade_id", "Trade id", true))
            .add_option(command_option(dpp::co_integer, "amount", "Coins to offer", true)));
        c.add_option(command_option(dpp::co_sub_command, "confirm", "Confirm your side.")
            .add_option(command_option(dpp::co_integer, "trade_id", "Trade id", true)));
        c.add_option(command_option(dpp::co_sub_command, "cancel", "Cancel the trade.")
            .add_option(command_option(dpp::co_integer, "trade_id", "Trade id", true)));
        cmds.push_back(c);
    }

    plain("inventory", "Look through your catch.");
    plain("balance", "Check your coins.", "cmd.balance.desc");
    plain("daily", "Claim your daily coins.", "cmd.daily.desc");

    {
        dpp::slashcommand c("leaderboard", "See the top players.", app);
        c.add_option(command_option(dpp::co_string, "type", "Which board", false)
            .add_choice(command_option_choice("Richest", std::string("balance")))
            .add_choice(command_option_choice("Most fish caught", std::string("fish")))
            .add_choice(command_option_choice("Current tournament", std::string("tournament"))));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("profile", "View an angler's profile.", app);
        c.add_option(command_option(dpp::co_user, "user", "Whose profile (default: yours)", false));
        cmds.push_back(c);
    }

    plain("fishdex", "Browse the species you've discovered.");
    plain("achievements", "See your achievements.");

    {
        dpp::slashcommand c("quest", "Your daily quest.", app);
        c.add_option(command_option(dpp::co_sub_command, "view", "Check your progress."));
        c.add_option(command_option(dpp::co_sub_command, "claim", "Collect the reward."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("location", "See or change where you fish.", app);
        c.add_option(command_option(dpp::co_sub_command, "view", "List every spot."));
        command_option where(dpp::co_string, "name", "Where to go", true);
        for (auto& l : all_locations()) where.add_choice(command_option_choice(l.name, l.name));
        c.add_option(command_option(dpp::co_sub_command, "travel", "Head somewhere new.").add_option(where));
        cmds.push_back(c);
    }

    plain("market", "Today's fish prices.");
    plain("weather", "The current fishing weather.");
    plain("pet", "Check on your pet.");

    {
        dpp::slashcommand c("gift", "Send coins to another player.", app);
        c.add_option(command_option(dpp::co_user, "user", "Who gets them", true));
        c.add_option(command_option(dpp::co_integer, "amount", "How many coins", true));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("aquarium", "Keep fish on display instead of selling them.", app);
        c.add_option(command_option(dpp::co_sub_command, "view", "Look at your aquarium."));
        c.add_option(command_option(dpp::co_sub_command, "add", "Put a fish on display.")
            .add_option(command_option(dpp::co_integer, "inv_id", "Inventory id", true)));
        c.add_option(command_option(dpp::co_sub_command, "remove", "Take a fish off display.")
            .add_option(command_option(dpp::co_integer, "inv_id", "Inventory id", true)));
        cmds.push_back(c);
    }

    plain("tournament", "Is a fishing tournament running?");
    plain("event", "Is a seasonal event running?");

    {
        dpp::slashcommand c("crew", "Fish together in a crew.", app);
        c.add_option(command_option(dpp::co_sub_command, "create", "Found a crew.")
            .add_option(command_option(dpp::co_string, "name", "Crew name (unique)", true)));
        c.add_option(command_option(dpp::co_sub_command, "join", "Join a crew.")
            .add_option(command_option(dpp::co_string, "name", "Crew name", true)));
        c.add_option(command_option(dpp::co_sub_command, "leave", "Leave your crew."));
        c.add_option(command_option(dpp::co_sub_command, "info", "Look at a crew.")
            .add_option(command_option(dpp::co_string, "name", "Crew name (default: yours)", false)));
        c.add_option(command_option(dpp::co_sub_command, "deposit", "Put coins in the crew bank.")
            .add_option(command_option(dpp::co_integer, "amount", "Coins", true)));
        c.add_option(command_option(dpp::co_sub_command, "withdraw", "[Owner] Take coins from the bank.")
            .add_option(command_option(dpp::co_integer, "amount", "Coins", true)));
        c.add_option(command_option(dpp::co_sub_command, "kick", "[Owner] Remove a member.")
            .add_option(command_option(dpp::co_user, "user", "Who to remove", true)));
        c.add_option(command_option(dpp::co_sub_command, "disband", "[Owner] Close the crew."));
        c.add_option(command_option(dpp::co_sub_command, "leaderboard", "Richest crews."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("boss", "World Boss fights, open to the whole server.", app);
        c.add_option(command_option(dpp::co_sub_command, "status", "Is a boss out there?"));
        cmds.push_back(c);
    }

    plain("help", "Everything the bot can do.", "cmd.help.desc");
    plain("stats", "Bot, host and database statistics.", "cmd.stats.desc");
    plain("terms", "Read the Terms of Service.", "cmd.terms.desc");
    plain("privacy", "Read the Privacy Policy.", "cmd.privacy.desc");

    {
        dpp::slashcommand c("language", "Pick the language the bot uses with you.", app);
        c.add_option(command_option(dpp::co_string, "language", "Language", false)
            .add_choice(command_option_choice("English", std::string("en")))
            .add_choice(command_option_choice("Tiếng Việt", std::string("vi")))
            .add_choice(command_option_choice("中文（简体）", std::string("zh")))
            .add_choice(command_option_choice("Auto (follow Discord)", std::string("auto"))));
        localize(c, "cmd.language.desc");
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("auction", "Buy and sell fish with other players.", app);
        c.add_option(command_option(dpp::co_sub_command, "browse", "See what's for sale.")
            .add_option(command_option(dpp::co_integer, "page", "Page", false)));
        c.add_option(command_option(dpp::co_sub_command, "list", "Sell one of your fish.")
            .add_option(command_option(dpp::co_integer, "inv_id", "Inventory id", true))
            .add_option(command_option(dpp::co_integer, "price", "Asking price", true)));
        c.add_option(command_option(dpp::co_sub_command, "buy", "Buy a listing.")
            .add_option(command_option(dpp::co_integer, "id", "Listing number", true)));
        c.add_option(command_option(dpp::co_sub_command, "cancel", "Pull one of your listings.")
            .add_option(command_option(dpp::co_integer, "id", "Listing number", true)));
        c.add_option(command_option(dpp::co_sub_command, "mine", "Your active listings."));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("lottery", "Daily lottery, one winner takes the pot.", app);
        c.add_option(command_option(dpp::co_sub_command, "status", "Jackpot and your tickets."));
        c.add_option(command_option(dpp::co_sub_command, "buy", "Buy tickets.")
            .add_option(command_option(dpp::co_integer, "count", "How many", true)));
        cmds.push_back(c);
    }

    {
        dpp::slashcommand c("enchant", "Permanent upgrades bought with coins.", app);
        c.add_option(command_option(dpp::co_sub_command, "list", "Your enchants and what's next."));
        command_option which(dpp::co_string, "name", "Which enchant", true);
        for (auto& en : all_enchants()) which.add_choice(command_option_choice(en.name, en.key));
        c.add_option(command_option(dpp::co_sub_command, "upgrade", "Raise one by a level.").add_option(which));
        cmds.push_back(c);
    }

    // servers only: no user installs, no DMs
    for (auto& c : cmds) {
        c.set_interaction_contexts({dpp::itc_guild});
        c.integration_types = {dpp::ait_guild_install};
    }
    bot.global_bulk_command_create(cmds);
}

// --- dispatch ---

void handle_slashcommand(dpp::cluster& bot, const dpp::slashcommand_t& event, Database& db) {
    if (!event.command.is_guild_interaction()) {
        event.reply(dpp::message("This bot only works in servers it has been added to. Ask a server admin to invite it.")
                        .set_flags(dpp::m_ephemeral));
        return;
    }
    auto route = routes().find(event.command.get_command_name());
    if (route == routes().end()) return;
    count_command();
    route->second(bot, event, db);
}

void handle_button(dpp::cluster& bot, const dpp::button_click_t& event, Database& db) {
    if (!event.command.is_guild_interaction()) {
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message("This bot only works in servers it has been added to.").set_flags(dpp::m_ephemeral));
        return;
    }
    const std::string& id = event.custom_id;

    if (id.rfind("reel_", 0) == 0) { reel_click(bot, event, db, id.substr(5)); return; }
    if (id.rfind("boss_", 0) == 0) { boss_click(bot, event, db, id.substr(5)); return; }

    // pg|kind|owner|page
    if (id.rfind("pg|", 0) == 0) {
        std::vector<std::string> p;
        size_t from = 0, bar;
        while ((bar = id.find('|', from)) != std::string::npos) { p.push_back(id.substr(from, bar - from)); from = bar + 1; }
        p.push_back(id.substr(from));
        if (p.size() != 4) return;
        if (cmd::uid(event) != p[2]) {
            event.reply(dpp::ir_channel_message_with_source,
                        dpp::message("Those pages belong to someone else. Run the command yourself.").set_flags(dpp::m_ephemeral));
            return;
        }
        int page = to_int(p[3], 0);
        dpp::message m = p[1] == "dex" ? page_fishdex(db, p[2], page)
                       : p[1] == "mkt" ? page_market(p[2], page)
                                       : page_inventory(db, p[2], page);
        event.reply(dpp::ir_update_message, m);
    }
}

void handle_select(dpp::cluster&, const dpp::select_click_t& event, Database& db) {
    const std::string& id = event.custom_id;
    if (id.rfind("sel|", 0) != 0 || event.values.empty()) return;
    size_t a = id.find('|', 4);
    if (a == std::string::npos) return;
    std::string kind = id.substr(4, a - 4);
    std::string owner = id.substr(a + 1);
    if (cmd::uid(event) != owner) {
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message("That menu belongs to someone else. Run the command yourself.").set_flags(dpp::m_ephemeral));
        return;
    }
    int page = to_int(event.values[0], 0);
    dpp::message m = kind == "dex" ? page_fishdex(db, owner, page) : page_market(owner, page);
    event.reply(dpp::ir_update_message, m);
}

// --- timers ---

void start_timers(dpp::cluster& bot, Database& db) {
    WorldState::instance().randomize_weather();

    // fishadmin leaves requests in the database, pick them up
    bot.start_timer([&bot, &db](dpp::timer) {
        for (auto& a : db.take_admin_actions()) run_admin_action(bot, db, a);
    }, 5);

    bot.start_timer([&bot, &db](dpp::timer) { draw_lottery(bot, db, false); }, 60);

    bot.start_timer([&db](dpp::timer) {
        db.auction_expire_older_than(cmd::now() - AUCTION_DURATION_SECONDS);
    }, 600);

    bot.start_timer([](dpp::timer) { WorldState::instance().randomize_weather(); }, 15 * 60);

    bot.start_timer([](dpp::timer) {
        auto& w = WorldState::instance();
        if (w.tournament_active() && cmd::now() >= w.tournament_ends_at()) w.end_tournament();
    }, 30);

    bot.start_timer([](dpp::timer) {
        auto& w = WorldState::instance();
        if (w.event_active() && cmd::now() >= w.event_ends_at()) w.end_event();
    }, 60);

    // bosses nobody finished off
    bot.start_timer([&bot, &db](dpp::timer) {
        for (auto& guild : BossSessionManager::instance().active_guild_ids()) {
            auto s = BossSessionManager::instance().get(guild);
            if (s && cmd::now() > s->expires_at) boss_timed_out(bot, db, guild);
        }
    }, 30);

    // reel lines nobody pulled in
    bot.start_timer([](dpp::timer) { ReelSessionManager::instance().sweep(cmd::now()); }, 300);

    // every ~2h each server with a boss channel has a 1 in 4 shot at a spawn
    bot.start_timer([&bot, &db](dpp::timer) {
        std::uniform_int_distribution<int> roll(0, 3);
        for (auto& [guild, channel] : db.all_boss_channels()) {
            if (BossSessionManager::instance().has_active(guild)) continue;
            if (roll(rng()) != 0) continue;
            spawn_boss(bot, guild, channel);
        }
    }, 2 * 3600);

    // new gifs from GIPHY a few times a day (does nothing without GIPHY_API_KEY)
    gifs::refresh(bot);
    bot.start_timer([&bot](dpp::timer) { gifs::refresh(bot); }, 6 * 3600);
}
