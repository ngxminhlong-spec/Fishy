#include "Handlers.hpp"
#include "CommandUtil.hpp"
#include "FishData.hpp"
#include "Game.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

using cmd::coins;

namespace {

const int CAST_COOLDOWN = 8;
const int MAX_BAIT_STACK = 999;
const int INV_PAGE_SIZE = 12;

dpp::component reel_row(const std::string& session_key, Lang L) {
    dpp::component row;
    row.add_component(dpp::component().set_type(dpp::cot_button).set_label(tr(L, "fish.reel_button"))
        .set_style(dpp::cos_primary).set_id("reel_" + session_key));
    return row;
}

// River/Ocean/... catches feed the matching daily quest.
void bump_location_quests(Database& db, const std::string& user, const std::string& location,
                          const FishSpecies& fish) {
    static const std::map<std::string, std::string> by_place = {
        {"River", "catch_river"}, {"Ocean", "catch_ocean"}, {"Lake", "catch_lake"},
        {"Deep Sea", "catch_deep"}, {"Frozen Fjord", "catch_fjord"},
    };
    auto it = by_place.find(location);
    if (it != by_place.end()) db.bump_quest_progress(user, it->second, 1);
    if (fish.rarity >= Rarity::Epic) db.bump_quest_progress(user, "catch_epic", 1);
}

std::string fish_tags(const FishSpecies& f) {
    std::string tags;
    if (!f.event_key.empty()) tags += " 🎉";
    if (f.boss_only) tags += " 👑";
    if (!f.required_weather.empty()) tags += " ☁️";
    if (f.requires_tournament) tags += " 🏁";
    return tags;
}

}  // namespace

// --- /fish ---

void cmd_fish(dpp::cluster&, const Slash& e, Database& db) {
    std::string id = cmd::uid(e);
    Lang L = cmd::lang(e, db);
    const dpp::user& player = e.command.get_issuing_user();
    UserRow u = db.get_or_create_user(id);

    int64_t t = cmd::now();
    if (t - u.last_cast < CAST_COOLDOWN) {
        cmd::refuse(e, trf(L, "fish.cooldown", {{"secs", std::to_string(CAST_COOLDOWN - (t - u.last_cast))}}));
        return;
    }
    const Location* spot = find_location(u.location);
    if (!spot) { cmd::refuse(e, tr(L, "fish.unknown_location")); return; }
    if (u.rod_tier < spot->min_rod_tier) { cmd::refuse(e, trf(L, "fish.need_rod", {{"loc", spot->name}})); return; }
    if (spot->min_boat_tier >= 0 && u.boat_tier < spot->min_boat_tier) {
        cmd::refuse(e, trf(L, "fish.need_boat", {{"loc", spot->name}}));
        return;
    }

    // one bait is used up per cast
    int bait_on_hook = -1;
    std::string bait_note;
    if (u.bait_id >= 0 && u.bait_count > 0) {
        bait_on_hook = u.bait_id;
        u.bait_count--;
        if (u.bait_count == 0) {
            const Bait* b = find_bait(u.bait_id);
            bait_note = "\n🪱 That was your last " + (b ? b->name : std::string("bait")) + ". Restock with `/shop bait`.";
            u.bait_id = -1;
        }
    } else if (u.bait_id >= 0) {
        u.bait_id = -1;
    }
    u.last_cast = t;
    db.save_user(u);

    double pet_bonus = 0.0;
    if (const PetSpecies* p = u.pet_id >= 0 ? find_pet(u.pet_id) : nullptr)
        pet_bonus = p->catch_bonus * (1.0 + 0.05 * (u.pet_level - 1));

    CastOutcome cast = resolve_cast(u.rod_tier, bait_on_hook, u.location, pet_bonus, db.get_enchants(id));
    db.bump_quest_progress(id, "cast_10", 1);

    if (!cast.bit) {
        auto em = style::card(style::GREY, "🌊 Nothing yet", trf(L, "fish.no_bite", {{"loc", spot->name}}) + bait_note);
        style::sign(em, player);
        style::add_gif(em, "miss");
        cmd::show(e, em);
        return;
    }

    const Rod* rod = find_rod(u.rod_tier);
    int window = std::max(12, 5 + cast.reels_required * 4);

    ReelSession session;
    session.user_id = id;
    session.fish = cast.fish;
    session.weight = cast.weight;
    session.rod_power = rod ? rod->power : 2;
    session.reels_needed = cast.reels_required;
    session.expires_at = t + window;
    session.tournament_catch = WorldState::instance().tournament_active() &&
                               WorldState::instance().tournament_location() == u.location;

    std::uniform_int_distribution<int> tiebreak(0, 999999);
    std::string key = id + "_" + std::to_string(t) + "_" + std::to_string(tiebreak(rng()));
    ReelSessionManager::instance().put(key, session);

    auto em = style::card(style::rarity_color(cast.fish->rarity),
                          trf(L, "fish.bite_title", {{"emoji", cast.fish->emoji}}),
                          trf(L, "fish.bite_desc", {{"name", cast.fish->name},
                                                    {"rarity", tr_rarity(L, cast.fish->rarity)},
                                                    {"count", std::to_string(cast.reels_required)},
                                                    {"secs", std::to_string(window)}}) + bait_note);
    em.add_field("Rarity", std::string(rarity_emoji(cast.fish->rarity)) + " " + tr_rarity(L, cast.fish->rarity), true)
      .add_field("Reels", std::to_string(cast.reels_required), true)
      .add_field("Time", std::to_string(window) + "s", true);
    style::sign(em, player);

    dpp::message m;
    m.add_embed(em);
    m.add_component(reel_row(key, L));
    e.reply(m);
}

