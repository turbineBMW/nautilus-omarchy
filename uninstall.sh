#!/usr/bin/env bash
# Put Arch's stock nautilus back in place of nautilus-omarchy. The fork's
# settings stay in dconf (org.gnome.nautilus.preferences) and are ignored by
# the stock build.
#
# SUDO overrides how pacman is elevated, e.g. SUDO="sudo -A".
set -euo pipefail

# --ask=4 answers yes to "remove the conflicting nautilus-omarchy package?".
${SUDO:-sudo} pacman -S --noconfirm --ask=4 nautilus
