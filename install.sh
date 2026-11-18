#!/usr/bin/env bash
# Package this checkout as nautilus-omarchy and install it in place of the
# stock nautilus package. Undo with ./uninstall.sh.
#
# SUDO overrides how pacman is elevated, e.g. SUDO="sudo -A".
set -euo pipefail

cd "$(dirname "$(realpath "$0")")/packaging"

makepkg --force --cleanbuild --syncdeps --noconfirm
pkg=$(makepkg --packagelist | head -n1)
# makepkg writes the computed version into PKGBUILD; keep the tree clean.
git checkout --quiet -- PKGBUILD

# --ask=4 answers yes to "remove the conflicting nautilus package?".
${SUDO:-sudo} pacman -U --noconfirm --ask=4 "$pkg"

# A running instance keeps the old code until it is restarted.
if pgrep -x nautilus >/dev/null; then
    echo "Restart Files to load the new build: nautilus -q"
fi