// --- reel button ---

void reel_click(dpp::cluster&, const dpp::button_click_t& e, Database& db, const std::string& key) {
    std::string clicker = cmd::uid(e);
    Lang L = cmd::lang(clicker, e.command.locale, db);
    const dpp::user& player = e.command.get_issuing_user();

    auto found = ReelSessionManager::instance().get(key);
    if (!found) {
        e.reply(dpp::ir_update_message, dpp::message(tr(L, "fish.line_slack")));
        return;
    }
    ReelSession s = *found;
    if (clicker != s.user_id) {
        e.reply(dpp::ir_channel_message_with_source,
                dpp::message(tr(L, "fish.not_your_fish")).set_flags(dpp::m_ephemeral));
        return;
    }

    if (cmd::now() > s.expires_at) {
        ReelSessionManager::instance().erase(key);
        auto em = style::card(style::GREY, "💨 Got away", trf(L, "fish.slipped_off", {{"name", s.fish->name}}));
        style::sign(em, player);
        style::add_gif(em, "escape");
        dpp::message m;
        m.add_embed(em);
        e.reply(dpp::ir_update_message, m);
        return;
    }

    s.reels_done++;
    if (s.reels_done < s.reels_needed) {
        ReelSessionManager::instance().update(key, s);
        auto em = style::card(style::GOLD, trf(L, "fish.reeling_title", {{"emoji", s.fish->emoji}}),
                              style::meter((double)s.reels_done / s.reels_needed, 12) + "\n" +
                              trf(L, "fish.reeling_desc", {{"done", std::to_string(s.reels_done)},
                                                           {"needed", std::to_string(s.reels_needed)}}));
        style::sign(em, player);
        dpp::message m;
        m.add_embed(em);
        m.add_component(reel_row(key, L));
        e.reply(dpp::ir_update_message, m);
        return;
    }

    // landed
    ReelSessionManager::instance().erase(key);
    UserRow u = db.get_or_create_user(s.user_id);
    std::string seen_key = "seen_" + std::to_string(s.fish->id);
    bool first_time = !db.has_achievement(s.user_id, seen_key);
    db.add_fish(s.user_id, s.fish->id, s.weight);
    db.mark_discovered(s.user_id, s.fish->id);
    db.unlock_achievement(s.user_id, seen_key);   // bookkeeping flag, never shown as an award

    if (s.tournament_catch) db.tournament_add_score(s.user_id, s.weight);

    db.bump_quest_progress(s.user_id, "catch_5", 1);
    if (s.fish->rarity >= Rarity::Uncommon) db.bump_quest_progress(s.user_id, "catch_3_uncommon", 1);
    if (s.fish->rarity >= Rarity::Rare) db.bump_quest_progress(s.user_id, "catch_1_rare", 1);
    bump_location_quests(db, s.user_id, u.location, *s.fish);

    std::string level_line = cmd::grant_xp(db, u, xp_for_catch(*s.fish));
    auto unlocked = check_achievements(db, s.user_id, s.fish, s.weight);

    std::ostringstream body;
    body << trf(L, "fish.caught_line", {{"name", s.fish->name}, {"emoji", s.fish->emoji},
                                        {"rarity", tr_rarity(L, s.fish->rarity)},
                                        {"weight", cmd::weight(s.weight)}});
    if (first_time) body << "\n" << tr(L, "fish.new_fishdex");
    if (!level_line.empty()) body << "\n" << level_line;
    for (auto& name : unlocked) body << "\n" << trf(L, "fish.achievement_unlocked", {{"name", name}});

    int worth = market_sell_price(*s.fish, s.weight);
    auto em = style::card(style::rarity_color(s.fish->rarity),
                          trf(L, "fish.caught_title", {{"emoji", s.fish->emoji}}), body.str());
    em.add_field("Weight", cmd::weight(s.weight), true)
      .add_field("Rarity", std::string(rarity_emoji(s.fish->rarity)) + " " + tr_rarity(L, s.fish->rarity), true)
      .add_field("Worth today", cmd::coins_l(L, worth), true);
    style::sign(em, player);
    style::add_gif(em, s.fish->rarity >= Rarity::Epic ? "legend" : "catch");

    dpp::message m;
    m.add_embed(em);
    e.reply(dpp::ir_update_message, m);
}

