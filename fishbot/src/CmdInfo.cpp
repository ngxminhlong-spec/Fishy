#include "Handlers.hpp"
#include "CommandUtil.hpp"
#include "SysStats.hpp"
#include <cstdio>

namespace {

std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string line(const std::string& label, const std::string& value) {
    return "**" + label + ":** " + value + "\n";
}

// bits / bytes / KB / MB / GB stacked, one per line
std::string size_lines(Lang L, uint64_t bytes) {
    SizeBreakdown b = size_breakdown(bytes);
    return "• **" + fmt_number(b.bits) + "** " + tr(L, "stats.bits") + "\n" +
           "• **" + fmt_number(b.bytes) + "** " + tr(L, "stats.bytes") + "\n" +
           "• **" + fixed(b.kb, 2) + "** KB\n" +
           "• **" + fixed(b.mb, 4) + "** MB\n" +
           "• **" + fixed(b.gb, 6) + "** GB";
}

const char* LEGAL_UPDATED = "2026-10-05";

void send_legal(const Slash& e, Database& db, const char* title_key, const char* body_key, const char* url_env) {
    Lang L = cmd::lang(e, db);
    std::string url = cmd::env_or(url_env);
    std::string full = url.empty() ? "" : trf(L, "legal.full", {{"url", url}});
    std::string contact = cmd::env_or("BOT_CONTACT", tr(L, "legal.contact.default"));
    std::string body = trf(L, body_key, {{"date", LEGAL_UPDATED}, {"contact", contact}, {"full", full}});
    cmd::show(e, style::card(style::BLUE, tr(L, title_key), body), true);
}

}  // namespace

void cmd_help(dpp::cluster&, const Slash& e, Database& db) {
    Lang L = cmd::lang(e, db);
    auto em = style::card(style::BLUE, tr(L, "help.title"), tr(L, "help.body"));
    cmd::show(e, em, true);
}

