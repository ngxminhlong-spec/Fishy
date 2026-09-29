#!/usr/bin/env bash
#
# install.sh — interactive fresh-VPS installer for FishBot.
#
# Walks through: build deps -> DPP (apt or from source) -> getting the source
# (git clone or local zip upload) -> build -> .env -> systemd service ->
# optional firewall + backup cron. Safe to re-run; asks before overwriting
# anything that already exists.
#
# Usage:
#   chmod +x install.sh
#   ./install.sh
#
# Can be run two ways:
#   - From inside an already-unzipped project (this script sitting next to
#     CMakeLists.txt / src/) — the source is auto-detected and used as-is.
#   - Standalone (e.g. just this one file, downloaded/copied onto a bare box
#     before the rest of the code exists) — it detects there's no source yet
#     and asks whether you have a git repo to clone; if not, it falls back to
#     asking for a local .zip you've uploaded.
#
# Root: this bot must not run as root. If launched as root, the script does
# NOT ask — it silently creates an isolated 'fishbot' system user/home (its
# own little run environment, kept separate from anything else on the box)
# and re-launches itself inside it automatically.

set -euo pipefail

# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()  { echo -e "${GREEN}==>${NC} $*"; }
warn()  { echo -e "${YELLOW}==>${NC} $*"; }
error() { echo -e "${RED}==>${NC} $*" >&2; }

ask() {
    # ask "Question text" "default"  -> echoes the answer
    local prompt="$1" default="${2:-}" reply
    if [[ -n "$default" ]]; then
        read -r -p "$prompt [$default]: " reply
        echo "${reply:-$default}"
    else
        read -r -p "$prompt: " reply
        echo "$reply"
    fi
}

ask_yn() {
    # ask_yn "Question text" "y|n"(default) -> returns 0 for yes, 1 for no
    local prompt="$1" default="${2:-y}" reply
    while true; do
        read -r -p "$prompt [$default]: " reply
        reply="${reply:-$default}"
        case "$reply" in
            [Yy]*) return 0 ;;
            [Nn]*) return 1 ;;
            *) echo "Please answer y or n." ;;
        esac
    done
}

require_cmd() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# 0. Root detection — auto-create an isolated non-root environment, no asking
# ---------------------------------------------------------------------------
#
# FishBot must not run as root. Rather than prompt, we just do the right
# thing: create a dedicated 'fishbot' system user (its own isolated home dir
# — the "virtual environment" this bot runs inside on the box) and re-launch
# the installer inside it. If this script was standalone (no source next to
# it), we also carry the detected/local source along so the re-launched copy
# doesn't lose track of it.

SCRIPT_PATH="$(readlink -f "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"

if [[ "$(id -u)" -eq 0 ]]; then
    warn "Running as root — creating an isolated 'fishbot' user/environment automatically."
    if ! id -u fishbot >/dev/null 2>&1; then
        adduser --disabled-password --gecos "" fishbot
        usermod -aG sudo fishbot
        info "Created isolated user+home 'fishbot' (this bot's own run environment)."
    else
        info "User 'fishbot' already exists — reusing that environment."
    fi

    # Carry the script itself, and if it's sitting inside a real checkout
    # (i.e. CMakeLists.txt is right next to it), carry that source along too
    # so detection still finds it after we switch users.
    cp "$SCRIPT_PATH" "/home/fishbot/install.sh"
    if [[ -f "$SCRIPT_DIR/CMakeLists.txt" && -d "$SCRIPT_DIR/src" ]]; then
        mkdir -p /home/fishbot/fishbot
        cp -a "$SCRIPT_DIR"/. /home/fishbot/fishbot/
        rm -f /home/fishbot/fishbot/install.sh
        chown -R fishbot:fishbot /home/fishbot/fishbot
    fi
    chown fishbot:fishbot "/home/fishbot/install.sh"
    chmod +x "/home/fishbot/install.sh"

    info "Re-launching inside the 'fishbot' environment..."
    exec su - fishbot -c "/home/fishbot/install.sh"
fi

echo -e "${BOLD}"
echo "==================================================="
echo "        FishBot — Fresh VPS Installer"
echo "==================================================="
echo -e "${NC}"

INSTALL_DIR="${HOME}/fishbot"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

# ---------------------------------------------------------------------------
# 1. Detect whether the source is already here — only ask if it isn't
# ---------------------------------------------------------------------------
#
# SRC_METHOD: "detected" (nothing to fetch, already on disk), "git", or "upload".

has_code() { [[ -f "$1/CMakeLists.txt" && -d "$1/src" ]]; }

SRC_METHOD=""
if [[ "$SCRIPT_DIR" != "$HOME" ]] && has_code "$SCRIPT_DIR"; then
    info "Found the project source right next to this script — using it as-is."
    INSTALL_DIR="$SCRIPT_DIR"
    SRC_METHOD="detected"
elif has_code "$INSTALL_DIR"; then
    info "Found an existing project at $INSTALL_DIR — using it as-is."
    SRC_METHOD="detected"
fi

info "A few questions first, then everything runs unattended."
echo