// --- /shop ---

namespace {

// ✅ already yours, 🟢 you can pay for it, ⚪ not yet
std::string stock_mark(bool owned, bool affordable) {
    return owned ? "✅" : (affordable ? "🟢" : "⚪");
}

dpp::embed shop_front(const UserRow& u) {
    std::ostringstream rods, baits, boats, pets;
    for (auto& r : all_rods())
        rods << stock_mark(r.tier <= u.rod_tier, u.balance >= r.price) << " **" << r.name << "** · power " << r.power
             << " · " << coins(r.price) << " · tier " << r.tier << "\n";
    for (auto& b : all_bait())
        baits << stock_mark(false, u.balance >= b.price) << " **" << b.name << "** · " << coins(b.price)
              << " each · id " << b.id << "\n";
    for (auto& b : all_boats())
        boats << stock_mark(b.tier <= u.boat_tier, u.balance >= b.price) << " **" << b.name << "** · "
              << coins(b.price) << " · tier " << b.tier << "\n";
    for (auto& p : all_pets())
        pets << stock_mark(u.pet_id == p.id, u.balance >= p.price) << " " << p.emoji << " **" << p.name << "** · "
             << coins(p.price) << " · id " << p.id << "\n";

    auto em = style::card(style::BLUE, "🛒 Fishing Shop",
                          "You have " + coins(u.balance) + "\n✅ owned   🟢 you can afford it   ⚪ saving up");
    em.add_field("🎣 Rods  `/shop rod tier:`", rods.str(), false)
      .add_field("🪱 Bait  `/shop bait id: amount:`", baits.str(), false)
      .add_field("🚤 Boats  `/shop boat tier:`", boats.str(), false)
      .add_field("🐾 Pets  `/shop pet id:`", pets.str(), false);
    return em;
}

}  // namespace

