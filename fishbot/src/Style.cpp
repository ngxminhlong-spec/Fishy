#include "Style.hpp"
#include "Gifs.hpp"
#include <algorithm>

namespace style {

uint32_t rarity_color(Rarity r) {
    switch (r) {
        case Rarity::Common:    return 0x9aa5b1;
        case Rarity::Uncommon:  return 0x4cc577;
        case Rarity::Rare:      return 0x3b8bd9;
        case Rarity::Epic:      return 0x9b6bdf;
        case Rarity::Legendary: return 0xf29a2e;
        case Rarity::Mythic:    return 0xe5484d;
    }
    return BLUE;
}

std::string meter(double frac, int width) {
    frac = std::clamp(frac, 0.0, 1.0);
    int filled = (int)(frac * width + 0.5);
    std::string out;
    for (int i = 0; i < width; i++) out += i < filled ? "▰" : "▱";
    return out;
}

std::string thousands(int64_t n) {
    bool neg = n < 0;
    std::string digits = std::to_string(neg ? -n : n);
    std::string out;
    int run = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (run && run % 3 == 0) out.push_back(',');
        out.push_back(*it);
        run++;
    }
    if (neg) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

std::string when(int64_t unix_seconds) {
    return "<t:" + std::to_string(unix_seconds) + ":R>";
}

dpp::embed card(uint32_t color, const std::string& title, const std::string& body) {
    dpp::embed e;
    e.set_color(color);
    if (!title.empty()) e.set_title(title);
    if (!body.empty()) e.set_description(body);
    return e;
}

void sign(dpp::embed& e, const dpp::user& u) {
    std::string name = u.global_name.empty() ? u.username : u.global_name;
    e.set_footer(dpp::embed_footer().set_text(name).set_icon(u.get_avatar_url(64)));
}

void add_gif(dpp::embed& e, const std::string& category) {
    std::string url = gifs::pick(category);
    if (!url.empty()) e.set_image(url);
}

}  // namespace style
