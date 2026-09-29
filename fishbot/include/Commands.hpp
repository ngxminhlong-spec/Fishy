#pragma once
#include <dpp/dpp.h>
#include "Database.hpp"

// Registers every slash command (call once on ready).
void register_commands(dpp::cluster& bot);

// Dispatches an incoming slash command to the right handler.
void handle_slashcommand(dpp::cluster& bot, const dpp::slashcommand_t& event, Database& db);

// Dispatches button clicks (reel minigame, trade confirm, shop buy buttons, etc).
void handle_button(dpp::cluster& bot, const dpp::button_click_t& event, Database& db);

// Starts recurring background timers: weather rotation, quest expiry, tournament end.
void start_timers(dpp::cluster& bot, Database& db);
