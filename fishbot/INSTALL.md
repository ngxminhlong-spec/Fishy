# FishBot — Full Fresh VPS Installation Guide
### (supports both **Git** and **Local Upload** methods)

Target: a brand-new Ubuntu 22.04/24.04 (or Debian equivalent) VPS — DigitalOcean,
Hetzner, Linode, Contabo, etc. 1 vCPU / 1GB RAM is enough; this bot is just SQLite +
a Discord gateway connection, no web server.

Everywhere the two deployment paths differ, you'll see:

- 🅰️ **GIT METHOD** — you keep the code in a git repo (GitHub/GitLab/private) and
  `git clone`/`git pull` it onto the box. Best if you'll keep editing the bot.
- 🅱️ **LOCAL UPLOAD METHOD** — you `scp` the zip you already have straight to the
  box and unzip it. Fastest for a one-off deploy, no git hosting needed.

Everything else (build, service, token, etc.) is identical for both — do those
steps once, regardless of which path you picked.

---

## 1. Log in and create a non-root user

```bash
ssh root@YOUR_SERVER_IP
```

Don't run a Discord bot as root. Make a dedicated user:

```bash
adduser fishbot
usermod -aG sudo fishbot
su - fishbot
cd ~
```

All remaining steps assume you're logged in as `fishbot`, in `/home/fishbot`.

---

## 2. Update the box and install build dependencies

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake git libsqlite3-dev pkg-config unzip curl
```

(`git` is installed either way — it's harmless for the local-upload method and
required for the git method.)

---

## 3. Install DPP (the Discord library the bot is built on)

Check if your distro's repo already has a recent `libdpp-dev` (0.9+):

```bash
apt-cache search libdpp-dev
```

**If it's there:**

```bash
sudo apt install -y libdpp-dev
```

**If not** (common on older Ubuntu/Debian), build it from source — it's a normal
CMake project, takes a few minutes:

```bash
cd ~
git clone https://github.com/brainboxdotcc/DPP.git
cd DPP
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
cd ~
```

---

## 4. Get the bot's source onto the box

Pick **one** of the two methods below.

### 🅰️ GIT METHOD

If your code isn't in a repo yet, push it from your **local machine** first:

```bash
# on your local machine, inside the unzipped fishbot/ folder
git init
git add .
git commit -m "Initial commit"
git remote add origin git@github.com:YOUR_USERNAME/fishbot.git
git push -u origin main
```

Use a **private** repo — the source has no secrets in it (the token lives in
`.env`, which you'll create on the server and should never commit), but there's
no reason to make it public.

Then, back on the **VPS**:

```bash
cd ~
git clone git@github.com:YOUR_USERNAME/fishbot.git
# or, for a public repo / HTTPS: git clone https://github.com/YOUR_USERNAME/fishbot.git
cd fishbot
```

If you used an SSH URL, you'll need a deploy key set up on the VPS first:

```bash
ssh-keygen -t ed25519 -C "fishbot-vps" -f ~/.ssh/id_ed25519 -N ""
cat ~/.ssh/id_ed25519.pub
# paste this into GitHub → repo → Settings → Deploy keys (read access is enough)
```

**Future updates** are then just:

```bash
cd ~/fishbot
git pull
```

### 🅱️ LOCAL UPLOAD METHOD

From your **local** machine, where the zip already sits:

```bash
scp fishbot_3_updated.zip fishbot@YOUR_SERVER_IP:~
```

Back on the **VPS**:

```bash
cd ~
unzip fishbot_3_updated.zip
cd fishbot
```

**Future updates** are then: re-run the same `scp`, then on the VPS unzip over
the old folder (or delete and re-unzip) and rebuild — see step 11.

---

## 5. Build it

Same for both methods, from inside the `fishbot/` project folder:

```bash
cd ~/fishbot
cmake -B build
cmake --build build -j$(nproc)
```

If `cmake -B build` can't find DPP and you built it from source in step 3, add:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build -j$(nproc)
```

You should end up with `build/fishbot` (the bot) and `build/fishadmin` (the
admin CLI).

---

## 6. Create the Discord bot application + token

1. https://discord.com/developers/applications → **New Application**.
2. **Bot** tab → **Add Bot** → **Reset Token** → copy it now (shown once only).
3. **Privileged Gateway Intents**: leave all three off — this bot only uses
   slash commands and button clicks, no message content/presence/members needed.
4. **OAuth2 → URL Generator**: scopes `bot` + `applications.commands`; bot
   permissions at minimum **Send Messages**, **Embed Links**, **Use Slash
   Commands**, **Read Message History**. Open the generated URL and invite the
   bot to your server.

---

## 7. Store the token (and optional server allowlist) safely

