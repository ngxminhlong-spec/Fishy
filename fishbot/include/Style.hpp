#pragma once
// Shared look for every embed the bot sends, so the colours and bars stay consistent.
#include <dpp/dpp.h>
#include <cstdint>
#include <string>
#include "FishData.hpp"

namespace style {

constexpr uint32_t BLUE   = 0x3b8bd9;
constexpr uint32_t GREEN  = 0x3fc380;
constexpr uint32_t GOLD   = 0xf5b83d;
constexpr uint32_t RED    = 0xe5534b;
constexpr uint32_t GREY   = 0x8b95a1;
constexpr uint32_t PURPLE = 0x9b6bdf;

uint32_t rarity_color(Rarity r);

// "▰▰▰▱▱▱" style meter, frac is clamped to 0..1.
std::string meter(double frac, int width = 10);

// 1234567 -> "1,234,567"
std::string thousands(int64_t n);

// Discord relative timestamp, e.g. "in 5 minutes".
std::string when(int64_t unix_seconds);

dpp::embed card(uint32_t color, const std::string& title, const std::string& body = "");

// Puts the player's name and avatar in the footer.
void sign(dpp::embed& e, const dpp::user& u);

// Attaches a gif if one is available for this moment.
void add_gif(dpp::embed& e, const std::string& category);

}  // namespace style
