/* nautilus-omarchy-palette.h: the Omarchy palette as libadwaita CSS.
 *
 * Part of the Omarchy fork; see FORK.md. Pure string work, no GTK, so it
 * can be tested without a display.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Resolve a colors.toml into libadwaita colour variables, the same way
 * omarchy-theme-color (and Bubo, Rustle and Skald) resolve it.
 * `light_mode_file` is whether a light.mode file sits beside colors.toml.
 * `light` receives the theme's mode. Returns newly allocated CSS. */
char *nautilus_omarchy_palette_css (const char *colors_toml,
                                    gboolean    light_mode_file,
                                    gboolean   *light);

G_END_DECLS