```bash
cd ~/fishbot
cat > .env << 'EOF'
BOT_TOKEN=paste-your-token-here
EOF
chmod 600 .env
```

`.env` holds secrets — if you're on the git method, make sure it's gitignored
(add `.env` to `.gitignore`) so it never gets pushed.

**Optional — lock the bot to specific servers only:**

Get each server's ID (Discord → User Settings → Advanced → enable Developer
Mode → right-click server icon → **Copy Server ID**), then:

```bash
cat >> .env << 'EOF'
ALLOWED_GUILD_IDS=111111111111111111,222222222222222222
EOF
```

With this set, the bot checks every server it's in at startup and leaves
anything not on the list, and does the same check the instant it's invited
anywhere new — clearing that server's boss-channel setting and any live World
Boss fight on the way out. Leave it unset to allow any server.

---

## 8. Run it as a systemd service (survives reboots/crashes)

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
journalctl -u fishbot -f      # live logs, Ctrl+C to stop watching
```

You should see DPP's log lines and finally `Fishing bot ready as
YourBotName#1234`. Global slash commands can take up to ~1 hour to appear the
first time — for instant testing, temporarily change `register_commands()` in
`src/Commands.cpp` to `bot.guild_bulk_command_create(cmds, YOUR_TEST_SERVER_ID)`
instead of `bot.global_bulk_command_create(cmds)`, rebuild, restart.

---

## 9. Firewall

The bot only makes outbound connections to Discord — nothing to open inbound
for it. Still worth locking the box down generally:

```bash
sudo ufw allow OpenSSH
sudo ufw enable
sudo ufw status
```

---

## 10. Back up the database

Everything (coins, inventory, crews, achievements) lives in one file:

```
~/fishbot/fishbot.sqlite3
```

```bash
mkdir -p ~/backups
crontab -e
```

add:

```
0 4 * * * cp /home/fishbot/fishbot/fishbot.sqlite3 /home/fishbot/backups/fishbot-$(date +\%F).sqlite3
```

Prune old backups as you like, or sync off-box with `rsync`/`rclone`.

---

## 11. Updating the bot later

### 🅰️ GIT METHOD

```bash
cd ~/fishbot
git pull
cmake --build build -j$(nproc)
sudo systemctl restart fishbot
```

### 🅱️ LOCAL UPLOAD METHOD

```bash
# on your local machine
scp fishbot_3_updated.zip fishbot@YOUR_SERVER_IP:~

# on the VPS
cd ~
unzip -o fishbot_3_updated.zip -d fishbot_new
rsync -a --exclude 'fishbot.sqlite3' --exclude '.env' --exclude 'build' fishbot_new/fishbot/ fishbot/
rm -rf fishbot_new
cd ~/fishbot
cmake --build build -j$(nproc)
sudo systemctl restart fishbot
```

(The `rsync --exclude` flags stop the update from clobbering your live
database, secrets, or build cache — swap in a plain `unzip -o … cd fishbot` if
you're comfortable overwriting everything except those files by hand instead.)

Either way, restarting drops any **in-progress** reel casts or World Boss
fights (in-memory by design) but never touches the database.

---

## 12. Admin CLI on the VPS

```bash
cd ~/fishbot
./build/fishadmin give coin <discord_user_id> 5000
./build/fishadmin info <discord_user_id>
```

Safe to run while the bot is live — SQLite handles concurrent access. Get a
player's ID via Discord Developer Mode → right-click their name → **Copy User
ID**.

---

## 13. World Boss channel setup (one-time, per server)

Once the bot's online and in your server, as a server admin, in the channel
you want boss fights to appear in:

```
/boss setchannel
```

Stored in the database — survives restarts. From then on there's roughly a
1-in-4 chance every ~2 hours a World Boss spawns there automatically; admins
can force one immediately with `/boss summon`.

---

## Quick troubleshooting

| Symptom | Fix |
|---|---|
| `DPP library not found` during `cmake -B build` | Add `-DCMAKE_PREFIX_PATH=/usr/local` if built from source, or confirm `sudo apt install libdpp-dev` actually succeeded. |
| Bot shows offline in Discord | `journalctl -u fishbot -f` — usually a bad/expired token in `.env`, or `EnvironmentFile=` path mismatch in the service file. |
| Slash commands don't show up | Global commands take up to ~1 hour on first registration; use guild-scoped registration (step 8) while testing. |
| Service won't start / instantly restarts | Run directly to see the real error: `cd ~/fishbot && BOT_TOKEN=xxx ./build/fishbot` |
| `git clone` asks for a password / hangs (git method) | You're using an SSH URL without a deploy key set up — see step 4A, or switch to the HTTPS clone URL. |
| Update overwrote my database (local upload method) | Restore from your latest `~/backups/` copy; use the `rsync --exclude` version in step 11 going forward. |
