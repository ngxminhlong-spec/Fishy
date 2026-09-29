# Deploying FishBot to a VPS

This walks through getting the bot running 24/7 on a fresh Ubuntu/Debian VPS
(DigitalOcean, Hetzner, Linode, a cheap Contabo box — any of them work the same way).
Assumes Ubuntu 22.04/24.04; note Debian equivalents where they differ.

## 1. Get a box and log in

Any $4-6/mo VPS with 1 vCPU / 1GB RAM is plenty — this bot is lightweight (SQLite,
no web server, low traffic). Once it's provisioned:

```bash
ssh root@YOUR_SERVER_IP
```

First, make a non-root user to run things as (don't run a Discord bot as root):

```bash
adduser fishbot
usermod -aG sudo fishbot
su - fishbot
```

## 2. Install build dependencies

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake git libsqlite3-dev pkg-config unzip
```

## 3. Install DPP

Check first whether your distro's package repo has a recent `libdpp-dev`:

```bash
apt-cache search libdpp-dev
```

If it's there and reasonably current (0.9+), just:

```bash
sudo apt install -y libdpp-dev
```

If not (common on older Ubuntu/Debian), build DPP from source — it's a normal CMake
project and takes a few minutes:

```bash
cd ~
git clone https://github.com/brainboxdotcc/DPP.git
cd DPP
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

## 4. Get the bot's source onto the box

Simplest path: upload the zip you already have and unzip it. From your **local**
machine:

```bash
scp fishbot.zip fishbot@YOUR_SERVER_IP:~
```

Back on the VPS:

```bash
cd ~
unzip fishbot.zip
cd fishbot
```

(If you'd rather track changes in git, push this folder to a private GitHub repo and
`git clone` it instead — makes future updates a `git pull` away.)

## 5. Build it

```bash
cmake -B build
cmake --build build -j$(nproc)
```

If `cmake -B build` fails to find DPP, and you built it from source in step 3, add:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/usr/local
```

You should end up with a `build/fishbot` binary.

## 6. Create your bot application + get a token

1. Go to https://discord.com/developers/applications → **New Application**.
2. Under **Bot**, click **Add Bot**, then **Reset Token** and copy it somewhere safe
   (you'll only see it once).
3. Under **Bot** → **Privileged Gateway Intents**, you don't need any of the
   privileged ones (message content, presence, members) for this bot — slash commands
   and button clicks don't require them.
4. Under **OAuth2 → URL Generator**, check scopes `bot` and `applications.commands`,
   and under bot permissions check at least: **Send Messages**, **Embed Links**,
   **Use Slash Commands**, **Read Message History**. Copy the generated URL, open it,
   and invite the bot to your server.

## 7. Store the token safely

Don't put the token in the code or in a world-readable file. Put it in an env file
only your user can read:

```bash
cd ~/fishbot
cat > .env << 'EOF'
BOT_TOKEN=paste-your-token-here
EOF
chmod 600 .env
```

### Optional: lock the bot to specific servers

If you only want this bot running in your own server(s), get each server's ID (in
Discord: User Settings → Advanced → enable Developer Mode, then right-click the
server icon → **Copy Server ID**) and add it to the same `.env` file:

```bash
cat >> .env << 'EOF'
ALLOWED_GUILD_IDS=111111111111111111,222222222222222222
EOF
```

Set this **before** the first run if possible. With it set, the bot checks every
server it's in at startup and leaves anything not on the list, and does the same
check the instant it's invited anywhere new — so even if someone else's server
adds it later, it walks right back out. Leave this variable unset to allow any
server (the default).

## 8. Run it as a systemd service (so it survives reboots/crashes)

```bash
sudo tee /etc/systemd/system/fishbot.service > /dev/null << 'EOF'
[Unit]
Description=FishBot Discord bot
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=fishbot
WorkingDirectory=/home/fishbot/fishbot
EnvironmentFile=/home/fishbot/fishbot/.env
ExecStart=/home/fishbot/fishbot/build/fishbot
Restart=on-failure
RestartSec=5
# Hardening (optional but recommended)
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=/home/fishbot/fishbot
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now fishbot
```

Check it's alive:

```bash
sudo systemctl status fishbot
journalctl -u fishbot -f      # live logs; Ctrl+C to stop watching
```

You should see DPP's log lines and finally something like
`Fishing bot ready as YourBotName#1234`. Global slash commands can take up to an hour
to show up the very first time — if you want them instantly for testing, temporarily
edit `register_commands()` in `src/Commands.cpp` to call
`bot.guild_bulk_command_create(cmds, YOUR_TEST_SERVER_ID)` instead of
`bot.global_bulk_command_create(cmds)`, rebuild, and restart the service.

## 9. Firewall

This bot makes only outbound connections to Discord (gateway + REST) — it doesn't
listen on any inbound port, so you don't need to open anything for it. Still worth
locking the box down in general:

```bash
sudo ufw allow OpenSSH
sudo ufw enable
sudo ufw status
```

## 10. Back up the database

All player progress lives in one file:

```bash
~/fishbot/fishbot.sqlite3
```

Set up a simple daily cron backup:

```bash
crontab -e
```

add:

```
0 4 * * * cp /home/fishbot/fishbot/fishbot.sqlite3 /home/fishbot/backups/fishbot-$(date +\%F).sqlite3
```

(make the `~/backups` dir first: `mkdir -p ~/backups`). Prune old backups however you
like, or sync them off-box with `rsync`/`rclone` for real safety.

## 11. Updating the bot later

```bash
cd ~/fishbot
# pull new code in (git pull, or scp + unzip a fresh copy over the old source files)
cmake --build build -j$(nproc)
sudo systemctl restart fishbot
```

Restarting mid-game drops any **in-progress** reel casts or World Boss fights (those
live in memory, by design — see the README's "Notes" section), but nothing in the
database (coins, inventory, crews, achievements) is affected.

## 12. Admin CLI on the VPS

`cmake --build build -j$(nproc)` also produces `build/fishadmin` right next to the bot
binary — it's the same tool described in the README, just running on the server. SSH
in and use it directly against the live database:

```bash
cd ~/fishbot
./build/fishadmin give coin <discord_user_id> 5000
./build/fishadmin info <discord_user_id>
```

It's safe to run this while the bot is live — SQLite handles the concurrent access.
To get a player's Discord user ID, have them enable Developer Mode in Discord
(User Settings → Advanced) and right-click their name → **Copy User ID**.

## 13. World Boss channel setup (one-time, per server)
Once the bot's online and in your server, in the channel you want boss fights to
appear in, run:

```
/boss setchannel
```

as a server admin. That's stored in the database, so it survives restarts — from then
on, roughly every 2 hours there's a 1-in-4 chance a World Boss spawns there
automatically, and admins can always force one immediately with `/boss summon`.

## Quick troubleshooting

- **"DPP library not found" during `cmake -B build`** — DPP isn't installed where
  CMake can see it. Re-run with `-DCMAKE_PREFIX_PATH=/usr/local` if you built from
  source, or double check `sudo apt install libdpp-dev` actually succeeded.
- **Bot shows offline in Discord** — check `journalctl -u fishbot -f` for errors;
  most common cause is a bad/expired token in `.env`, or the `.env` file not being
  picked up (confirm the path in `EnvironmentFile=` matches exactly).
- **Slash commands don't show up** — global commands take up to ~1 hour on first
  registration; use guild-scoped registration (step 8) for instant iteration while
  testing.
- **Service won't start / instantly restarts** — run the binary directly to see the
  real error: `cd ~/fishbot && BOT_TOKEN=xxx ./build/fishbot`.