void cmd_shop(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    UserRow u = db.get_or_create_user(id);
    const dpp::user& player = e.command.get_issuing_user();

    if (what == "view" || what.empty()) {
        auto em = shop_front(u);
        style::sign(em, player);
        cmd::show(e, em);
        return;
    }

    if (what == "rod") {
        int tier = (int)cmd::opt_int(e, "tier").value_or(-1);
        const Rod* r = find_rod(tier);
        if (!r) { cmd::refuse(e, "There's no rod with that tier. `/shop view` lists them."); return; }
        if (tier <= u.rod_tier) { cmd::refuse(e, "You already carry a rod that good or better."); return; }
        if (u.balance < r->price) {
            cmd::refuse(e, "The **" + r->name + "** costs " + coins(r->price) + ". You're " +
                           coins(r->price - u.balance) + " short.");
            return;
        }
        u.balance -= r->price;
        u.rod_tier = tier;
        db.save_user(u);
        auto em = style::card(style::GREEN, "🎣 New rod: " + r->name);
        em.add_field("Power", std::to_string(r->power), true)
          .add_field("Rare luck", "x" + std::to_string(r->rare_bonus).substr(0, 4), true)
          .add_field("Balance", coins(u.balance), true);
        style::sign(em, player);
        cmd::show(e, em);
        return;
    }

    if (what == "bait") {
        int bait_id = (int)cmd::opt_int(e, "id").value_or(-1);
        int amount = std::max(1, (int)cmd::opt_int(e, "amount").value_or(1));
        const Bait* b = find_bait(bait_id);
        if (!b) { cmd::refuse(e, "I don't stock that bait. `/shop view` has the list."); return; }
        if (u.bait_id >= 0 && u.bait_count > 0 && u.bait_id != bait_id) {
            const Bait* held = find_bait(u.bait_id);
            cmd::refuse(e, "Your hook is still loaded with " + std::to_string(u.bait_count) + "x " +
                           (held ? held->name : std::string("bait")) + ". Use it up first, or buy more of the same.");
            return;
        }
        int have = (u.bait_id == bait_id) ? u.bait_count : 0;
        if (have + amount > MAX_BAIT_STACK) {
            cmd::refuse(e, "A tackle box only holds " + std::to_string(MAX_BAIT_STACK) + " of one bait.");
            return;
        }
        int64_t cost = (int64_t)b->price * amount;
        if (u.balance < cost) { cmd::refuse(e, std::to_string(amount) + "x " + b->name + " costs " + coins(cost) + "."); return; }
        u.balance -= cost;
        u.bait_id = bait_id;
        u.bait_count = have + amount;
        db.save_user(u);
        auto em = style::card(style::GREEN, "🪱 Bait loaded: " + b->name);
        em.add_field("Bought", std::to_string(amount) + "x", true)
          .add_field("In your box", std::to_string(u.bait_count), true)
          .add_field("Paid", coins(cost), true);
        style::sign(em, player);
        cmd::show(e, em);
        return;
    }

    if (what == "boat") {
        int tier = (int)cmd::opt_int(e, "tier").value_or(-1);
        const Boat* b = find_boat(tier);
        if (!b) { cmd::refuse(e, "There's no boat with that tier."); return; }
        if (tier <= u.boat_tier) { cmd::refuse(e, "You already own a boat that size or bigger."); return; }
        if (u.balance < b->price) {
            cmd::refuse(e, "The **" + b->name + "** costs " + coins(b->price) + ". You're " +
                           coins(b->price - u.balance) + " short.");
            return;
        }
        u.balance -= b->price;
        u.boat_tier = tier;
        db.save_user(u);
        auto em = style::card(style::GREEN, "🚤 You own a " + b->name, "Deeper water just opened up. Check `/location view`.");
        em.add_field("Paid", coins(b->price), true).add_field("Balance", coins(u.balance), true);
        style::sign(em, player);
        cmd::show(e, em);
        return;
    }

    if (what == "pet") {
        int pet_id = (int)cmd::opt_int(e, "id").value_or(-1);
        const PetSpecies* p = find_pet(pet_id);
        if (!p) { cmd::refuse(e, "No pet with that id."); return; }
        if (u.balance < p->price) { cmd::refuse(e, "**" + p->name + "** costs " + coins(p->price) + "."); return; }
        u.balance -= p->price;
        u.pet_id = pet_id;
        u.pet_level = 1;
        u.pet_xp = 0;
        db.save_user(u);
        auto em = style::card(style::GREEN, p->emoji + " Meet " + p->name,
                              "It rides along on every cast from now on.");
        em.add_field("Bite bonus", "+" + std::to_string((int)std::round(p->catch_bonus * 100)) + "%", true)
          .add_field("Sell bonus", "x" + std::to_string(p->value_bonus).substr(0, 4), true)
          .add_field("Paid", coins(p->price), true);
        style::sign(em, player);
        cmd::show(e, em);
        return;
    }
}

