#include "Handlers.hpp"
#include "CommandUtil.hpp"
#include "FishData.hpp"
#include "Game.hpp"
#include "Gifs.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

using cmd::coins;

namespace {

const int BOSS_WINDOW_SECONDS = 10 * 60;
const int BOSS_COOLDOWN_SECONDS = 2;

std::string player_name(const dpp::user& u) {
    return u.global_name.empty() ? u.username : u.global_name;
}

}  // namespace

// --- /profile ---

void cmd_profile(dpp::cluster&, const Slash& e, Database& db) {
    auto target = cmd::opt_user(e, "user");
    std::string id = target ? target->str() : cmd::uid(e);
    UserRow u = db.get_or_create_user(id);

    // the target's name and avatar come with the interaction when they were picked
    const dpp::user* who = &e.command.get_issuing_user();
    if (target) {
        auto hit = e.command.resolved.users.find(*target);
        if (hit != e.command.resolved.users.end()) who = &hit->second;
    }

    const Rod* rod = find_rod(u.rod_tier);
    const Boat* boat = u.boat_tier >= 0 ? find_boat(u.boat_tier) : nullptr;
    const Bait* bait = (u.bait_id >= 0 && u.bait_count > 0) ? find_bait(u.bait_id) : nullptr;
    const PetSpecies* pet = u.pet_id >= 0 ? find_pet(u.pet_id) : nullptr;
    const Location* spot = find_location(u.location);

    int64_t floor_xp = xp_needed_for_level(u.level);
    int64_t next_xp = xp_needed_for_level(u.level + 1);
    double into_level = next_xp > floor_xp ? (double)(u.xp - floor_xp) / (next_xp - floor_xp) : 1.0;

    std::string crew_name = "None";
    if (u.crew_id >= 0)
        if (auto crew = db.get_crew(u.crew_id)) crew_name = crew->name;

    auto em = style::card(style::BLUE, "🎣 " + player_name(*who),
        "**Level " + std::to_string(u.level) + "**  " + style::meter(into_level, 10) + "  " +
        style::thousands(u.xp) + " / " + style::thousands(next_xp) + " xp");
    em.set_thumbnail(who->get_avatar_url(256));
    em.add_field("Balance", coins(u.balance), true)
      .add_field("Spot", spot ? spot->emoji + " " + spot->name : u.location, true)
      .add_field("Crew", crew_name, true)
      .add_field("Rod", rod ? rod->name + " (power " + std::to_string(rod->power) + ")" : "None", true)
      .add_field("Boat", boat ? boat->name : "None", true)
      .add_field("Bait", bait ? bait->name + " x" + std::to_string(u.bait_count) : "None", true)
      .add_field("Pet", pet ? pet->emoji + " " + pet->name + " (Lv." + std::to_string(u.pet_level) + ")" : "None", true)
      .add_field("Fish caught", style::thousands(db.count_fish(id)), true)
      .add_field("Heaviest", cmd::weight(db.heaviest_catch(id)), true)
      .add_field("Fishdex", std::to_string(db.get_discovered(id).size()) + " / " + std::to_string(all_fish().size()), true)
      .add_field("Achievements", std::to_string(db.get_achievements(id).size()), true);
    cmd::show(e, em);
}

// --- /achievements ---

void cmd_achievements(dpp::cluster&, const Slash& e, Database& db) {
    auto have = db.get_achievements(cmd::uid(e));
    std::ostringstream body;
    int got = 0;
    for (auto& a : achievement_pool()) {
        bool yes = std::find(have.begin(), have.end(), a.key) != have.end();
        got += yes ? 1 : 0;
        body << (yes ? "🏆 **" : "🔒 ") << a.name << (yes ? "**" : "") << "\n-# " << a.description << "\n";
    }
    auto em = style::card(style::GOLD, "🏆 Achievements",
        style::meter((double)got / achievement_pool().size(), 12) + "  " + std::to_string(got) + " / " +
        std::to_string(achievement_pool().size()) + "\n\n" + body.str());
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em, true);
}

// --- /quest ---

