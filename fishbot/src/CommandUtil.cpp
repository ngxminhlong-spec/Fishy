#include "CommandUtil.hpp"
#include "Game.hpp"
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace cmd {

std::string uid(const Slash& e) { return e.command.get_issuing_user().id.str(); }
std::string uid(const dpp::button_click_t& e) { return e.command.get_issuing_user().id.str(); }
std::string uid(const dpp::select_click_t& e) { return e.command.get_issuing_user().id.str(); }

std::string sub(const Slash& e) {
    const auto& opts = e.command.get_command_interaction().options;
    if (!opts.empty() && opts[0].type == dpp::co_sub_command) return opts[0].name;
    return "";
}

static const dpp::command_data_option* dig(const std::vector<dpp::command_data_option>& opts,
                                           const std::string& name) {
    for (auto& o : opts) {
        if (o.name == name) return &o;
        if (auto* hit = dig(o.options, name)) return hit;
    }
    return nullptr;
}

std::optional<int64_t> opt_int(const Slash& e, const std::string& name) {
    auto* o = dig(e.command.get_command_interaction().options, name);
    if (!o) return std::nullopt;
    return std::get<int64_t>(o->value);
}

std::optional<std::string> opt_str(const Slash& e, const std::string& name) {
    auto* o = dig(e.command.get_command_interaction().options, name);
    if (!o) return std::nullopt;
    return std::get<std::string>(o->value);
}

std::optional<dpp::snowflake> opt_user(const Slash& e, const std::string& name) {
    auto* o = dig(e.command.get_command_interaction().options, name);
    if (!o) return std::nullopt;
    return std::get<dpp::snowflake>(o->value);
}

Lang lang(const std::string& user_id, const std::string& locale, Database& db) {
    if (auto saved = parse_lang(db.get_lang(user_id))) return *saved;
    return lang_from_discord_locale(locale);
}

Lang lang(const Slash& e, Database& db) {
    return lang(uid(e), e.command.locale, db);
}

std::string coins(int64_t n) { return "🪙 " + style::thousands(n); }

std::string coins_l(Lang l, int64_t n) { return style::thousands(n) + " " + tr(l, "unit.coins"); }

std::string weight(double kg) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << kg << "kg";
    return ss.str();
}

void show(const Slash& e, dpp::embed em, bool hidden) {
    dpp::message m;
    m.add_embed(em);
    if (hidden) m.set_flags(dpp::m_ephemeral);
    e.reply(m);
}

void show(const Slash& e, dpp::message m) { e.reply(m); }

void say(const Slash& e, const std::string& text, bool hidden) {
    dpp::message m(text);
    if (hidden) m.set_flags(dpp::m_ephemeral);
    e.reply(m);
}

void refuse(const Slash& e, const std::string& text) {
    // texts that already open with an emoji don't get a second one
    bool has_icon = !text.empty() && (unsigned char)text[0] >= 0x80;
    show(e, style::card(style::RED, "", has_icon ? text : "⚠️ " + text), true);
}

void done(const Slash& e, const std::string& title, const std::string& body, bool hidden) {
    show(e, style::card(style::GREEN, title, body), hidden);
}

int64_t now() { return (int64_t)std::time(nullptr); }

std::string grant_xp(Database& db, UserRow& u, int xp) {
    int before = u.level;
    u.xp += xp;
    u.level = level_for_xp(u.xp);
    db.save_user(u);
    if (u.level > before) return "🎉 Level up! You're now **level " + std::to_string(u.level) + "**.";
    return "";
}

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

dpp::component pager_buttons(const std::string& kind, const std::string& owner, int page, int pages) {
    auto id = [&](int p) { return "pg|" + kind + "|" + owner + "|" + std::to_string(p); };
    dpp::component row;
    row.add_component(dpp::component().set_type(dpp::cot_button).set_label("◀")
        .set_style(dpp::cos_secondary).set_id(id(page - 1)).set_disabled(page <= 0));
    row.add_component(dpp::component().set_type(dpp::cot_button)
        .set_label(std::to_string(page + 1) + " / " + std::to_string(pages))
        .set_style(dpp::cos_secondary).set_id("noop|" + kind + "|" + owner).set_disabled(true));
    row.add_component(dpp::component().set_type(dpp::cot_button).set_label("▶")
        .set_style(dpp::cos_secondary).set_id(id(page + 1)).set_disabled(page >= pages - 1));
    return row;
}

dpp::component jump_menu(const std::string& kind, const std::string& owner, int page,
                         const std::vector<std::pair<std::string, std::string>>& labels) {
    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu).set_id("sel|" + kind + "|" + owner).set_placeholder("Jump to...");
    for (size_t i = 0; i < labels.size(); i++) {
        dpp::select_option opt(labels[i].first, std::to_string(i), labels[i].second);
        opt.set_default(i == (size_t)page);
        menu.add_select_option(opt);
    }
    dpp::component row;
    row.add_component(menu);
    return row;
}

}  // namespace cmd
