#include "Handlers.hpp"
#include "CommandUtil.hpp"
#include "FishData.hpp"
#include "Game.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

using cmd::coins;

namespace {

const char* MEDALS[] = {"🥇", "🥈", "🥉"};

std::string rank_tag(int place) {
    return place <= 3 ? MEDALS[place - 1] : "`" + std::to_string(place) + ".`";
}

// Trade offers are stored as "coins:N;fish:1,2,3".
std::string pack_offer(int64_t coins_offered, const std::vector<int>& fish_ids) {
    std::ostringstream ss;
    ss << "coins:" << coins_offered << ";fish:";
    for (size_t i = 0; i < fish_ids.size(); i++) ss << (i ? "," : "") << fish_ids[i];
    return ss.str();
}

void unpack_offer(const std::string& s, int64_t& coins_offered, std::vector<int>& fish_ids) {
    coins_offered = 0;
    fish_ids.clear();
    auto c = s.find("coins:");
    auto semi = s.find(';');
    if (c != std::string::npos && semi != std::string::npos)
        coins_offered = std::stoll(s.substr(c + 6, semi - (c + 6)));
    auto f = s.find("fish:");
    if (f == std::string::npos) return;
    std::stringstream rest(s.substr(f + 5));
    std::string tok;
    while (std::getline(rest, tok, ','))
        if (!tok.empty()) fish_ids.push_back(std::stoi(tok));
}

std::string describe_offer(Database& db, const std::string& packed) {
    int64_t c;
    std::vector<int> ids;
    unpack_offer(packed, c, ids);
    std::ostringstream ss;
    ss << coins(c);
    for (int inv_id : ids) {
        auto item = db.get_inventory_item(inv_id);
        const FishSpecies* f = item ? find_fish(item->fish_id) : nullptr;
        if (f) ss << "\n" << f->emoji << " " << f->name << " · " << cmd::weight(item->weight) << " `#" << inv_id << "`";
        else ss << "\n`#" << inv_id << "`";
    }
    return ss.str();
}

dpp::embed trade_card(Database& db, const PendingTrade& t, const std::string& headline) {
    auto em = style::card(style::BLUE, "🤝 Trade #" + std::to_string(t.trade_id), headline);
    em.add_field(std::string(t.a_confirmed ? "✅ " : "⏳ ") + "Offer from <@" + t.user_a + ">", describe_offer(db, t.offer_a), true)
      .add_field(std::string(t.b_confirmed ? "✅ " : "⏳ ") + "Offer from <@" + t.user_b + ">", describe_offer(db, t.offer_b), true);
    return em;
}

}  // namespace

// --- balance, daily, gift ---

void cmd_balance(dpp::cluster&, const Slash& e, Database& db) {
    Lang L = cmd::lang(e, db);
    UserRow u = db.get_or_create_user(cmd::uid(e));
    auto em = style::card(style::GOLD, "", trf(L, "balance.show", {{"coins", cmd::coins_l(L, u.balance)}}));
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em);
}

void cmd_daily(dpp::cluster&, const Slash& e, Database& db) {
    Lang L = cmd::lang(e, db);
    UserRow u = db.get_or_create_user(cmd::uid(e));
    int64_t t = cmd::now();
    int64_t since = t - u.last_daily;

    if (u.last_daily != 0 && since < 20 * 3600) {
        cmd::refuse(e, trf(L, "daily.cooldown", {{"hours", std::to_string((20 * 3600 - since) / 3600 + 1)}}));
        return;
    }
    if (u.last_daily != 0 && since > 48 * 3600) u.daily_streak = 0;
    u.daily_streak++;
    int64_t reward = 100 + (int64_t)std::min(u.daily_streak, 30) * 20;
    u.balance += reward;
    u.last_daily = t;
    db.save_user(u);
    if (u.daily_streak % 10 == 0) db.unlock_achievement(cmd::uid(e), "ten_quests");

    auto em = style::card(style::GOLD, "📅 Daily reward",
        trf(L, "daily.claimed", {{"coins", cmd::coins_l(L, reward)}, {"streak", std::to_string(u.daily_streak)}}));
    em.add_field("Streak", style::meter(std::min(u.daily_streak, 30) / 30.0, 10) + " " + std::to_string(u.daily_streak) + "/30", false)
      .add_field("Balance", coins(u.balance), true);
    style::sign(em, e.command.get_issuing_user());
    style::add_gif(em, "daily");
    cmd::show(e, em);
}

