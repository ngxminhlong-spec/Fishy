#pragma once
// Reaction gifs for embeds.
//
// Two sources, tried in this order:
//   1. Live search on GIPHY, when GIPHY_API_KEY is set. Refreshed every few hours.
//   2. The plain text list in gifs.txt (one "category url" pair per line).
// With neither, embeds simply go out without a gif.
//
// Env knobs: GIFS=off turns them all off, GIF_CHANCE=0..100 sets how often one is shown,
// GIF_FILE points at a different list than ./gifs.txt.
#include <dpp/dpp.h>
#include <string>

namespace gifs {

void load_file(const std::string& path);
void refresh(dpp::cluster& bot);

// Empty string when nothing is available or the dice say skip.
std::string pick(const std::string& category);

}  // namespace gifs