// --- /sell ---

void cmd_sell(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    UserRow u = db.get_or_create_user(id);
    Enchants en = db.get_enchants(id);
    const dpp::user& player = e.command.get_issuing_user();

    if (what == "one") {
        int inv_id = (int)cmd::opt_int(e, "inv_id").value_or(-1);
        auto item = db.get_inventory_item(inv_id);
        if (!item || item->user_id != id) { cmd::refuse(e, "That item isn't in your inventory."); return; }
        if (item->in_aquarium) { cmd::refuse(e, "That one is on display. Take it out with `/aquarium remove` first."); return; }
        const FishSpecies* f = find_fish(item->fish_id);
        if (!f) { cmd::refuse(e, "I don't recognise that fish. Tell the bot owner."); return; }
        int price = apply_sell_bonus(market_sell_price(*f, item->weight), en.greedy);
        db.remove_inventory_item(inv_id);
        u.balance += price;
        db.save_user(u);
        db.bump_quest_progress(id, "sell_300", price);

        auto em = style::card(style::GOLD, "💰 Sold " + f->emoji + " " + f->name);
        em.add_field("Weight", cmd::weight(item->weight), true)
          .add_field("Got", coins(price), true)
          .add_field("Balance", coins(u.balance), true);
        style::sign(em, player);
        style::add_gif(em, "sell");
        cmd::show(e, em);
        return;
    }

    if (what == "all") {
        int count = 0;
        int64_t total = 0;
        const FishSpecies* best = nullptr;
        int best_price = 0;
        for (auto& item : db.get_inventory(id)) {
            if (item.in_aquarium) continue;
            const FishSpecies* f = find_fish(item.fish_id);
            if (!f) continue;
            int price = apply_sell_bonus(market_sell_price(*f, item.weight), en.greedy);
            total += price;
            count++;
            if (price > best_price) { best_price = price; best = f; }
            db.remove_inventory_item(item.inv_id);
        }
        if (count == 0) { cmd::refuse(e, "Nothing to sell. Fish in your aquarium are kept."); return; }
        u.balance += total;
        db.save_user(u);
        db.bump_quest_progress(id, "sell_300", (int)total);

        auto em = style::card(style::GOLD, "💰 Cleared out the cooler");
        em.add_field("Fish sold", std::to_string(count), true)
          .add_field("Total", coins(total), true)
          .add_field("Balance", coins(u.balance), true);
        if (best) em.add_field("Best sale", best->emoji + " " + best->name + " for " + coins(best_price), false);
        style::sign(em, player);
        style::add_gif(em, "sell");
        cmd::show(e, em);
        return;
    }
}

// --- inventory, aquarium ---

dpp::message page_inventory(Database& db, const std::string& user, int page) {
    auto items = db.get_inventory(user);
    int pages = std::max(1, ((int)items.size() + INV_PAGE_SIZE - 1) / INV_PAGE_SIZE);
    page = std::clamp(page, 0, pages - 1);

    std::ostringstream body;
    int64_t worth = 0;
    for (auto& item : items) {
        if (const FishSpecies* f = find_fish(item.fish_id)) worth += market_sell_price(*f, item.weight);
    }
    for (int i = page * INV_PAGE_SIZE; i < (int)items.size() && i < (page + 1) * INV_PAGE_SIZE; i++) {
        const auto& item = items[i];
        const FishSpecies* f = find_fish(item.fish_id);
        if (!f) continue;
        body << "`#" << item.inv_id << "` " << rarity_emoji(f->rarity) << " " << f->emoji << " **" << f->name
             << "** · " << cmd::weight(item.weight) << " · " << coins(market_sell_price(*f, item.weight))
             << (item.in_aquarium ? " · 🏺" : "") << "\n";
    }

    auto em = style::card(style::BLUE, "🎒 Your catch", body.str());
    em.add_field("Fish", std::to_string(items.size()), true).add_field("Market value", coins(worth), true);

    dpp::message m;
    m.add_embed(em);
    if (pages > 1) m.add_component(cmd::pager_buttons("inv", user, page, pages));
    m.set_flags(dpp::m_ephemeral);
    return m;
}