void cmd_gift(dpp::cluster&, const Slash& e, Database& db) {
    auto target = cmd::opt_user(e, "user");
    int64_t amount = cmd::opt_int(e, "amount").value_or(0);
    if (!target) { cmd::refuse(e, "Pick someone to gift."); return; }
    std::string to = target->str();
    std::string from = cmd::uid(e);
    if (to == from) { cmd::refuse(e, "Gifting yourself just moves coins around in a circle."); return; }
    if (amount <= 0) { cmd::refuse(e, "The amount has to be above zero."); return; }
    UserRow giver = db.get_or_create_user(from);
    if (giver.balance < amount) { cmd::refuse(e, "You only have " + coins(giver.balance) + "."); return; }
    UserRow taker = db.get_or_create_user(to);
    giver.balance -= amount;
    taker.balance += amount;
    db.save_user(giver);
    db.save_user(taker);
    auto em = style::card(style::GREEN, "🎁 Gift sent", "<@" + from + "> sent " + coins(amount) + " to <@" + to + ">.");
    style::add_gif(em, "money");
    cmd::show(e, em);
}

// --- /trade ---

void cmd_trade(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string me = cmd::uid(e);

    if (what == "start") {
        auto other = cmd::opt_user(e, "user");
        if (!other) { cmd::refuse(e, "Pick who you want to trade with."); return; }
        std::string partner = other->str();
        if (partner == me) { cmd::refuse(e, "You can't trade with yourself."); return; }
        int trade_id = db.create_trade(me, partner);
        auto em = style::card(style::BLUE, "🤝 Trade #" + std::to_string(trade_id) + " opened",
            "<@" + me + "> wants to trade with <@" + partner + ">.\n\n"
            "Both of you can use `/trade addcoins`, `/trade additem` and `/trade view` with trade id **" +
            std::to_string(trade_id) + "**, then `/trade confirm` when you're happy.");
        cmd::show(e, em);
        return;
    }

    int trade_id = (int)cmd::opt_int(e, "trade_id").value_or(-1);
    auto trade = db.get_trade(trade_id);
    if (!trade) { cmd::refuse(e, "I can't find that trade."); return; }
    if (me != trade->user_a && me != trade->user_b) { cmd::refuse(e, "That trade isn't yours."); return; }
    bool i_am_a = (me == trade->user_a);
    std::string& my_offer = i_am_a ? trade->offer_a : trade->offer_b;

    if (what == "view") {
        cmd::show(e, trade_card(db, *trade, "Current offers."));
        return;
    }

    if (what == "addcoins") {
        int64_t amount = cmd::opt_int(e, "amount").value_or(0);
        UserRow u = db.get_or_create_user(me);
        if (amount < 0 || amount > u.balance) { cmd::refuse(e, "You can't offer " + coins(amount) + ". You hold " + coins(u.balance) + "."); return; }
        int64_t old_coins;
        std::vector<int> ids;
        unpack_offer(my_offer, old_coins, ids);
        my_offer = pack_offer(amount, ids);
        trade->a_confirmed = trade->b_confirmed = false;
        db.update_trade(*trade);
        cmd::show(e, trade_card(db, *trade, "<@" + me + "> changed their coin offer. Confirmations reset."));
        return;
    }

    if (what == "additem") {
        int inv_id = (int)cmd::opt_int(e, "inv_id").value_or(-1);
        auto item = db.get_inventory_item(inv_id);
        if (!item || item->user_id != me) { cmd::refuse(e, "That item isn't in your inventory."); return; }
        int64_t offered_coins;
        std::vector<int> ids;
        unpack_offer(my_offer, offered_coins, ids);
        if (std::find(ids.begin(), ids.end(), inv_id) == ids.end()) ids.push_back(inv_id);
        my_offer = pack_offer(offered_coins, ids);
        trade->a_confirmed = trade->b_confirmed = false;
        db.update_trade(*trade);
        cmd::show(e, trade_card(db, *trade, "<@" + me + "> added a fish. Confirmations reset."));
        return;
    }

    if (what == "cancel") {
        db.delete_trade(trade_id);
        cmd::show(e, style::card(style::GREY, "Trade #" + std::to_string(trade_id) + " cancelled",
                                 "<@" + me + "> called it off."));
        return;
    }

    if (what == "confirm") {
        (i_am_a ? trade->a_confirmed : trade->b_confirmed) = true;
        db.update_trade(*trade);
        if (!(trade->a_confirmed && trade->b_confirmed)) {
            cmd::show(e, trade_card(db, *trade, "<@" + me + "> is happy with the deal. Waiting on the other side."));
            return;
        }

        int64_t coins_a, coins_b;
        std::vector<int> fish_a, fish_b;
        unpack_offer(trade->offer_a, coins_a, fish_a);
        unpack_offer(trade->offer_b, coins_b, fish_b);
        UserRow ua = db.get_or_create_user(trade->user_a);
        UserRow ub = db.get_or_create_user(trade->user_b);

        if (ua.balance < coins_a || ub.balance < coins_b) {
            db.delete_trade(trade_id);
            cmd::refuse(e, "Trade #" + std::to_string(trade_id) + " fell through: someone can't cover their coins anymore.");
            return;
        }
        auto still_owned = [&](const std::vector<int>& ids, const std::string& owner) {
            for (int inv : ids) {
                auto it = db.get_inventory_item(inv);
                if (!it || it->user_id != owner) return false;
            }
            return true;
        };
        if (!still_owned(fish_a, trade->user_a) || !still_owned(fish_b, trade->user_b)) {
            db.delete_trade(trade_id);
            cmd::refuse(e, "Trade #" + std::to_string(trade_id) + " fell through: an offered fish is gone.");
            return;
        }

        auto em = trade_card(db, *trade, "✅ Done. Everything changed hands.");
        em.set_color(style::GREEN);
        style::add_gif(em, "trade");

        ua.balance += coins_b - coins_a;
        ub.balance += coins_a - coins_b;
        db.save_user(ua);
        db.save_user(ub);
        auto move_fish = [&](const std::vector<int>& ids, const std::string& to) {
            for (int inv : ids) {
                auto it = db.get_inventory_item(inv);
                db.remove_inventory_item(inv);
                db.add_fish(to, it->fish_id, it->weight);
            }
        };
        move_fish(fish_a, trade->user_b);
        move_fish(fish_b, trade->user_a);

        db.delete_trade(trade_id);
        cmd::show(e, em);
        return;
    }
}

