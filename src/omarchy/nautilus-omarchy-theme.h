/* nautilus-omarchy-theme.h: follow the active Omarchy theme, and the
 * optional see-through sidebar.
 *
 * Part of the Omarchy fork; see FORK.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Keys in org.gnome.nautilus.preferences. */
#define NAUTILUS_PREFERENCES_FOLLOW_OMARCHY_THEME "follow-omarchy-theme"
#define NAUTILUS_PREFERENCES_TRANSPARENT_SIDEBAR "transparent-sidebar"
#define NAUTILUS_PREFERENCES_SIDEBAR_OPACITY "sidebar-opacity"

/* Install once, during application startup, after the global preferences. */
void nautilus_omarchy_theme_init (GtkApplication *application);

/* Wire the Appearance group of the preferences dialog. */
void nautilus_omarchy_preferences_setup (GtkBuilder *builder);

G_END_DECLS
