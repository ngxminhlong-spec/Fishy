#pragma once
// Small helpers shared by the command handlers.
#include <dpp/dpp.h>
#include <optional>
#include <string>
#include "Database.hpp"
#include "I18n.hpp"
#include "Style.hpp"

using Slash = dpp::slashcommand_t;

namespace cmd {

std::string uid(const Slash& e);
std::string uid(const dpp::button_click_t& e);
std::string uid(const dpp::select_click_t& e);

// Name of the subcommand that was used, or "" for commands without any.
std::string sub(const Slash& e);

std::optional<int64_t> opt_int(const Slash& e, const std::string& name);
std::optional<std::string> opt_str(const Slash& e, const std::string& name);
std::optional<dpp::snowflake> opt_user(const Slash& e, const std::string& name);

// The player's /language choice, falling back to their Discord client language.
Lang lang(const Slash& e, Database& db);
Lang lang(const std::string& user_id, const std::string& locale, Database& db);

std::string coins(int64_t n);               // "🪙 1,234"
std::string coins_l(Lang l, int64_t n);     // "1,234 coins" in the player's language
std::string weight(double kg);              // "12.50kg"

// Replies. hidden = only the caller sees it.
void show(const Slash& e, dpp::embed em, bool hidden = false);
void show(const Slash& e, dpp::message m);
void say(const Slash& e, const std::string& text, bool hidden = false);
void refuse(const Slash& e, const std::string& text);
void done(const Slash& e, const std::string& title, const std::string& body = "", bool hidden = false);

int64_t now();

// Adds xp and saves the player. Returns a level-up line, or "" if nothing changed.
std::string grant_xp(Database& db, UserRow& u, int xp);
std::string env_or(const char* name, const std::string& fallback = "");

// ---- paged views (fishdex, market, inventory) ----
// Button ids look like "pg|<kind>|<owner>|<page>", select menus like "sel|<kind>|<owner>".
dpp::component pager_buttons(const std::string& kind, const std::string& owner, int page, int pages);
dpp::component jump_menu(const std::string& kind, const std::string& owner, int page,
                         const std::vector<std::pair<std::string, std::string>>& labels);

}  // namespace cmd