void cmd_quest(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    auto q = db.get_active_quest(id);
    int64_t t = cmd::now();

    if (!q || t - q->assigned_at > 24 * 3600) {
        const QuestDef& pick = random_quest();
        Database::QuestRow fresh;
        fresh.user_id = id;
        fresh.quest_key = pick.key;
        fresh.target = pick.target;
        fresh.assigned_at = t;
        db.set_quest(fresh);
        q = fresh;
    }
    const QuestDef* def = find_quest(q->quest_key);

    if (what == "claim") {
        if (q->claimed) { cmd::refuse(e, "You already took today's reward."); return; }
        if (q->progress < q->target) {
            cmd::refuse(e, "Not done yet: " + std::to_string(q->progress) + " / " + std::to_string(q->target) + ".");
            return;
        }
        UserRow u = db.get_or_create_user(id);
        int reward = def ? def->reward_coins : 0;
        u.balance += reward;
        db.save_user(u);
        q->claimed = true;
        db.set_quest(*q);
        auto em = style::card(style::GREEN, "🎁 Quest complete", "You earned " + coins(reward) + ".");
        style::sign(em, e.command.get_issuing_user());
        style::add_gif(em, "money");
        cmd::show(e, em);
        return;
    }

    double frac = q->target > 0 ? (double)q->progress / q->target : 0;
    std::string footer_line = q->claimed ? "✅ Reward claimed." :
                              (q->progress >= q->target ? "Done! Collect with `/quest claim`." : "");
    auto em = style::card(q->claimed ? style::GREY : style::BLUE, "📜 Daily quest",
        (def ? def->description : q->quest_key) + "\n\n" + style::meter(frac, 12) + "  **" + std::to_string(q->progress) +
        " / " + std::to_string(q->target) + "**" + (footer_line.empty() ? "" : "\n" + footer_line));
    if (def) em.add_field("Reward", coins(def->reward_coins), true);
    em.add_field("Resets", style::when(q->assigned_at + 24 * 3600), true);
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em, true);
}

// --- /pet ---

void cmd_pet(dpp::cluster&, const Slash& e, Database& db) {
    UserRow u = db.get_or_create_user(cmd::uid(e));
    const PetSpecies* p = u.pet_id >= 0 ? find_pet(u.pet_id) : nullptr;
    if (!p) { cmd::refuse(e, "You haven't adopted a pet yet. See `/shop view`."); return; }
    double bonus = p->catch_bonus * (1.0 + 0.05 * (u.pet_level - 1));
    auto em = style::card(style::PURPLE, p->emoji + " " + p->name);
    em.add_field("Level", std::to_string(u.pet_level), true)
      .add_field("XP", std::to_string(u.pet_xp), true)
      .add_field("Bite bonus", "+" + std::to_string((int)std::round(bonus * 100)) + "%", true);
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em);
}

// --- /crew ---

