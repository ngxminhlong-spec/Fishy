#include <dpp/dpp.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <set>
#include <string>
#include "Database.hpp"
#include "Commands.hpp"
#include "Game.hpp"
#include "Gifs.hpp"

// ALLOWED_GUILD_IDS is a comma-separated list of server ids. Empty or unset means any server is fine.
static std::set<uint64_t> parse_allowed_guilds() {
    std::set<uint64_t> out;
    const char* raw = std::getenv("ALLOWED_GUILD_IDS");
    if (!raw || std::string(raw).empty()) return out;

    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        token = token.substr(start, end - start + 1);
        if (token.empty()) continue;
        try {
            out.insert(std::stoull(token));
        } catch (...) {
            std::cerr << "Warning: '" << token << "' in ALLOWED_GUILD_IDS isn't a valid guild ID, skipping it.\n";
        }
    }
    return out;
}

int main() {
    // under systemd stdout is block-buffered, so flush every write or journalctl stays empty
    std::cout << std::unitbuf;

    const char* token = std::getenv("BOT_TOKEN");
    if (!token || std::string(token).empty()) {
        std::cerr << "Set the BOT_TOKEN environment variable to your Discord bot token.\n";
        return 1;
    }

    std::set<uint64_t> allowed_guilds = parse_allowed_guilds();
    if (allowed_guilds.empty()) {
        std::cout << "No ALLOWED_GUILD_IDS set - the bot will stay in any server it's invited to.\n";
    } else {
        std::cout << "Server allowlist active (" << allowed_guilds.size()
                   << " guild id(s)) - the bot will auto-leave any other server.\n";
    }

    Database db("fishbot.sqlite3");
    gifs::load_file(std::getenv("GIF_FILE") ? std::getenv("GIF_FILE") : "gifs.txt");

    dpp::cluster bot(token, dpp::i_default_intents);

    bot.on_log(dpp::utility::cout_logger());

    // runs for every server at startup and again for each new invite
    bot.on_guild_create([&bot, &db, allowed_guilds](const dpp::guild_create_t& event) {
        if (allowed_guilds.empty()) return;
        uint64_t guild_id = event.created.id;
        if (allowed_guilds.find(guild_id) == allowed_guilds.end()) {
            std::cout << "Guild '" << event.created.name << "' (" << guild_id
                       << ") is not on the allowlist - leaving it.\n";
            bot.current_user_leave_guild(event.created.id);
            // the boss channel and a live fight are the only per-server state
            std::string guild_id_str = event.created.id.str();
            db.clear_boss_channel(guild_id_str);
            BossSessionManager::instance().erase(guild_id_str);
        }
    });

    bot.on_slashcommand([&bot, &db](const dpp::slashcommand_t& event) {
        try {
            handle_slashcommand(bot, event, db);
        } catch (const std::exception& ex) {
            dpp::message m(std::string("⚠️ Something went wrong: ") + ex.what());
            m.set_flags(dpp::m_ephemeral);
            event.reply(m);
        }
    });

    bot.on_button_click([&bot, &db](const dpp::button_click_t& event) {
        try {
            handle_button(bot, event, db);
        } catch (const std::exception& ex) {
            dpp::message m(std::string("⚠️ Something went wrong: ") + ex.what());
            m.set_flags(dpp::m_ephemeral);
            event.reply(m);
        }
    });

    bot.on_select_click([&bot, &db](const dpp::select_click_t& event) {
        try {
            handle_select(bot, event, db);
        } catch (const std::exception& ex) {
            event.reply(dpp::message(std::string("⚠️ Something went wrong: ") + ex.what()).set_flags(dpp::m_ephemeral));
        }
    });

    bot.on_ready([&bot, &db](const dpp::ready_t&) {
        if (dpp::run_once<struct register_bot_commands>()) {
            register_commands(bot);
            start_timers(bot, db);
            std::cout << "Fishing bot ready as " << bot.me.username << "\n";
        }
    });

    bot.start(dpp::st_wait);
    return 0;
}
