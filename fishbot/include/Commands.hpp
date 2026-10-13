#pragma once
#include <dpp/dpp.h>
#include "Database.hpp"

void register_commands(dpp::cluster& bot);
void handle_slashcommand(dpp::cluster& bot, const dpp::slashcommand_t& event, Database& db);
void handle_button(dpp::cluster& bot, const dpp::button_click_t& event, Database& db);
void handle_select(dpp::cluster& bot, const dpp::select_click_t& event, Database& db);
void start_timers(dpp::cluster& bot, Database& db);
