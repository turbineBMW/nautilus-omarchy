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

/* The custom property a theme's nautilus.css may set, a number from 0 to 1.
 * It only says how see-through the sidebar is once Preferences turns the
 * transparent sidebar on. */
#define NAUTILUS_OMARCHY_SIDEBAR_OPACITY "--nautilus-sidebar-opacity"

/* Resolve a colors.toml into libadwaita colour variables, the same way
 * omarchy-theme-color (and Bubo, Rustle and Skald) resolve it.
 * `light_mode_file` is whether a light.mode file sits beside colors.toml.
 * `light` receives the theme's mode. Returns newly allocated CSS. */
char *nautilus_omarchy_palette_css (const char *colors_toml,
                                    gboolean    light_mode_file,
                                    gboolean   *light);

/* The last value NAUTILUS_OMARCHY_SIDEBAR_OPACITY is given in `css`,
 * clamped to 0..1. Comments are skipped. FALSE when it is never set, and
 * also when its last value is not a plain number: GTK's alpha() rejects a
 * percentage, so nothing is known about the opacity then. */
gboolean nautilus_omarchy_sidebar_opacity_in (const char *css,
                                              double     *opacity);

G_END_DECLS