void cmd_crew(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    UserRow u = db.get_or_create_user(id);

    auto must_own = [&](std::optional<CrewRow>& crew) {
        if (u.crew_id < 0) { cmd::refuse(e, "You're not in a crew."); return false; }
        crew = db.get_crew(u.crew_id);
        if (!crew || crew->owner_id != id) { cmd::refuse(e, "Only the crew owner can do that."); return false; }
        return true;
    };

    if (what == "create") {
        if (u.crew_id >= 0) { cmd::refuse(e, "Leave your current crew first with `/crew leave`."); return; }
        std::string name = cmd::opt_str(e, "name").value_or("");
        if (name.empty() || name.size() > 32) { cmd::refuse(e, "Crew names are 1 to 32 characters."); return; }
        auto crew_id = db.create_crew(name, id);
        if (!crew_id) { cmd::refuse(e, "Someone already took that name."); return; }
        db.join_crew(id, *crew_id);
        db.unlock_achievement(id, "crew_founder");
        cmd::done(e, "⚓ Crew founded: " + name, "Invite people to `/crew join name:" + name + "`.");
        return;
    }

    if (what == "join") {
        if (u.crew_id >= 0) { cmd::refuse(e, "Leave your current crew first with `/crew leave`."); return; }
        auto crew = db.get_crew_by_name(cmd::opt_str(e, "name").value_or(""));
        if (!crew) { cmd::refuse(e, "No crew goes by that name."); return; }
        db.join_crew(id, crew->crew_id);
        cmd::done(e, "⚓ Welcome aboard", "You joined **" + crew->name + "**.");
        return;
    }

    if (what == "leave") {
        if (u.crew_id < 0) { cmd::refuse(e, "You're not in a crew."); return; }
        auto crew = db.get_crew(u.crew_id);
        bool owner = crew && crew->owner_id == id;
        if (owner && db.count_crew_members(u.crew_id) > 1) {
            cmd::refuse(e, "You own this crew and others are still in it. `/crew kick` them or `/crew disband`.");
            return;
        }
        if (owner) { db.delete_crew(u.crew_id); cmd::done(e, "Crew closed", "You were the last member, so it's gone."); return; }
        db.leave_crew(id);
        cmd::done(e, "You left the crew");
        return;
    }

    if (what == "info") {
        std::string name = cmd::opt_str(e, "name").value_or("");
        std::optional<CrewRow> crew;
        if (!name.empty()) crew = db.get_crew_by_name(name);
        else if (u.crew_id >= 0) crew = db.get_crew(u.crew_id);
        if (!crew) { cmd::refuse(e, "No such crew. Name one, or join one first."); return; }
        auto members = db.get_crew_member_ids(crew->crew_id);
        std::ostringstream roster;
        for (auto& m : members) roster << "<@" << m << ">" << (m == crew->owner_id ? " 👑" : "") << "\n";
        auto em = style::card(style::BLUE, "⚓ " + crew->name);
        em.add_field("Owner", "<@" + crew->owner_id + ">", true)
          .add_field("Bank", coins(crew->bank), true)
          .add_field("Founded", style::when(crew->created_at), true)
          .add_field("Crew (" + std::to_string(members.size()) + ")", roster.str().empty() ? "Empty" : roster.str(), false);
        cmd::show(e, em);
        return;
    }

    if (what == "deposit") {
        if (u.crew_id < 0) { cmd::refuse(e, "You're not in a crew."); return; }
        int64_t amount = cmd::opt_int(e, "amount").value_or(0);
        if (amount <= 0 || amount > u.balance) { cmd::refuse(e, "You can't deposit that. You hold " + coins(u.balance) + "."); return; }
        u.balance -= amount;
        db.save_user(u);
        db.adjust_crew_bank(u.crew_id, amount);
        cmd::done(e, "🏦 Deposited", coins(amount) + " went into the crew bank.");
        return;
    }

    if (what == "withdraw") {
        std::optional<CrewRow> crew;
        if (!must_own(crew)) return;
        int64_t amount = cmd::opt_int(e, "amount").value_or(0);
        if (amount <= 0 || amount > crew->bank) { cmd::refuse(e, "The bank only holds " + coins(crew->bank) + "."); return; }
        db.adjust_crew_bank(u.crew_id, -amount);
        u.balance += amount;
        db.save_user(u);
        cmd::done(e, "🏦 Withdrawn", coins(amount) + " came out of the crew bank.");
        return;
    }

    if (what == "kick") {
        std::optional<CrewRow> crew;
        if (!must_own(crew)) return;
        auto target = cmd::opt_user(e, "user");
        if (!target) { cmd::refuse(e, "Pick who to remove."); return; }
        std::string tid = target->str();
        if (tid == id) { cmd::refuse(e, "You can't kick yourself. Use `/crew disband`."); return; }
        UserRow other = db.get_or_create_user(tid);
        if (other.crew_id != u.crew_id) { cmd::refuse(e, "They aren't in your crew."); return; }
        db.leave_crew(tid);
        cmd::done(e, "Removed from the crew", "<@" + tid + "> is out.");
        return;
    }

    if (what == "disband") {
        std::optional<CrewRow> crew;
        if (!must_own(crew)) return;
        db.delete_crew(u.crew_id);
        cmd::done(e, "Crew disbanded", "**" + crew->name + "** is no more.");
        return;
    }

    if (what == "leaderboard") {
        std::ostringstream body;
        int place = 1;
        for (auto& c : db.top_crews_by_bank(10)) {
            body << (place <= 3 ? std::string(place == 1 ? "🥇" : place == 2 ? "🥈" : "🥉") : "`" + std::to_string(place) + ".`")
                 << " **" << c.name << "** · " << coins(c.bank) << "\n";
            place++;
        }
        std::string text = body.str();
        cmd::show(e, style::card(style::GOLD, "⚓ Richest crews", text.empty() ? "No crews yet." : text));
        return;
    }
}

