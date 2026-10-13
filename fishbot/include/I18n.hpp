#pragma once
// English, Vietnamese and Simplified Chinese.
// New string: add a row in I18n.cpp, then tr(lang, "key") or trf(lang, "key", {{"name", v}}).
// Missing translations fall back to English, then to the key.
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

enum class Lang { EN = 0, VI = 1, ZH = 2 };

constexpr int LANG_COUNT = 3;

// "en" / "vi" / "zh" - the value stored in the database.
const char* lang_code(Lang l);

// Accepts "en", "vi", "zh", "zh-CN", "zh-TW", "vi-VN", ... (case-insensitive).
std::optional<Lang> parse_lang(const std::string& code);

// Maps a Discord client locale ("en-US", "vi", "zh-CN", "zh-TW", ...) to a supported
// language; anything unsupported becomes English.
Lang lang_from_discord_locale(const std::string& locale);

// Native display name, e.g. "Tiếng Việt".
const char* lang_native_name(Lang l);

// Display names for rarity and weather. Defined next to the enums.
enum class Rarity;
enum class Weather;
std::string tr_rarity(Lang l, Rarity r);
std::string tr_weather(Lang l, Weather w);

std::string tr(Lang l, const std::string& key);

// Replaces every {placeholder} in the translated string.
std::string trf(Lang l, const std::string& key,
                std::initializer_list<std::pair<const char*, std::string>> args);