// --- /leaderboard ---

void cmd_leaderboard(dpp::cluster&, const Slash& e, Database& db) {
    std::string kind = cmd::opt_str(e, "type").value_or("balance");
    std::ostringstream body;
    std::string title;
    int place = 1;

    if (kind == "fish") {
        title = "🐟 Most fish caught";
        for (auto& r : db.top_by_fish_count(10)) body << rank_tag(place++) << " <@" << r.user_id << "> · **" << r.xp << "** fish\n";
    } else if (kind == "tournament") {
        title = "🏁 Tournament standings";
        for (auto& r : db.tournament_leaderboard(10)) body << rank_tag(place++) << " <@" << r.first << "> · **" << cmd::weight(r.second) << "**\n";
    } else {
        title = "💰 Richest anglers";
        for (auto& r : db.top_by_balance(10)) body << rank_tag(place++) << " <@" << r.user_id << "> · " << coins(r.balance) << "\n";
    }
    std::string text = body.str();
    cmd::show(e, style::card(style::GOLD, title, text.empty() ? "Nobody on the board yet." : text));
}

// --- /enchant ---

void cmd_enchant(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    UserRow u = db.get_or_create_user(id);

    if (what == "upgrade") {
        std::string key = cmd::opt_str(e, "name").value_or("");
        const EnchantDef* def = find_enchant(key);
        if (!def) { cmd::refuse(e, "I don't know that enchant."); return; }
        DbResult r = db.upgrade_enchant(id, key);
        if (!r.ok) {
            if (r.error == "not_enough_coins")
                cmd::refuse(e, "The next level of **" + def->name + "** costs " + coins(r.amount) + ". You have " + coins(u.balance) + ".");
            else
                cmd::refuse(e, r.error);
            return;
        }
        auto em = style::card(style::PURPLE, def->emoji + " " + def->name + " upgraded",
                              style::meter((double)r.id / def->max_level, def->max_level) + "  level " +
                              std::to_string(r.id) + " / " + std::to_string(def->max_level));
        em.add_field("Paid", coins(r.amount), true);
        style::sign(em, e.command.get_issuing_user());
        cmd::show(e, em);
        return;
    }

    Enchants en = db.get_enchants(id);
    auto level_of = [&](const std::string& key) {
        if (key == "lucky") return en.lucky;
        if (key == "swift") return en.swift;
        if (key == "greedy") return en.greedy;
        return en.magnetic;
    };
    auto em = style::card(style::PURPLE, "✨ Enchants", "Permanent. They stay when you change rods.\nBuy a level with `/enchant upgrade`.");
    for (auto& d : all_enchants()) {
        int lv = level_of(d.key);
        std::string next = lv >= d.max_level ? "**MAX**" : "Next: " + coins(enchant_cost(d, lv + 1));
        em.add_field(d.emoji + " " + d.name,
                     style::meter((double)lv / d.max_level, d.max_level) + " " + std::to_string(lv) + "/" +
                     std::to_string(d.max_level) + "\n" + d.desc + "\n" + next, false);
    }
    style::sign(em, e.command.get_issuing_user());
    cmd::show(e, em);
}

