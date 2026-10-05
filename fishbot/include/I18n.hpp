#pragma once
// Tiny translation layer: English (default), Vietnamese, Simplified Chinese.
// No Discord / SQLite dependency.
//
// To translate another string:
//   1. add a row to the table in I18n.cpp:  {"my.key", "English", "Tiếng Việt", "中文"}
//   2. call tr(lang, "my.key")  or  trf(lang, "my.key", {{"name", value}})
// A missing translation falls back to English, then to the key itself - so a half-done
// translation never crashes or shows an empty message.
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

std::string tr(Lang l, const std::string& key);

// Replaces every {placeholder} in the translated string.
std::string trf(Lang l, const std::string& key,
                std::initializer_list<std::pair<const char*, std::string>> args);
