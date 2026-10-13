#pragma once
// One function per slash command, grouped by the .cpp file that implements them.
#include <dpp/dpp.h>
#include <string>
#include "Database.hpp"

using Slash = dpp::slashcommand_t;

// CmdFishing.cpp
void cmd_fish(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_shop(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_sell(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_inventory(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_aquarium(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_fishdex(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_market(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_location(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_weather(dpp::cluster& bot, const Slash& e, Database& db);
void reel_click(dpp::cluster& bot, const dpp::button_click_t& e, Database& db, const std::string& session_key);

// Paged views, shared by the slash command and the page buttons.
dpp::message page_fishdex(Database& db, const std::string& user, int page);
dpp::message page_market(const std::string& user, int page);
dpp::message page_inventory(Database& db, const std::string& user, int page);

// CmdEconomy.cpp
void cmd_balance(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_daily(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_gift(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_trade(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_auction(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_lottery(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_enchant(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_leaderboard(dpp::cluster& bot, const Slash& e, Database& db);

// CmdSocial.cpp
void cmd_profile(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_achievements(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_quest(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_pet(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_crew(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_boss(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_tournament(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_event(dpp::cluster& bot, const Slash& e, Database& db);

void boss_click(dpp::cluster& bot, const dpp::button_click_t& e, Database& db, const std::string& guild_id);
bool spawn_boss(dpp::cluster& bot, const std::string& guild_id, const std::string& channel_id);
void boss_timed_out(dpp::cluster& bot, Database& db, const std::string& guild_id);

// CmdInfo.cpp
void cmd_help(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_stats(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_terms(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_privacy(dpp::cluster& bot, const Slash& e, Database& db);
void cmd_language(dpp::cluster& bot, const Slash& e, Database& db);

// Commands.cpp
void count_command();
uint64_t commands_run();
