#!/usr/bin/env bash
# Upgrade an existing FishBot install to this version without touching the database or .env.
#
#   bash update.sh [install_dir]        (default: ~/fishbot)
#
# Backs up the database, copies the new source over, rebuilds and restarts the service.
set -euo pipefail

NEW_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET="${1:-$HOME/fishbot}"

say() { printf '\033[0;32m==>\033[0m %s\n' "$*"; }
die() { printf '\033[0;31m==>\033[0m %s\n' "$*" >&2; exit 1; }

[[ -d "$TARGET" ]] || die "No install at $TARGET. Pass the folder as an argument, or run install.sh for a fresh setup."

if [[ -f "$TARGET/fishbot.sqlite3" ]]; then
    mkdir -p "$HOME/backups"
    stamp="$(date +%F-%H%M%S)"
    if command -v sqlite3 >/dev/null 2>&1; then
        sqlite3 "$TARGET/fishbot.sqlite3" ".backup '$HOME/backups/fishbot-$stamp.sqlite3'"
    else
        cp "$TARGET"/fishbot.sqlite3* "$HOME/backups/" 2>/dev/null || true
    fi
    say "Database backed up to ~/backups"
fi

if [[ "$NEW_SRC" != "$(cd "$TARGET" && pwd)" ]]; then
    say "Copying new source into $TARGET"
    if command -v rsync >/dev/null 2>&1; then
        rsync -a --exclude fishbot.sqlite3 --exclude 'fishbot.sqlite3-*' --exclude .env --exclude build --exclude gifs.txt "$NEW_SRC"/ "$TARGET"/
    else
        (cd "$NEW_SRC" && tar --exclude=build --exclude=.env --exclude='fishbot.sqlite3*' --exclude=gifs.txt -cf - .) | (cd "$TARGET" && tar -xf -)
    fi
    # keep an edited gifs.txt, but give first-time upgraders one
    [[ -f "$TARGET/gifs.txt" ]] || cp "$NEW_SRC/gifs.txt" "$TARGET/gifs.txt"
fi

cd "$TARGET"

if [[ -f .env ]] && ! grep -q '^#\?GIPHY_API_KEY' .env; then
    printf '\n# Optional: live reaction gifs from GIPHY (free key at developers.giphy.com)\n#GIPHY_API_KEY=\n#GIF_CHANCE=100\n' >> .env
    say "Added GIPHY_API_KEY placeholder to .env"
fi

say "Building"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)"

if systemctl list-unit-files 2>/dev/null | grep -q '^fishbot.service'; then
    sudo systemctl restart fishbot
    say "Restarted. Follow the log with: journalctl -u fishbot -f"
else
    say "Built. Start it with: set -a; . ./.env; set +a; ./build/fishbot"
fi