// --- tournament, event ---

void cmd_tournament(dpp::cluster&, const Slash& e, Database&) {
    auto& world = WorldState::instance();
    if (!world.tournament_active()) {
        cmd::show(e, style::card(style::GREY, "🏁 Tournament", "Nothing is running right now."));
        return;
    }
    const Location* spot = find_location(world.tournament_location());
    auto em = style::card(style::GOLD, "🏁 Tournament on",
        "Catch fish at **" + (spot ? spot->emoji + " " + spot->name : world.tournament_location()) +
        "**. Total weight counts.\nEnds " + style::when(world.tournament_ends_at()) +
        ".\n\nStandings: `/leaderboard type:Current tournament`");
    cmd::show(e, em);
}

void cmd_event(dpp::cluster&, const Slash& e, Database&) {
    auto& world = WorldState::instance();
    if (!world.event_active()) {
        cmd::show(e, style::card(style::GREY, "🎉 Seasonal event", "Nothing is running right now."));
        return;
    }
    cmd::show(e, style::card(style::PURPLE, "🎉 " + world.event_name(),
        "Event fish can show up on any normal cast.\nEnds " + style::when(world.event_ends_at()) + "."));
}

// --- World Boss ---

namespace {

dpp::embed boss_card(const BossSession& s, const std::string& headline) {
    std::vector<std::pair<std::string, double>> ranked(s.contributions.begin(), s.contributions.end());
    std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::ostringstream top;
    for (size_t i = 0; i < ranked.size() && i < 3; i++)
        top << (i == 0 ? "🥇" : i == 1 ? "🥈" : "🥉") << " <@" << ranked[i].first << "> · "
            << (int)std::round(ranked[i].second) << " dmg\n";

    auto em = style::card(style::RED, "⚔️ " + s.fish->name + " " + s.fish->emoji,
        render_health_bar(s.current_hp, s.total_hp, 14) + "\n" + headline + "\nEscapes " + style::when(s.expires_at));
    em.add_field("Top damage", top.str().empty() ? "No hits yet." : top.str(), true)
      .add_field("Fighters", std::to_string(s.contributions.size()), true);
    if (!s.gif.empty()) em.set_image(s.gif);
    return em;
}

dpp::component attack_row(const std::string& guild_id) {
    dpp::component row;
    row.add_component(dpp::component().set_type(dpp::cot_button).set_label("Attack!").set_emoji("⚔️")
        .set_style(dpp::cos_danger).set_id("boss_" + guild_id));
    return row;
}

}  // namespace

void cmd_boss(dpp::cluster&, const Slash& e, Database&) {
    auto sess = BossSessionManager::instance().get(e.command.guild_id.str());
    if (!sess) {
        cmd::show(e, style::card(style::GREY, "⚔️ World Boss", "Nothing lurking here right now."));
        return;
    }
    cmd::show(e, boss_card(*sess, "The fight is on."));
}

bool spawn_boss(dpp::cluster& bot, const std::string& guild_id, const std::string& channel_id) {
    if (BossSessionManager::instance().has_active(guild_id)) return false;
    const FishSpecies* species = pick_boss_fish();
    if (!species) return false;

    BossSession s;
    s.guild_id = guild_id;
    s.channel_id = channel_id;
    s.fish = species;
    s.total_hp = boss_hp_for(*species);
    s.current_hp = s.total_hp;
    std::uniform_real_distribution<double> kg(species->min_weight, species->max_weight);
    s.weight = std::round(kg(rng()) * 100.0) / 100.0;
    s.expires_at = cmd::now() + BOSS_WINDOW_SECONDS;
    s.gif = gifs::pick("boss");
    BossSessionManager::instance().put(guild_id, s);

    auto em = boss_card(s, "**A World Boss has surfaced!** Everyone can hit **Attack!** to bring it down.");
    em.set_title("⚠️ " + species->name + " " + species->emoji + " appears");

    dpp::message m;
    m.channel_id = dpp::snowflake(channel_id);
    m.add_embed(em);
    m.add_component(attack_row(guild_id));
    bot.message_create(m);
    return true;
}