// --- /auction ---

namespace {

std::string auction_line(const AuctionRow& a, bool with_seller) {
    const FishSpecies* f = find_fish(a.fish_id);
    std::ostringstream ss;
    ss << "`#" << a.auction_id << "` ";
    if (f) ss << rarity_emoji(f->rarity) << " " << f->emoji << " **" << f->name << "**";
    else ss << "🐟 **Unknown fish**";
    ss << " · " << cmd::weight(a.weight) << " · **" << coins(a.price) << "**";
    if (f) ss << " (worth " << coins(market_sell_price(*f, a.weight)) << ")";
    if (with_seller) ss << "\n-# sold by <@" << a.seller_id << ">";
    ss << "\n";
    return ss.str();
}

}  // namespace

void cmd_auction(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    db.get_or_create_user(id);

    if (what == "list") {
        int inv_id = (int)cmd::opt_int(e, "inv_id").value_or(-1);
        int64_t price = cmd::opt_int(e, "price").value_or(0);
        DbResult r = db.auction_create(id, inv_id, price);
        if (!r.ok) { cmd::refuse(e, r.error); return; }
        const FishSpecies* f = find_fish(r.fish_id);
        int64_t tax = price * AUCTION_TAX_PERCENT / 100;
        auto em = style::card(style::GOLD, "🔨 Listed on the auction house",
            (f ? f->emoji + " **" + f->name + "**" : std::string("Your fish")) + " · " + cmd::weight(r.weight) +
            "\nListing `#" + std::to_string(r.id) + "`");
        em.add_field("Asking", coins(price), true)
          .add_field("You'll get", coins(price - tax), true)
          .add_field("Tax", std::to_string(AUCTION_TAX_PERCENT) + "%", true)
          .add_field("Expires", "Back in your cooler after " + std::to_string(AUCTION_DURATION_SECONDS / 3600) + "h if unsold", false);
        style::sign(em, e.command.get_issuing_user());
        cmd::show(e, em);
        return;
    }

    if (what == "buy") {
        int aid = (int)cmd::opt_int(e, "id").value_or(-1);
        DbResult r = db.auction_buy(id, aid);
        if (!r.ok) {
            if (r.error == "not_enough_coins") cmd::refuse(e, "That listing costs " + coins(r.amount) + ".");
            else cmd::refuse(e, r.error);
            return;
        }
        const FishSpecies* f = find_fish(r.fish_id);
        auto em = style::card(style::GREEN, "🛒 Bought at auction",
            (f ? f->emoji + " **" + f->name + "**" : std::string("A fish")) + " · " + cmd::weight(r.weight) +
            "\nPaid " + coins(r.amount) + ". It's in your `/inventory`.");
        style::sign(em, e.command.get_issuing_user());
        style::add_gif(em, "auction");
        cmd::show(e, em);
        return;
    }

    if (what == "cancel") {
        int aid = (int)cmd::opt_int(e, "id").value_or(-1);
        DbResult r = db.auction_cancel(id, aid);
        if (!r.ok) { cmd::refuse(e, r.error); return; }
        cmd::show(e, style::card(style::GREY, "↩️ Listing #" + std::to_string(aid) + " pulled", "The fish is back in your inventory."));
        return;
    }

    if (what == "mine") {
        auto mine = db.auction_by_seller(id);
        if (mine.empty()) { cmd::refuse(e, "You have nothing listed. Try `/auction list`."); return; }
        std::ostringstream body;
        for (auto& a : mine) body << auction_line(a, false);
        cmd::show(e, style::card(style::GOLD, "🔨 Your listings", body.str()), true);
        return;
    }

    const int per_page = 8;
    int total = db.auction_count();
    int pages = std::max(1, (total + per_page - 1) / per_page);
    int page = std::clamp((int)cmd::opt_int(e, "page").value_or(1), 1, pages);
    auto rows = db.auction_list(per_page, (page - 1) * per_page);
    if (rows.empty()) { cmd::show(e, style::card(style::GREY, "🔨 Auction house", "No listings yet. Be the first with `/auction list`.")); return; }
    std::ostringstream body;
    for (auto& a : rows) body << auction_line(a, true) << "\n";
    auto em = style::card(style::GOLD, "🔨 Auction house", body.str());
    em.set_footer(dpp::embed_footer().set_text("Page " + std::to_string(page) + "/" + std::to_string(pages) + " · " +
                  std::to_string(total) + " listing(s) · buy with /auction buy"));
    cmd::show(e, em);
}

