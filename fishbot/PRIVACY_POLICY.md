# FishBot — Privacy Policy

**Last updated:** 2026-10-05

> **Operator note (delete before publishing):** this is a good-faith template, not legal advice.
> Replace the `[bracketed]` placeholders and have it reviewed if you operate at scale or serve users in
> regions with specific privacy laws (GDPR/UK GDPR, CCPA, Vietnam's PDPD, China's PIPL, etc.).
> Host it at a public URL and paste the link into the Discord Developer Portal → *General Information* →
> **Privacy Policy URL**. Set `PRIVACY_URL` in `.env` so `/privacy` links to it as well.
> This text matches what the code actually stores today — if you add features that store more, update it.

This policy explains what **FishBot** (the "Bot"), operated by **[Operator name]** ("we"), stores about you
and why.

## 1. What we store
Everything is keyed by your **Discord user ID** (a numeric identifier — *not* your username, display name,
avatar or email):

| Data | Why |
|---|---|
| Game progress: coins, rod/bait/boat/pet, location, XP/level, daily streak, cast cooldown | Run the game |
| Inventory and aquarium fish, fishdex discoveries, achievements, quest progress, enchants | Run the game |
| Crew membership, and crew names/banks you create (crew names are text you type) | Crews feature |
| Active trades, auction listings, lottery tickets, tournament score | Trading, auctions, lottery, tournaments |
| Language preference (`en`, `vi` or `zh`) — only if you chose one with `/language` | Show messages in your language |

For each **Discord server** where an administrator configured a boss channel, we store the server ID and
that channel ID. Live boss fights are kept in memory only and are lost on restart.

## 2. What we do **not** store
Message content, direct messages, usernames, display names, avatars, email addresses, IP addresses,
payment information, or anything from channels the Bot isn't used in. The Bot reacts only to slash commands
and button clicks and does not request Discord's privileged message-content, members or presence intents.

## 3. How we use it
Only to operate the Bot: saving progress, leaderboards, trades, auctions, lotteries, bosses, tournaments, and
displaying text in your language. We do not use it for advertising or profiling.

## 4. Who can see it
- **Other players** in a server can see your name/mention where the game displays it — leaderboards, trades,
  auctions, crews, boss results. Crew names are visible to everyone who uses `/crew`.
- **Nobody else:** we do not sell your data or share it with third parties. It is stored on the Operator's
  server and necessarily passes through **Discord**, which every Discord bot relies on
  ([Discord's Privacy Policy](https://discord.com/privacy) applies to that part).
- The Operator may be required to disclose data if compelled by law.

## 5. `/stats` and logs
The `/stats` command shows technical information about the Bot's host (CPU, memory, database size, row
counts). It contains no personal data. The host may keep short-lived technical logs (errors, servers the
Bot joins or leaves) for troubleshooting.

## 6. Retention and deletion
We keep your data while the Bot is in use. **You can ask us to delete everything stored about you** by
contacting us (section 9) and giving your Discord user ID. We erase it from the live database; backups are
overwritten as they rotate out (typically within [30] days). You can also remove your saved language
preference yourself at any time with `/language` → *Auto*.

Removing the Bot from a server does not delete your player data, because progress is tied to you, not to
a server — ask us to erase it if you want it gone.

## 7. Age
You must meet Discord's minimum age to use Discord, and therefore the Bot. We do not knowingly collect
data from anyone below that age; contact us to have it removed if you believe we have.

## 8. Security
We take reasonable steps to protect the data (restricted server access, a token kept out of the code),
but no system is 100% secure.

## 9. Contact
**[email address or support-server invite link]**

## 10. Changes
We may update this policy; the "Last updated" date changes when we do.