if [[ -z "$SRC_METHOD" ]]; then
    warn "No code file found (no CMakeLists.txt/src/ next to this script or at $INSTALL_DIR)."
    if ask_yn "Do you have a git repo for this project to clone?" y; then
        SRC_METHOD="git"
        GIT_URL="$(ask "Git repo URL (SSH or HTTPS)" "")"
        while [[ -z "$GIT_URL" ]]; do
            error "A repo URL is required for the git method."
            GIT_URL="$(ask "Git repo URL (SSH or HTTPS)" "")"
        done
        GIT_BRANCH="$(ask "Branch to clone" "main")"
    else
        SRC_METHOD="upload"
        ZIP_PATH="$(ask "Path to the uploaded .zip file" "$HOME/fishbot_full.zip")"
        while [[ ! -f "$ZIP_PATH" ]]; do
            error "File not found: $ZIP_PATH"
            ZIP_PATH="$(ask "Path to the uploaded .zip file (or scp it over now, then retry)" "")"
        done
    fi
fi

echo
echo "How should I install DPP (the Discord library)?"
echo "  1) apt (fast — only works if your distro has a recent enough package)"
echo "  2) Build from source (slower, always works)"
DPP_METHOD="$(ask "Enter 1 or 2" "1")"

echo
BOT_TOKEN="$(ask "Discord bot token (leave blank to paste into .env manually later)" "")"
ALLOWED_GUILD_IDS="$(ask "ALLOWED_GUILD_IDS to lock the bot to (comma-separated, blank = allow any server)" "")"

echo
SETUP_UFW=n; ask_yn "Enable ufw firewall (allow SSH only)?" y && SETUP_UFW=y || true
SETUP_CRON=n; ask_yn "Set up a daily 4am database backup cron job?" y && SETUP_CRON=y || true
SETUP_SERVICE=n; ask_yn "Install + enable the systemd service now?" y && SETUP_SERVICE=y || true

echo
info "Got everything I need. Starting installation..."
echo

# ---------------------------------------------------------------------------
# 2. System packages
# ---------------------------------------------------------------------------

info "Updating apt and installing build dependencies..."
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential cmake git libsqlite3-dev pkg-config unzip curl rsync ufw

# ---------------------------------------------------------------------------
# 3. DPP
# ---------------------------------------------------------------------------

if [[ "$DPP_METHOD" == "1" ]]; then
    info "Trying to install libdpp-dev via apt..."
    if sudo apt install -y libdpp-dev; then
        info "libdpp-dev installed."
    else
        warn "apt install failed or package too old. Falling back to building from source."
        DPP_METHOD="2"
    fi
fi

if [[ "$DPP_METHOD" == "2" ]]; then
    if ldconfig -p | grep -q libdpp; then
        info "DPP already appears to be installed from source — skipping rebuild."
    else
        info "Building DPP from source (this takes a few minutes)..."
        cd "$HOME"
        if [[ -d DPP ]]; then
            warn "~/DPP already exists, reusing it."
        else
            git clone https://github.com/brainboxdotcc/DPP.git
        fi
        cd DPP
        mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j"$(nproc)"
        sudo make install
        sudo ldconfig
        cd "$HOME"
    fi
fi

# ---------------------------------------------------------------------------
# 4. Get the bot source
# ---------------------------------------------------------------------------

case "$SRC_METHOD" in
    detected)
        info "Using the already-present source at $INSTALL_DIR — nothing to fetch."
        ;;
    git)
        info "Cloning source via git..."
        if [[ -d "$INSTALL_DIR/.git" ]]; then
            warn "$INSTALL_DIR already looks like a git checkout — pulling latest instead of re-cloning."
            cd "$INSTALL_DIR"
            git pull
        elif [[ -d "$INSTALL_DIR" ]]; then
            error "$INSTALL_DIR already exists and isn't a git checkout. Move/remove it and re-run, or choose the upload method."
            exit 1
        else
            git clone --branch "$GIT_BRANCH" "$GIT_URL" "$INSTALL_DIR"
        fi
        ;;
    upload)
        info "Unzipping uploaded source..."
        if [[ -d "$INSTALL_DIR" ]]; then
            warn "$INSTALL_DIR already exists. Merging new files in without touching fishbot.sqlite3 / .env / build/."
            TMP_DIR="$(mktemp -d)"
            unzip -q -o "$ZIP_PATH" -d "$TMP_DIR"
            # the zip contains a top-level "fishbot/" folder (as produced by our packaging step)
            SRC_ROOT="$TMP_DIR"
            if [[ -d "$TMP_DIR/fishbot" ]]; then SRC_ROOT="$TMP_DIR/fishbot"; fi
            rsync -a --exclude 'fishbot.sqlite3' --exclude '.env' --exclude 'build' "$SRC_ROOT"/ "$INSTALL_DIR"/
            rm -rf "$TMP_DIR"
        else
            TMP_DIR="$(mktemp -d)"
            unzip -q -o "$ZIP_PATH" -d "$TMP_DIR"
            SRC_ROOT="$TMP_DIR"
            if [[ -d "$TMP_DIR/fishbot" ]]; then SRC_ROOT="$TMP_DIR/fishbot"; fi
            mv "$SRC_ROOT" "$INSTALL_DIR"
            rm -rf "$TMP_DIR"
        fi
        ;;
