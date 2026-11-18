# Files (Nautilus), with Omarchy theming

A fork of [Nautilus](https://gitlab.gnome.org/GNOME/nautilus) that follows the
[Omarchy](https://omarchy.org/) desktop theme the way Bubo, Rustle and Era do,
and can show the desktop through its sidebar. It is kept as a small stack of
commits on top of an upstream release, and is installed as `nautilus-omarchy`
in place of Arch's `nautilus`.

## What the fork adds

**Theme.** Where an Omarchy theme is present, Files takes its colours, accent
and light/dark mode from `~/.local/state/omarchy/current/theme/colors.toml`,
and follows every `omarchy theme set` live. The palette is resolved exactly as
Bubo resolves it (and with the same fallbacks as `omarchy-theme-color`, so
legacy `color0`..`color15` themes work too).

A theme can ship a `nautilus.css`, either in the theme directory or rendered
from `~/.config/omarchy/themed/nautilus.css.tpl`. It is layered over the
generated colours, so it only needs the libadwaita variables it changes.

**Transparent sidebar.** Off by default. With it on, the sidebar's background
(never its text) is drawn see-through so the desktop shows behind it; the file
view stays solid. When the window is too narrow for the sidebar to sit beside
the files, it overlays them instead and stays opaque. The file chooser portal
window behaves the same way.

How see-through: the *Sidebar Opacity* slider, 0–100%. Until you move it, the
theme decides with `:root { --nautilus-sidebar-opacity: 0.8; }` in its
`nautilus.css` (a number from 0 to 1, not a percentage), and failing that it is
80%. Once you move it, your value wins over every theme; the ↶ button next to
the slider hands the choice back to the theme.

**Preferences → General → Appearance:**

| Row                  | GSettings key (`org.gnome.nautilus.preferences`) | Default |
|----------------------|--------------------------------------------------|---------|
| Follow Omarchy Theme | `follow-omarchy-theme` (hidden outside Omarchy)  | on      |
| Transparent Sidebar  | `transparent-sidebar`                            | off     |
| Sidebar Opacity      | `sidebar-opacity` (0–1)                          | 0.8     |

```sh
gsettings set org.gnome.nautilus.preferences transparent-sidebar true
gsettings set org.gnome.nautilus.preferences sidebar-opacity 0.7
gsettings reset org.gnome.nautilus.preferences sidebar-opacity   # back to the theme's
```

## Branches

| Branch    | Contents                                                          |
|-----------|-------------------------------------------------------------------|
| `omarchy` | An upstream release tag (now `51.1`) + the fork's commits. Build from here. |

Upstream's own branches and tags come from the `upstream` remote
(gitlab.gnome.org/GNOME/nautilus). The checkout is a blob-less partial clone,
so old upstream file contents download on demand.

## Install, update, remove

```sh
./install.sh                 # makepkg in packaging/, then pacman -U (replaces nautilus)
nautilus -q                  # restart Files to load it
./uninstall.sh               # put Arch's nautilus back
```

The package `provides` `nautilus`, so `nautilus-python` and the rest stay
satisfied, and Arch's `libnautilus-extension` stays installed: the fork does not
touch the extension library. Because `nautilus-omarchy` conflicts with
`nautilus`, `pacman -Syu` will not pull the stock package back in, and it also
will not update this one. When Arch moves to a new Nautilus release:

```sh
scripts/sync-upstream.sh          # newest release in the current series (51.x)
scripts/sync-upstream.sh 52.0     # move to the next GNOME release
./install.sh
```

The script rebases `omarchy` onto the tag (with `git rerere` on, and the old
branch saved as `omarchy-before-sync`), rebuilds, and runs the fork's tests. If
the rebase conflicts, resolve the files, `git add` them,
`git rebase --continue`, and run it again. Move to a new major release only
when Arch ships the matching `gtk4`, `libadwaita` and `libnautilus-extension`.

## Where the fork touches upstream files

Everything substantial is in new files, which cannot conflict:

- `src/omarchy/nautilus-omarchy-palette.c`: colors.toml → libadwaita CSS, and
  the `--nautilus-sidebar-opacity` parser. A C port of Bubo's
  `src/omarchy/palette.rs` (itself from Rustle's `rustle-core`); keep them in
  step. Its output was checked byte for byte against Bubo's for every theme on
  this machine.
- `src/omarchy/nautilus-omarchy-theme.c`: CSS providers, light/dark forcing,
  the theme directory monitors, per-window transparency, and the Appearance
  rows of Preferences.
- `test/displayless/test-omarchy-palette.c`: tests for the palette and parser.
- `packaging/`, `install.sh`, `uninstall.sh`, `scripts/sync-upstream.sh`, this file.

The hooks into upstream files are each marked `fork`:

| File | Hook |
|------|------|
| `data/org.gnome.nautilus.gschema.xml` | the three keys above |
| `src/nautilus-application.c` | include, and `nautilus_omarchy_theme_init ()` after the global preferences in `startup` |
| `src/nautilus-preferences-dialog.c` | include, and `nautilus_omarchy_preferences_setup ()` at the end of `nautilus_preferences_dialog_setup ()` |
| `src/resources/ui/nautilus-preferences-dialog.blp` | the Appearance group |
| `src/meson.build` | the two new sources |
| `test/displayless/meson.build` | the new test |

The window templates are not touched. Transparency finds each window's
`AdwOverlaySplitView` when the window is added to the application, and relies
on libadwaita's `.sidebar-pane` and `.content-pane` nodes under
`overlay-split-view`. If an upstream release restructures the window, check
those first.

## Testing

```sh
meson setup build --prefix=/usr -Dselinux=disabled -Dtests=headless
meson test -C build test-omarchy-palette
GSETTINGS_SCHEMA_DIR=$PWD/build/data build/src/nautilus --new-window
```

Quit any running Files first (`nautilus -q`): it is a single-instance
application, and a second launch only opens a window in the first.