void boss_timed_out(dpp::cluster& bot, Database& db, const std::string& guild_id) {
    auto found = BossSessionManager::instance().get(guild_id);
    if (!found) return;
    BossSession s = *found;
    BossSessionManager::instance().erase(guild_id);

    std::string body = "The **" + s.fish->name + "** slipped away.";
    if (!s.contributions.empty()) {
        body += " Everyone who swung at it gets " + coins(50) + " for the effort.";
        for (auto& [user, dmg] : s.contributions) {
            (void)dmg;
            UserRow p = db.get_or_create_user(user);
            p.balance += 50;
            cmd::grant_xp(db, p, 10);
        }
    }
    auto em = style::card(style::GREY, "💨 " + s.fish->name + " escaped", body);
    style::add_gif(em, "escape");
    dpp::message m;
    m.channel_id = dpp::snowflake(s.channel_id);
    m.add_embed(em);
    bot.message_create(m);
}

void boss_click(dpp::cluster& bot, const dpp::button_click_t& e, Database& db, const std::string& guild_id) {
    auto found = BossSessionManager::instance().get(guild_id);
    if (!found) {
        e.reply(dpp::ir_channel_message_with_source,
                dpp::message("That fight is already over.").set_flags(dpp::m_ephemeral));
        return;
    }
    BossSession s = *found;
    std::string clicker = cmd::uid(e);
    int64_t t = cmd::now();

    if (t > s.expires_at) {
        boss_timed_out(bot, db, guild_id);
        e.reply(dpp::ir_update_message, dpp::message("💨 Too slow, the boss got away."));
        return;
    }
    auto last = s.last_hit.find(clicker);
    if (last != s.last_hit.end() && t - last->second < BOSS_COOLDOWN_SECONDS) {
        e.reply(dpp::ir_channel_message_with_source,
                dpp::message("Catch your breath for a second.").set_flags(dpp::m_ephemeral));
        return;
    }
    s.last_hit[clicker] = t;

    UserRow u = db.get_or_create_user(clicker);
    double pet_bonus = 0.0;
    if (const PetSpecies* p = u.pet_id >= 0 ? find_pet(u.pet_id) : nullptr)
        pet_bonus = p->catch_bonus * (1.0 + 0.05 * (u.pet_level - 1));
    double dmg = boss_damage_per_hit(u.rod_tier, pet_bonus);
    s.current_hp -= dmg;
    s.contributions[clicker] += dmg;

    if (s.current_hp > 0) {
        BossSessionManager::instance().update(guild_id, s);
        dpp::message m;
        m.add_embed(boss_card(s, "<@" + clicker + "> hits for **" + std::to_string((int)std::round(dmg)) + "**!"));
        m.add_component(attack_row(guild_id));
        e.reply(dpp::ir_update_message, m);
        return;
    }

    // down
    BossSessionManager::instance().erase(guild_id);
    std::string top_user;
    double top_dmg = -1, total_dmg = 0;
    for (auto& [user, d] : s.contributions) {
        total_dmg += d;
        if (d > top_dmg) { top_dmg = d; top_user = user; }
    }

    int64_t pot = (int64_t)std::round(s.total_hp * 5);
    for (auto& [user, d] : s.contributions) {
        double share = total_dmg > 0 ? d / total_dmg : 0;
        UserRow p = db.get_or_create_user(user);
        p.balance += (int64_t)std::round(pot * share);
        cmd::grant_xp(db, p, (int)std::round(50 * share) + 10);
        db.unlock_achievement(user, "boss_slayer");
        if (user == top_user) {
            db.add_fish(user, s.fish->id, s.weight);
            db.mark_discovered(user, s.fish->id);
            db.unlock_achievement(user, "boss_champion");
        }
    }

    auto em = style::card(style::GREEN, "🎉 " + s.fish->name + " defeated",
        "Brought down by **" + std::to_string(s.contributions.size()) + "** angler(s).\n"
        "🏆 Most damage: <@" + top_user + "> (" + std::to_string((int)std::round(top_dmg)) + ") takes the " + s.fish->name + ".\n"
        "💰 " + coins(pot) + " split by contribution.");
    style::add_gif(em, "victory");
    dpp::message m;
    m.add_embed(em);
    e.reply(dpp::ir_update_message, m);
}