// --- /lottery ---

void cmd_lottery(dpp::cluster&, const Slash& e, Database& db) {
    std::string what = cmd::sub(e);
    std::string id = cmd::uid(e);
    db.get_or_create_user(id);

    if (what == "buy") {
        int count = (int)cmd::opt_int(e, "count").value_or(1);
        DbResult r = db.lottery_buy(id, count);
        if (!r.ok) {
            if (r.error == "not_enough_coins") cmd::refuse(e, "Those tickets cost " + coins(r.amount) + ".");
            else cmd::refuse(e, r.error);
            return;
        }
        auto em = style::card(style::GOLD, "🎟️ Tickets bought",
            "You bought **" + std::to_string(count) + "** for " + coins(r.amount) + ".\nYou now hold **" +
            std::to_string(r.id) + "** this round. Good luck!");
        style::sign(em, e.command.get_issuing_user());
        style::add_gif(em, "lottery");
        cmd::show(e, em);
        return;
    }

    LotteryState st = db.lottery_get(id);
    auto em = style::card(style::GOLD, "🎟️ Daily lottery");
    em.add_field("Jackpot", coins(st.pool * LOTTERY_PAYOUT_PERCENT / 100), true)
      .add_field("Tickets sold", std::to_string(st.total_tickets), true)
      .add_field("Next draw", style::when(st.draw_at), true);

    std::ostringstream mine;
    mine << st.my_tickets << " / " << LOTTERY_MAX_TICKETS;
    if (st.my_tickets > 0 && st.total_tickets > 0)
        mine << " · " << std::fixed << std::setprecision(1) << (100.0 * st.my_tickets / st.total_tickets) << "% to win";
    em.add_field("Your tickets", mine.str(), false);
    if (!st.last_winner.empty())
        em.add_field("Last winner", "<@" + st.last_winner + "> took " + coins(st.last_prize), false);
    em.set_footer(dpp::embed_footer().set_text("Tickets " + style::thousands(LOTTERY_TICKET_PRICE) + " coins each · " +
                  std::to_string(100 - LOTTERY_PAYOUT_PERCENT) + "% of the pot is kept by the house"));
    cmd::show(e, em);
}
