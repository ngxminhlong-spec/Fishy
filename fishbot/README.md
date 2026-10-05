# FishBot — a full fishing-game Discord bot (D++ / C++)

A complete fishing economy game built with [D++ (DPP)](https://dpp.dev/), the C++
Discord library, and SQLite for persistence.

## Features

- **`/fish`** — cast a line; if something bites, a **Reel!** button minigame appears
  (click it N times within 12s, N scales with the fish's strength vs. your rod's power —
  this is the "real rule" mechanic, not pure RNG)
- **`/shop`** — buy rods (6 tiers), bait (5 types), boats (3 tiers, needed for deeper
  water), and pets
- **`/sell`** — sell one fish or your whole inventory at the live market price
- **`/trade`** — two-party trade flow: `start` → `addcoins` / `additem` → `confirm`
  (both sides must confirm; canceling or changing an offer resets confirmations)
- **`/inventory`**, **`/aquarium`** — manage caught fish; aquarium fish are protected
  from `/sell all` and just for show
- **`/balance`**, **`/daily`** — economy + daily streak bonus
- **`/gift`** — send coins to another player
- **`/location`** — 6 locations (Pond → River/Lake → Ocean → Deep Sea → Volcanic
  Vents), gated by rod tier and boat ownership
- **`/market`** — daily-fluctuating sell prices (deterministic per day, same for
  everyone)
- **`/weather`** — a rotating weather system (Sunny/Rainy/Stormy/Foggy/Windy) that
  changes bite rate and rare-fish odds every 15 minutes, and unlocks weather-gated
  event fish
- **`/profile`** — level, XP, gear, pet, stats
- **`/fishdex`** — Pokédex-style discovery log for all 97 fish species
- **`/achievements`** — 12 unlockable achievements
- **`/quest`** — a rotating daily quest with a coin reward
- **`/pet`** — a companion that passively boosts your bite chance
- **`/leaderboard`** — richest players, most fish caught, or current tournament
- **`/auction`** — player-to-player fish market: `list` a fish at your own price (it's held in escrow),
  `browse` / `buy` other players' listings, `cancel` or `mine`. 5% of the sale price is taxed;
  unsold fish go back to the seller after 48h. Coins and fish move in one database transaction.
- **`/lottery`** — one global pot. Tickets cost 500 coins (max 25 per player per round), one winner is drawn
  every 24h and takes 90% of the pot (the rest is removed from the economy). Winners are announced in
  the boss channels. Force a draw from the CLI with `fishadmin lottery draw`.
- **`/enchant`** — permanent coin sinks, 5 levels each (cost triples per level): **Lucky Lure** (+8% Rare+
  weight), **Swift Reel** (+12% reel power), **Greedy Hook** (+4% sell price), **Magnetic Bait** (+2.5% bite chance).
- **Rods 6-8** (Abyssal / Celestial / Primordial) and the **Abyssal Trench** location (Submarine + rod tier 6),
  plus 36 new fish (137 species total).
- **`/tournament`** — shows the running tournament (started from the CLI: `fishadmin tournament start`);
  catches at the target location count toward a tournament-only leaderboard
- **`/crew`** — form a fishing crew (clan) with other players: shared bank
  (`deposit`/`withdraw`), owner-controlled `kick`/`disband`, and a crew richest-bank
  leaderboard
- **`/boss`** — World Boss fights: a shared-HP boss spawns in a channel and *anyone in
  the server* can click **Attack!** to help take it down (rod tier + pet scale your
  damage). Top damage-dealer claims the boss-exclusive fish; everyone who helped gets a
  coin/XP share proportional to their contribution. The bot owner sets the spawn channel and can
  summon bosses from the CLI (`fishadmin boss setchannel` / `boss summon`), so bosses also spawn
  automatically every couple hours
- **`/help`** — in-Discord command summary
- **`/stats`** — live bot statistics (only you see the reply): uptime, servers, players, **PID**, **CPU**
  (bot's share of one core and of the whole machine), **RAM**, **virtual memory (VV)**, swap, host OS / CPU /
  load / RAM, and the **database size shown in bits, bytes, KB, MB and GB** (main file + WAL/SHM files),
  plus SQLite page info and the row count of every table
- **`/terms`**, **`/privacy`** — Terms of Service and Privacy Policy (full versions: `TERMS_OF_SERVICE.md`,
  `PRIVACY_POLICY.md`)
- **`/language`** — English, Tiếng Việt or 中文（简体）. By default the bot follows each player's Discord
  client language; `/language` overrides it per player (`Auto` goes back to following Discord)
- **Server allowlist** — set `ALLOWED_GUILD_IDS` before starting the bot and it will
  automatically leave any Discord server whose ID isn't on that list (checked both at
  startup and the instant it's added anywhere new)

That's `/shop`, `/sell`, `/trade`, the reel-based "real rule" catching system, plus
17 additional features on top.

### Server allowlist

Set `ALLOWED_GUILD_IDS` in the environment before starting the bot to a
comma-separated list of Discord server (guild) IDs — e.g.
`ALLOWED_GUILD_IDS=111111111111111111,222222222222222222`. Once set:

- On every startup, the bot checks every server it's currently in against the list
  and immediately leaves any that aren't on it.
- The instant it's invited to a new server, the same check runs and it leaves right
  away if that server isn't allowed — it never sits around waiting to be removed.

Leave `ALLOWED_GUILD_IDS` unset (or empty) and the bot places no restriction on
which servers it can be in — that's the default, so existing setups aren't affected.
This is a startup-time environment variable, not a slash command, by design — a
compromised or malicious admin inside an allowed server shouldn't be able to edit the
allowlist from within Discord.

### Languages (English / Tiếng Việt / 中文)

- **Detection:** every interaction carries the player's Discord language, so a Vietnamese or Chinese client
  gets that language automatically. `/language` stores a per-player override in the `user_prefs` table.
  Traditional Chinese (zh-TW) clients currently get Simplified Chinese.
- **Command list:** Discord shows the slash-command descriptions of `/fish`, `/balance`, `/daily`, `/help`,
  `/stats`, `/terms`, `/privacy` and `/language` in the viewer's own language.
- **Translated so far:** `/help`, `/stats`, `/terms`, `/privacy`, `/language`, `/balance`, `/daily`.
  Everything else (shop, fish names, boss text...) is still English and falls back gracefully.
- **Translating more:** all text lives in one table in `src/I18n.cpp`. Add a row
  `{"key", "English", "Tiếng Việt", "中文"}`, then replace the hard-coded string in `Commands.cpp` with
  `tr(L, "key")` / `trf(L, "key", {{"name", value}})`, where `L = lang_of(e, db)`.
  To add a whole new language, add it to the `Lang` enum, `parse_lang`, `lang_code`, `lang_native_name`,
  the table columns, and one `/language` choice.

### Terms of Service, Privacy Policy and the settings behind them

`/terms` and `/privacy` show a short in-Discord summary. Optional environment variables (put them in `.env`):

| Variable | Purpose |
|---|---|
| `BOT_CONTACT` | Contact shown at the bottom of both (email or support-server link). Quote it if it has spaces. |
| `TOS_URL` | Link to your hosted `TERMS_OF_SERVICE.md`; appended as "Full text". |
| `PRIVACY_URL` | Link to your hosted `PRIVACY_POLICY.md`. |

Fill in the `[bracketed]` placeholders in the two `.md` files, host them at a public URL, and paste the links
into the Discord Developer Portal (General Information). They are templates, not legal advice. To honour a
deletion request, run `fishadmin erase <user_id> --yes`.

### Fish catalog: big update

The regular catalog grew from 58 to 88 species — roughly 5 new fish per location,
including a handful of "junk" catches (Old Boot, Rusty Can, Message in a Bottle...)
worth almost nothing, purely for flavor. Counting the 6 event fish and 3 World Boss
exclusives, there are **97 fish total**. Spawn weights were recalculated the same way
as the previous rebalance — each rarity tier keeps a fixed overall share of a
location's pool, split evenly across however many fish now occupy that tier — so the
extra variety didn't shift the overall odds of pulling a Common vs. a Legendary,
it just gave you more different things to pull.

### Event fish

Six of the 94 non-boss species are gated behind special conditions and never appear otherwise:

| Fish | Location | Condition |
|---|---|---|
| Starlit Koi ✨ | Pond | Weather is Foggy |
| Storm Salmon ⚡ | River | Weather is Stormy |
| Aurora Serpent 🌌 | Lake | Weather is Windy |
| Tournament Marlin King 👑 | Ocean | A tournament is active there |
| Abyssal Champion 🏆 | Deep Sea | A tournament is active there |
| Molten Crown Wyrm 👑 | Volcanic Vents | A tournament **and** Stormy weather, together |

`resolve_cast()` in `Game.cpp` filters these out of a location's spawn pool entirely
unless the condition holds (see the `required_weather` / `requires_tournament` fields
on `FishSpecies`) — so they're not just low-probability, they're literally unavailable
the rest of the time. The Molten Crown Wyrm needs both a running tournament and Stormy
weather to line up, making it the hardest catch in the game.

### World Boss mechanics

Three boss-exclusive Mythic fish (`FishData.cpp`, `boss_only = true`) never appear from
a normal `/fish` cast — the only way to get one is to help defeat its boss fight:

| Boss | Location theme | HP |
|---|---|---|
| Kraken King 🐙👑 | Ocean | 1,000 |
| Leviathan Alpha 🐳👑 | Deep Sea | 1,100 |
| Inferno Wyrm King 🔥👑 | Volcanic Vents | 1,200 |

A boss fight is shared per Discord server (`BossSessionManager` keys sessions by
guild id, so multiple servers running the same bot process each get their own
independent fight). Anyone can click **Attack!** — no location or gear requirement to
participate, though a better rod and pet increase your damage per click. There's a 2
second per-user cooldown on the button to prevent spam-clicking. If the boss's HP hits
zero, the top damage-dealer gets the fish and an achievement; every contributor gets a
coin/XP payout proportional to their share of total damage. If 10 minutes pass without
a kill, the boss escapes and everyone who hit it gets a small flat consolation reward.

### Crews

`/crew` is a lightweight clan system: one crew per player (enforced via a `crew_id`
column on the `users` table), a shared coin bank any member can deposit into, and
owner-only withdraw/kick/disband. There's a crew richest-bank leaderboard. It doesn't
gate any content yet (no crew-only locations or crew-boosted boss damage) — that's a
natural next step if you want crews to matter more than bragging rights and a shared
piggy bank.

### CLI admin tool (`fishadmin`)

Alongside the bot itself, the build produces a second, independent binary —
`fishadmin` — for granting things to players straight from the command line, no
Discord interaction needed. It talks directly to the same `fishbot.sqlite3` file, so
it works whether the bot is running or not, and it has **zero dependency on DPP** (it
only needs SQLite), so it builds even on a machine where you haven't set up DPP at all.

```bash
./build/fishadmin give coin 123456789012345678 5000        # give 5,000 coins
./build/fishadmin give rod  123456789012345678 3            # give the Carbon Fiber Rod (tier 3)
./build/fishadmin give bag  123456789012345678 27 2         # give 2x Ghost Shark (fish id 27)
./build/fishadmin give bag  123456789012345678 4 1 5.5      # give 1x Golden Koi, exactly 5.5kg
./build/fishadmin give bait 123456789012345678 4            # equip Golden Lure
./build/fishadmin give boat 123456789012345678 1            # give a Yacht
./build/fishadmin give pet  123456789012345678 3            # adopt a Lucky Koi for them
./build/fishadmin enchant set 123456789012345678 lucky 3    # set an enchant level
./build/fishadmin lottery draw                               # force today's lottery draw
./build/fishadmin give xp   123456789012345678 2000

./build/fishadmin take coin 123456789012345678 200          # deduct coins
./build/fishadmin take bag  123456789012345678 27 1         # remove 1x Ghost Shark
./build/fishadmin take rod  123456789012345678               # reset to the starting Twig Rod

./build/fishadmin info 123456789012345678                    # dump everything about a player
./build/fishadmin erase 123456789012345678 --yes             # privacy request: delete ALL data about a player
./build/fishadmin list rods                                  # see valid tiers/ids for anything giveable
./build/fishadmin list fish
./build/fishadmin list giveables

./build/fishadmin --db /path/to/other.sqlite3 give coin ...   # point at a different DB file
```

`"bag"` and `"fish"` are interchangeable — both add/remove inventory items.

**Adding a new giveable resource later** is a one-function, one-line change: write a
handler matching the `GiveFn` shape in `admin_cli.cpp` (there are seven examples to
copy from), then add it to the map in `give_registry()`. The CLI dispatch, error
messages, and `list giveables` all key off that map automatically.

### Spawn weight rebalance

With more fish per location, raw per-fish spawn weights were recalculated so each
*rarity tier* keeps a consistent share of a location's pool regardless of how many
species sit in that tier (roughly Common 40 / Uncommon 24 / Rare 14 / Epic 8 /
Legendary 4 / Mythic 2, split evenly among however many fish occupy that tier at that
location). So adding more Common pond fish, for example, didn't make Commons as a
whole more likely — it just diversified which Common fish you might get.

## Project layout

```
fishbot/
  CMakeLists.txt
  include/           headers (Database, FishData, Game, Commands, I18n, SysStats)
  src/                implementation + main.cpp
```

- `Database.*` — SQLite schema + access layer (users, inventory, fishdex,
  achievements, quests, trades, tournament scores, crews, per-server settings)
- `FishData.*` — static game data: 97 fish (88 regular + 6 conditional event fish +
  3 World Boss exclusives) across 6 locations/6 rarities, rods, bait, boats, pets
- `Game.*` — core rules: weather, cast/bite/rarity resolution, market pricing,
  XP/leveling, quest & achievement definitions, in-memory reel-minigame and
  World Boss fight sessions
- `Commands.*` — slash command registration, dispatch, and the reel/boss button
  handlers
- `I18n.*` — English / Vietnamese / Chinese string table and `tr()` / `trf()` lookup (no dependencies)
- `SysStats.*` — PID / CPU / RAM / virtual-memory / host readings from `/proc`, plus size formatting for `/stats`
  (Linux only; shows "N/A" elsewhere)
- `admin_cli.cpp` — the standalone `fishadmin` CLI tool (no DPP dependency)
- `main.cpp` — cluster setup, event wiring (bot only)

## Building

Requirements:
- A C++17 compiler + CMake 3.15+
- `libsqlite3-dev` (required for both targets)
- [DPP](https://github.com/brainboxdotcc/DPP) (`libdpp-dev` on recent Debian/Ubuntu, or
  built from source) — only required for the `fishbot` Discord bot itself. The
  `fishadmin` CLI tool builds without it.

```bash
sudo apt install libsqlite3-dev cmake g++
# install DPP: either `sudo apt install libdpp-dev` if available for your distro,
# or build from source per https://dpp.dev/build-linux.html

cmake -B build
cmake --build build -j
```

This produces two binaries: `build/fishbot` (the Discord bot) and `build/fishadmin`
(the CLI admin tool — see below). If DPP isn't found, CMake prints a warning, skips
`fishbot`, and still builds `fishadmin` on its own.

## Running

1. Create a Discord application + bot at https://discord.com/developers/applications,
   enable it, and invite it to your server with the `applications.commands` and `bot`
   scopes (permissions: Send Messages, Use Slash Commands, Embed Links).
2. Run with your token in the environment:

```bash
BOT_TOKEN="your-bot-token-here" ./build/fishbot
```

Slash commands register globally on first startup (`bot.global_bulk_command_create`),
which can take up to an hour to propagate the first time on Discord's side — for
instant testing during development, swap that call for
`bot.guild_bulk_command_create(cmds, YOUR_TEST_GUILD_ID)` in `Commands.cpp`.

The bot creates `fishbot.sqlite3` next to the binary on first run — back that file up
to preserve player progress.

For running this long-term on a VPS (systemd service, firewall, backups, updates),
see **[DEPLOY.md](./DEPLOY.md)**.

## Notes / things you may want to tune

- Economy numbers (prices, XP curve, cooldowns) live at the top of `Game.cpp` /
  `FishData.cpp` — tweak freely, they're all in one place.
- The reel minigame stores sessions in memory (`ReelSessionManager`), so an in-progress
  cast is lost if the bot restarts — acceptable for a casual game, but flag it if you
  need that to survive restarts.
- `/trade` is a minimal but functional 1-fish-inventory-at-a-time offer system; there's
  no UI confirmation embed showing both offers side-by-side yet — a nice thing to add
  is an embed in `/trade confirm`'s reply summarizing both sides before executing.
- Every admin action (boss channel, boss summon, tournaments, seasonal events) is CLI-only via
  `fishadmin` — there are no admin slash commands, so Discord server admins can't run them either.
  Tournaments/events/summons are queued in the `admin_queue` table and applied by the running bot within ~5s.
- World Boss sessions live in memory (`BossSessionManager`), same caveat as the reel
  minigame — an in-progress fight is lost on restart. Since it's already saved who dealt
  how much damage right up until that point, the only real loss is the fight itself, not
  economy state.
- `/crew` currently has no cap on member count and no invite/approval flow — anyone can
  join any crew by name. If you want it locked down, the natural additions are an
  invite-only flag on `crews` and a `/crew invite` + `/crew accept` pair.