void cmd_inventory(dpp::cluster&, const Slash& e, Database& db) {
    std::string id = cmd::uid(e);
    if (db.count_fish(id) == 0) { cmd::refuse(e, "Your cooler is empty. Try `/fish`."); return; }
    cmd::show(e, page_inventory(db, id, 0));
}

void cmd_aquarium(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);

    if (what == "add" || what == "remove") {
        int inv_id = (int)cmd::opt_int(e, "inv_id").value_or(-1);
        auto item = db.get_inventory_item(inv_id);
        if (!item || item->user_id != id) { cmd::refuse(e, "That item isn't in your inventory."); return; }
        db.set_aquarium(inv_id, what == "add");
        const FishSpecies* f = find_fish(item->fish_id);
        std::string name = f ? f->emoji + " " + f->name : "Fish";
        if (what == "add") cmd::done(e, "🏺 On display", name + " now lives in your aquarium and won't be sold by `/sell all`.");
        else cmd::done(e, "Back in the cooler", name + " can be sold again.");
        return;
    }

    auto items = db.get_inventory(id, true);
    if (items.empty()) { cmd::refuse(e, "Your aquarium is empty. Use `/aquarium add`."); return; }
    std::ostringstream body;
    for (auto& item : items) {
        const FishSpecies* f = find_fish(item.fish_id);
        if (!f) continue;
        body << rarity_emoji(f->rarity) << " " << f->emoji << " **" << f->name << "** · " << cmd::weight(item.weight) << "\n";
    }
    auto em = style::card(style::PURPLE, "🏺 Your aquarium", body.str());
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em);
}

// --- fishdex, market ---

dpp::message page_fishdex(Database& db, const std::string& user, int page) {
    const auto& spots = all_locations();
    page = std::clamp(page, 0, (int)spots.size() - 1);

    auto seen_list = db.get_discovered(user);
    std::set<int> seen(seen_list.begin(), seen_list.end());

    std::vector<std::pair<std::string, std::string>> menu;
    for (auto& spot : spots) {
        auto pool = fish_for_location(spot.name);
        int have = 0;
        for (auto* f : pool) have += seen.count(f->id) ? 1 : 0;
        menu.push_back({spot.emoji + " " + spot.name, std::to_string(have) + " / " + std::to_string(pool.size()) + " found"});
    }

    const Location& here = spots[page];
    std::ostringstream body;
    int have_here = 0;
    auto pool = fish_for_location(here.name);
    for (auto* f : pool) {
        bool got = seen.count(f->id) > 0;
        have_here += got ? 1 : 0;
        body << rarity_emoji(f->rarity) << " ";
        if (got) body << f->emoji << " **" << f->name << "**";
        else body << "❔ ???";
        body << fish_tags(*f) << "\n";
    }

    auto em = style::card(style::BLUE, "📖 " + here.emoji + " " + here.name,
        style::meter(pool.empty() ? 0 : (double)have_here / pool.size(), 12) + "  " + std::to_string(have_here) +
        " / " + std::to_string(pool.size()) + "\n\n" + body.str());
    em.set_footer(dpp::embed_footer().set_text("Total: " + std::to_string(seen.size()) + " / " +
                  std::to_string(all_fish().size()) + " species  ·  🎉 event  ☁️ weather  🏁 tournament  👑 boss"));

    dpp::message m;
    m.add_embed(em);
    m.add_component(cmd::jump_menu("dex", user, page, menu));
    m.add_component(cmd::pager_buttons("dex", user, page, (int)spots.size()));
    m.set_flags(dpp::m_ephemeral);
    return m;
}

void cmd_fishdex(dpp::cluster&, const Slash& e, Database& db) {
    cmd::show(e, page_fishdex(db, cmd::uid(e), 0));
}