esac

cd "$INSTALL_DIR"

if ! has_code "$INSTALL_DIR"; then
    error "Still no CMakeLists.txt/src/ found in $INSTALL_DIR after fetching — something went wrong above."
    exit 1
fi

# ---------------------------------------------------------------------------
# 5. .env
# ---------------------------------------------------------------------------

info "Writing .env..."
{
    echo "BOT_TOKEN=${BOT_TOKEN}"
    if [[ -n "$ALLOWED_GUILD_IDS" ]]; then
        echo "ALLOWED_GUILD_IDS=${ALLOWED_GUILD_IDS}"
    fi
} > "$INSTALL_DIR/.env"
chmod 600 "$INSTALL_DIR/.env"

if [[ -z "$BOT_TOKEN" ]]; then
    warn "No token entered — edit $INSTALL_DIR/.env and set BOT_TOKEN before starting the bot."
fi

# ---------------------------------------------------------------------------
# 6. Build
# ---------------------------------------------------------------------------

info "Building the bot (this can take a couple of minutes)..."
CMAKE_EXTRA_ARGS=()
if [[ "$DPP_METHOD" == "2" ]]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=/usr/local")
fi
cmake -B build "${CMAKE_EXTRA_ARGS[@]}"
cmake --build build -j"$(nproc)"

if [[ -x "$INSTALL_DIR/build/fishbot" ]]; then
    info "Build succeeded: $INSTALL_DIR/build/fishbot"
else
    error "Build did not produce build/fishbot — check the output above for errors."
    exit 1
fi

# ---------------------------------------------------------------------------
# 7. systemd service
# ---------------------------------------------------------------------------

if [[ "$SETUP_SERVICE" == "y" ]]; then
    info "Installing systemd service..."
    CURRENT_USER="$(whoami)"
    sudo tee /etc/systemd/system/fishbot.service > /dev/null << EOF
[Unit]
Description=FishBot Discord bot
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${CURRENT_USER}
WorkingDirectory=${INSTALL_DIR}
EnvironmentFile=${INSTALL_DIR}/.env
ExecStart=${INSTALL_DIR}/build/fishbot
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=${INSTALL_DIR}
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
    sudo systemctl daemon-reload

    if [[ -n "$BOT_TOKEN" ]]; then
        sudo systemctl enable --now fishbot
        sleep 2
        sudo systemctl status fishbot --no-pager || true
    else
        sudo systemctl enable fishbot
        warn "Service installed but not started (no token yet). Add BOT_TOKEN to .env, then:"
        warn "  sudo systemctl start fishbot"
    fi
fi

# ---------------------------------------------------------------------------
# 8. Firewall
# ---------------------------------------------------------------------------

if [[ "$SETUP_UFW" == "y" ]]; then
    info "Configuring ufw (allowing SSH only)..."
    sudo ufw allow OpenSSH
    sudo ufw --force enable
    sudo ufw status
fi

# ---------------------------------------------------------------------------
# 9. Backup cron
# ---------------------------------------------------------------------------

if [[ "$SETUP_CRON" == "y" ]]; then
    info "Setting up daily 4am database backup..."
    mkdir -p "$HOME/backups"
    CRON_LINE="0 4 * * * cp ${INSTALL_DIR}/fishbot.sqlite3 ${HOME}/backups/fishbot-\$(date +\%F).sqlite3"
    ( crontab -l 2>/dev/null | grep -v "fishbot.sqlite3" ; echo "$CRON_LINE" ) | crontab -
    info "Cron job installed. Backups will land in $HOME/backups/"
fi

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------

echo
echo -e "${BOLD}${GREEN}==================================================="
echo "  Installation complete."
echo -e "===================================================${NC}"
echo
echo "Project dir:   $INSTALL_DIR"
echo "Binary:        $INSTALL_DIR/build/fishbot"
echo "Admin CLI:     $INSTALL_DIR/build/fishadmin"
echo "Env file:      $INSTALL_DIR/.env"
echo
if [[ -z "$BOT_TOKEN" ]]; then
    echo "NEXT STEP: edit $INSTALL_DIR/.env, set BOT_TOKEN, then:"
    echo "  sudo systemctl start fishbot"
    echo
fi
echo "Useful commands:"
echo "  sudo systemctl status fishbot"
echo "  journalctl -u fishbot -f"
echo "  cd $INSTALL_DIR && ./build/fishadmin info <discord_user_id>"
echo
echo "Once the bot is in your Discord server, run '/boss setchannel' as an"
echo "admin in the channel you want World Boss fights to appear in."
echo
echo "New: seasonal event fish. Kick one off anytime with, e.g.:"
echo "  /event start key:halloween name:\"Spooky Season\" hours:168"
echo "Ships with 'halloween' and 'lunar_new_year' fish already tagged in"
echo "FishData.cpp — add more by setting a fish's event_key there."
echo