void cmd_stats(dpp::cluster& bot, const Slash& e, Database& db) {
    Lang L = cmd::lang(e, db);
    const std::string na = tr(L, "stats.na");

    ProcStats p = read_proc_stats(250);
    HostStats h = read_host_stats();
    auto counts = db.table_row_counts();
    auto info = db.db_info();

    auto rows_in = [&](const std::string& table) -> int64_t {
        for (auto& c : counts) if (c.first == table) return c.second;
        return 0;
    };

    std::string bot_txt;
    bot_txt += line(tr(L, "stats.uptime"), fmt_uptime(bot_uptime_seconds()));
    bot_txt += line(tr(L, "stats.servers"), fmt_number(dpp::get_guild_cache()->count()));
    bot_txt += line(tr(L, "stats.players"), fmt_number((uint64_t)rows_in("users")));
    bot_txt += line(tr(L, "stats.fish_total"), fmt_number((uint64_t)rows_in("inventory")));
    bot_txt += line(tr(L, "stats.cmds"), fmt_number(commands_run()));
    bot_txt += line(tr(L, "stats.ping"), std::to_string((long)std::lround(bot.rest_ping * 1000.0)) + " ms");
    bot_txt += line(tr(L, "stats.shards"), std::to_string(bot.numshards));
#ifdef DPP_VERSION_TEXT
    bot_txt += line(tr(L, "stats.lib"), DPP_VERSION_TEXT);
#endif

    std::string proc_txt = na;
    std::string mem_txt = na;
    if (p.supported) {
        proc_txt  = line(tr(L, "stats.pid"), std::to_string(p.pid));
        proc_txt += line(tr(L, "stats.threads"), std::to_string(p.threads));
        proc_txt += line(tr(L, "stats.cpu"),
            trf(L, "stats.cpu.fmt", {{"core", fixed(p.cpu_percent_core, 1)},
                                     {"total", fixed(p.cpu_percent_total, 1)},
                                     {"cores", std::to_string(h.cores > 0 ? h.cores : 1)}}));
        proc_txt += line(tr(L, "stats.cputime"), fmt_uptime(p.cpu_time_ms / 1000));

        std::string ram = fmt_bytes(p.rss_bytes);
        if (h.mem_total > 0) ram += " (" + fixed(100.0 * (double)p.rss_bytes / (double)h.mem_total, 2) + "%)";
        mem_txt  = line(tr(L, "stats.ram"), ram);
        mem_txt += line(tr(L, "stats.vv"), fmt_bytes(p.vsize_bytes));
        mem_txt += line(tr(L, "stats.vvpeak"), fmt_bytes(p.vpeak_bytes));
        mem_txt += line(tr(L, "stats.swap"), fmt_bytes(p.swap_bytes));
    }

    std::string host_txt = na;
    if (h.supported) {
        host_txt.clear();
        if (!h.os.empty()) host_txt += line(tr(L, "stats.os"), h.os);
        if (!h.cpu_model.empty()) host_txt += line(tr(L, "stats.cpumodel"), h.cpu_model);
        host_txt += line(tr(L, "stats.cores"), std::to_string(h.cores));
        host_txt += line(tr(L, "stats.load"), fixed(h.load1, 2) + " / " + fixed(h.load5, 2) + " / " + fixed(h.load15, 2));
        uint64_t used = h.mem_total > h.mem_available ? h.mem_total - h.mem_available : 0;
        host_txt += line(tr(L, "stats.hostram"), fmt_bytes(used) + " / " + fmt_bytes(h.mem_total));
        if (h.swap_total > 0) {
            uint64_t swapped = h.swap_total > h.swap_free ? h.swap_total - h.swap_free : 0;
            host_txt += line(tr(L, "stats.hostswap"), fmt_bytes(swapped) + " / " + fmt_bytes(h.swap_total));
        }
        host_txt += line(tr(L, "stats.hostup"), fmt_uptime(h.uptime_seconds));
    }

    // WAL mode keeps fresh writes in the -wal file, so the real footprint is main + wal + shm
    uint64_t db_main = file_size_or_zero(db.path());
    uint64_t db_side = file_size_or_zero(db.path() + "-wal") + file_size_or_zero(db.path() + "-shm");
    std::string db_txt = line(tr(L, "stats.dbmain"), fmt_bytes(db_main)) +
                         line(tr(L, "stats.dbwal"), fmt_bytes(db_side)) +
                         "**" + tr(L, "stats.dbtotal") + ":**\n" + size_lines(L, db_main + db_side);

    std::string dbinfo_txt = line(tr(L, "stats.sqlite"), info.sqlite_version) +
        line(tr(L, "stats.pages"), fmt_number((uint64_t)info.page_size) + " B × " + fmt_number((uint64_t)info.page_count)) +
        line(tr(L, "stats.freepages"), fmt_number((uint64_t)info.freelist_count)) +
        line(tr(L, "stats.sqlmem"), fmt_bytes((uint64_t)info.sqlite_mem_used));

    std::string rows_txt;
    for (auto& c : counts) rows_txt += "`" + c.first + "` " + fmt_number((uint64_t)c.second) + "\n";

    auto em = style::card(style::BLUE, tr(L, "stats.title"));
    em.add_field(tr(L, "stats.sec.bot"), bot_txt, true)
      .add_field(tr(L, "stats.sec.proc"), proc_txt, true)
      .add_field(tr(L, "stats.sec.mem"), mem_txt, true)
      .add_field(tr(L, "stats.sec.host"), host_txt, false)
      .add_field(tr(L, "stats.sec.db"), db_txt, true)
      .add_field(tr(L, "stats.sec.dbinfo"), dbinfo_txt, true)
      .add_field(tr(L, "stats.sec.rows"), rows_txt, true)
      .set_footer(dpp::embed_footer().set_text(tr(L, "stats.footer")))
      .set_timestamp(time(nullptr));
    cmd::show(e, em, true);
}

void cmd_terms(dpp::cluster&, const Slash& e, Database& db) {
    send_legal(e, db, "terms.title", "terms.body", "TOS_URL");
}

void cmd_privacy(dpp::cluster&, const Slash& e, Database& db) {
    send_legal(e, db, "privacy.title", "privacy.body", "PRIVACY_URL");
}

void cmd_language(dpp::cluster&, const Slash& e, Database& db) {
    std::string id = cmd::uid(e);
    Lang from_discord = lang_from_discord_locale(e.command.locale);
    auto choice = cmd::opt_str(e, "language");

    if (!choice) {
        auto saved = parse_lang(db.get_lang(id));
        Lang current = saved.value_or(from_discord);
        cmd::show(e, style::card(style::BLUE, tr(current, "lang.title"),
            trf(current, "lang.show", {{"lang", lang_native_name(current)},
                                       {"mode", tr(current, saved ? "lang.mode.manual" : "lang.mode.auto")}})), true);
        return;
    }
    if (*choice == "auto") {
        db.set_lang(id, "");
        cmd::show(e, style::card(style::GREEN, "", trf(from_discord, "lang.auto_set", {{"lang", lang_native_name(from_discord)}})), true);
        return;
    }
    auto picked = parse_lang(*choice);
    if (!picked) { cmd::refuse(e, "en / vi / zh"); return; }
    db.set_lang(id, lang_code(*picked));
    cmd::show(e, style::card(style::GREEN, "", trf(*picked, "lang.set", {{"lang", lang_native_name(*picked)}})), true);
}