dpp::message page_market(const std::string& user, int page) {
    const auto& spots = all_locations();
    page = std::clamp(page, 0, (int)spots.size() - 1);

    std::vector<std::pair<std::string, std::string>> menu;
    for (auto& spot : spots) menu.push_back({spot.emoji + " " + spot.name, ""});

    const Location& here = spots[page];
    std::ostringstream body;
    for (auto* f : fish_for_location(here.name)) {
        double mult = market_multiplier(f->id);
        body << rarity_emoji(f->rarity) << " " << f->emoji << " " << f->name << " · **"
             << style::thousands((int64_t)std::round(f->base_value * mult)) << "**/kg"
             << (mult > 1.1 ? " 📈" : (mult < 0.9 ? " 📉" : "")) << "\n";
    }
    auto em = style::card(style::GOLD, "📊 Market · " + here.emoji + " " + here.name, body.str());
    em.set_footer(dpp::embed_footer().set_text("Prices swing daily, the same for everyone. 📈 above average  📉 below"));

    dpp::message m;
    m.add_embed(em);
    m.add_component(cmd::jump_menu("mkt", user, page, menu));
    m.add_component(cmd::pager_buttons("mkt", user, page, (int)spots.size()));
    return m;
}

void cmd_market(dpp::cluster&, const Slash& e, Database&) {
    cmd::show(e, page_market(cmd::uid(e), 0));
}

// --- /location, /weather ---

void cmd_location(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    Lang L = cmd::lang(e, db);
    UserRow u = db.get_or_create_user(id);

    if (what == "travel") {
        std::string name = cmd::opt_str(e, "name").value_or("");
        const Location* spot = find_location(name);
        if (!spot) { cmd::refuse(e, tr(L, "fish.unknown_location")); return; }
        if (u.rod_tier < spot->min_rod_tier) { cmd::refuse(e, tr(L, "location.need_rod")); return; }
        if (spot->min_boat_tier >= 0 && u.boat_tier < spot->min_boat_tier) { cmd::refuse(e, tr(L, "location.need_boat")); return; }
        u.location = spot->name;
        db.save_user(u);
        auto em = style::card(style::BLUE, spot->emoji + " " + spot->name, trf(L, "location.travelled",
            {{"emoji", spot->emoji}, {"name", spot->name}, {"desc", spot->desc}}));
        style::sign(em, e.command.get_issuing_user());
        cmd::show(e, em);
        return;
    }

    std::ostringstream body;
    for (auto& spot : all_locations()) {
        bool rod_ok = u.rod_tier >= spot.min_rod_tier;
        bool boat_ok = spot.min_boat_tier < 0 || u.boat_tier >= spot.min_boat_tier;
        body << (rod_ok && boat_ok ? "🔓 " : "🔒 ") << spot.emoji << " **" << spot.name << "**"
             << (u.location == spot.name ? "  ← you are here" : "") << "\n"
             << "-# " << spot.desc << " (" << trf(L, "location.rod_tier", {{"tier", std::to_string(spot.min_rod_tier)}})
             << (spot.min_boat_tier >= 0 ? ", " + trf(L, "location.boat_tier", {{"tier", std::to_string(spot.min_boat_tier)}}) : "")
             << ")\n";
    }
    auto em = style::card(style::BLUE, tr(L, "location.title"), body.str());
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em);
}

void cmd_weather(dpp::cluster&, const Slash& e, Database& db) {
    Lang L = cmd::lang(e, db);
    Weather w = WorldState::instance().weather();
    const char* effect = w == Weather::Stormy ? "weather.effect.stormy" :
                         w == Weather::Rainy  ? "weather.effect.rainy" :
                         w == Weather::Foggy  ? "weather.effect.foggy" :
                         w == Weather::Windy  ? "weather.effect.windy" : "weather.effect.normal";
    auto em = style::card(style::BLUE, std::string(weather_emoji(w)) + " " + tr_weather(L, w), tr(L, effect));
    em.add_field("Bite chance", "x" + std::to_string(weather_bite_mult(w)).substr(0, 4), true)
      .add_field("Rare luck", "x" + std::to_string(weather_rare_mult(w)).substr(0, 4), true);
    em.set_footer(dpp::embed_footer().set_text("The sky changes every 15 minutes"));
    cmd::show(e, em);
}
