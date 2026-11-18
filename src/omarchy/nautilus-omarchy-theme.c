/* nautilus-omarchy-theme.c: follow the active Omarchy theme, and the
 * optional see-through sidebar.
 *
 * Part of the Omarchy fork; see FORK.md. Modelled on Bubo's
 * src/omarchy/mod.rs and src/sidebar.rs, with one difference: the sidebar
 * only turns see-through when Preferences says so.
 *
 * Theme. Omarchy keeps the active theme at
 * ~/.local/state/omarchy/current/theme and swaps that directory wholesale on
 * every `omarchy theme set`. Its colors.toml is resolved into libadwaita's
 * colour variables (nautilus-omarchy-palette.c) and loaded into a provider
 * above the app stylesheet, light or dark is forced to the theme's mode, and
 * both the stable parent and the theme directory are watched so a theme
 * change applies live. A theme may ship a nautilus.css (by hand, or rendered
 * from ~/.config/omarchy/themed/nautilus.css.tpl); it is layered after the
 * generated colours, so it only needs the variables it changes.
 *
 * Sidebar. With the transparent-sidebar key on, the sidebar's background is
 * drawn at the sidebar-opacity key's alpha (its text never fades). A faded
 * sidebar over the window's own background would only blend into that
 * colour, so the window's background is cleared too and the content pane is
 * painted again, leaving only the sidebar see-through. Until the user moves
 * the slider, the theme's --nautilus-sidebar-opacity decides the opacity.
 * When the window is narrow the sidebar overlays the files instead of sitting
 * beside them, and stays opaque.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "nautilus-omarchy-theme.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "nautilus-global-preferences.h"
#include "nautilus-omarchy-palette.h"

#define TRANSPARENT_CLASS "nautilus-omarchy-transparent"
#define SPLIT_VIEW_KEY "nautilus-omarchy-split-view"
#define RELOAD_DELAY_MS 120

/* Only the window background is cleared, and only in a window that has the
 * class, which is set while the sidebar sits beside the files. The file
 * chooser paints the view colour on the split view itself, so that is
 * cleared as well; both windows show the view colour behind the files. */
static const char *sidebar_css =
    "window." TRANSPARENT_CLASS ",\n"
    "window." TRANSPARENT_CLASS " overlay-split-view {\n"
    "  background-color: transparent;\n"
    "}\n"
    "window." TRANSPARENT_CLASS " overlay-split-view > .sidebar-pane {\n"
    "  background-color: alpha(var(--sidebar-bg-color), var(" NAUTILUS_OMARCHY_SIDEBAR_OPACITY "));\n"
    "}\n"
    "window." TRANSPARENT_CLASS " overlay-split-view > .sidebar-pane:backdrop {\n"
    "  background-color: alpha(var(--sidebar-backdrop-color), var(" NAUTILUS_OMARCHY_SIDEBAR_OPACITY "));\n"
    "}\n"
    "window." TRANSPARENT_CLASS " overlay-split-view > .content-pane {\n"
    "  background-color: var(--view-bg-color);\n"
    "  color: var(--view-fg-color);\n"
    "}\n";

typedef struct
{
    GtkApplication *application;
    GtkCssProvider *palette;
    GtkCssProvider *sidebar;
    GPtrArray *monitors;
    guint pending;

    /* What the theme's own CSS sets the sidebar's opacity to, if anything. */
    gboolean theme_sets_opacity;
    double theme_opacity;

    /* The preferences dialog's slider while it is open. */
    GtkAdjustment *opacity_adjustment;
    GtkWidget *opacity_reset;
    gboolean updating_adjustment;
} Theme;

static Theme *theme = NULL;

static char *
state_dir (void)
{
    return g_build_filename (g_get_home_dir (), ".local", "state", "omarchy", "current", NULL);
}

static char *
theme_dir (void)
{
    g_autofree char *state = state_dir ();

    return g_build_filename (state, "theme", NULL);
}

static gboolean
omarchy_detected (void)
{
    g_autofree char *dir = theme_dir ();

    return g_file_test (dir, G_FILE_TEST_IS_DIR);
}

/* The active theme's slug, prettified for Preferences: "tokyo-night" is
 * shown as "Tokyo Night". */
static char *
theme_display_name (void)
{
    g_autofree char *state = state_dir ();
    g_autofree char *path = g_build_filename (state, "theme.name", NULL);
    g_autofree char *contents = NULL;
    GString *name;
    gboolean word_start = TRUE;

    if (!g_file_get_contents (path, &contents, NULL, NULL))
    {
        return NULL;
    }
    g_strstrip (contents);
    if (*contents == '\0')
    {
        return NULL;
    }

    name = g_string_new (NULL);
    for (const char *p = contents; *p != '\0'; p++)
    {
        char ch = (*p == '-' || *p == '_') ? ' ' : *p;

        g_string_append_c (name, word_start ? g_ascii_toupper (ch) : ch);
        word_start = ch == ' ';
    }
    return g_string_free (name, FALSE);
}

static double
effective_opacity (void)
{
    g_autoptr (GVariant) user_value = g_settings_get_user_value (nautilus_preferences,
                                                                 NAUTILUS_PREFERENCES_SIDEBAR_OPACITY);

    if (user_value == NULL && theme->theme_sets_opacity)
    {
        return theme->theme_opacity;
    }
    return CLAMP (g_settings_get_double (nautilus_preferences,
                                         NAUTILUS_PREFERENCES_SIDEBAR_OPACITY),
                  0.0, 1.0);
}

static gboolean
sidebar_transparent (void)
{
    return g_settings_get_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_TRANSPARENT_SIDEBAR) &&
           effective_opacity () < 1.0;
}

static void
sync_window (GtkWindow *window)
{
    AdwOverlaySplitView *split_view = g_object_get_data (G_OBJECT (window), SPLIT_VIEW_KEY);

    if (split_view == NULL)
    {
        return;
    }
    if (sidebar_transparent () && !adw_overlay_split_view_get_collapsed (split_view))
    {
        gtk_widget_add_css_class (GTK_WIDGET (window), TRANSPARENT_CLASS);
    }
    else
    {
        gtk_widget_remove_css_class (GTK_WIDGET (window), TRANSPARENT_CLASS);
    }
}

static void
sync_preferences (void)
{
    g_autoptr (GVariant) user_value = NULL;

    if (theme->opacity_adjustment != NULL)
    {
        theme->updating_adjustment = TRUE;
        gtk_adjustment_set_value (theme->opacity_adjustment, effective_opacity ());
        theme->updating_adjustment = FALSE;
    }
    if (theme->opacity_reset != NULL)
    {
        user_value = g_settings_get_user_value (nautilus_preferences,
                                                NAUTILUS_PREFERENCES_SIDEBAR_OPACITY);
        gtk_widget_set_visible (theme->opacity_reset, user_value != NULL);
    }
}

/* Bring the sidebar's opacity and every window in line with the settings
 * and the theme. */
static void
sync_sidebar (void)
{
    char number[G_ASCII_DTOSTR_BUF_SIZE];
    g_autofree char *css = NULL;

    g_ascii_formatd (number, sizeof number, "%.3f", effective_opacity ());
    css = g_strdup_printf (":root { " NAUTILUS_OMARCHY_SIDEBAR_OPACITY ": %s; }\n%s",
                           number, sidebar_css);
    gtk_css_provider_load_from_string (theme->sidebar, css);

    for (GList *l = gtk_application_get_windows (theme->application); l != NULL; l = l->next)
    {
        sync_window (GTK_WINDOW (l->data));
    }
    sync_preferences ();
}

static void
reload (void)
{
    g_autofree char *dir = theme_dir ();
    g_autofree char *colors_path = g_build_filename (dir, "colors.toml", NULL);
    g_autofree char *light_path = g_build_filename (dir, "light.mode", NULL);
    g_autofree char *own_path = g_build_filename (dir, "nautilus.css", NULL);
    g_autofree char *colors = NULL;
    g_autofree char *own = NULL;
    g_autoptr (GString) css = g_string_new (NULL);
    gboolean follow = g_settings_get_boolean (nautilus_preferences,
                                              NAUTILUS_PREFERENCES_FOLLOW_OMARCHY_THEME);
    gboolean loaded = FALSE;
    gboolean light = FALSE;

    if (follow && g_file_get_contents (colors_path, &colors, NULL, NULL))
    {
        g_autofree char *palette = nautilus_omarchy_palette_css (colors,
                                                                 g_file_test (light_path, G_FILE_TEST_EXISTS),
                                                                 &light);

        g_string_append (css, palette);
        /* A theme that speaks to Files directly is layered over the palette
         * rather than replacing it, so a partial file only changes what it
         * names. */
        if (g_file_get_contents (own_path, &own, NULL, NULL))
        {
            g_string_append (css, "/* The theme's own nautilus.css. */\n");
            g_string_append (css, own);
        }
        loaded = TRUE;
    }

    gtk_css_provider_load_from_string (theme->palette, css->str);
    theme->theme_sets_opacity = nautilus_omarchy_sidebar_opacity_in (css->str, &theme->theme_opacity);

    adw_style_manager_set_color_scheme (adw_style_manager_get_default (),
                                        !loaded ? ADW_COLOR_SCHEME_DEFAULT :
                                        light ? ADW_COLOR_SCHEME_FORCE_LIGHT :
                                        ADW_COLOR_SCHEME_FORCE_DARK);
    sync_sidebar ();
}

static void watch (void);

static gboolean
reload_timeout (gpointer user_data)
{
    theme->pending = 0;
    reload ();
    /* The theme directory just watched may have been replaced. */
    watch ();
    return G_SOURCE_REMOVE;
}

static void
on_theme_changed (GFileMonitor      *monitor,
                  GFile             *file,
                  GFile             *other_file,
                  GFileMonitorEvent  event,
                  gpointer           user_data)
{
    /* omarchy writes several files per switch; reload once they settle. */
    g_clear_handle_id (&theme->pending, g_source_remove);
    theme->pending = g_timeout_add (RELOAD_DELAY_MS, reload_timeout, NULL);
}

static void
watch (void)
{
    g_autofree char *state = state_dir ();
    g_autofree char *dir = theme_dir ();
    const char *dirs[] = { state, dir };

    for (guint i = 0; i < theme->monitors->len; i++)
    {
        g_file_monitor_cancel (g_ptr_array_index (theme->monitors, i));
    }
    g_ptr_array_set_size (theme->monitors, 0);

    /* The parent survives omarchy's directory replacement; the theme
     * directory itself reports edits to colors.toml, light.mode or
     * nautilus.css. */
    for (guint i = 0; i < G_N_ELEMENTS (dirs); i++)
    {
        g_autoptr (GFile) file = g_file_new_for_path (dirs[i]);
        g_autoptr (GError) error = NULL;
        GFileMonitor *monitor;

        if (!g_file_test (dirs[i], G_FILE_TEST_IS_DIR))
        {
            continue;
        }
        monitor = g_file_monitor_directory (file, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
        if (monitor == NULL)
        {
            g_message ("Cannot watch %s: %s", dirs[i], error->message);
            continue;
        }
        g_signal_connect (monitor, "changed", G_CALLBACK (on_theme_changed), NULL);
        g_ptr_array_add (theme->monitors, monitor);
    }
}

static void
on_preferences_changed (GSettings  *settings,
                        const char *key,
                        gpointer    user_data)
{
    if (g_str_equal (key, NAUTILUS_PREFERENCES_FOLLOW_OMARCHY_THEME))
    {
        reload ();
    }
    else if (g_str_equal (key, NAUTILUS_PREFERENCES_TRANSPARENT_SIDEBAR) ||
             g_str_equal (key, NAUTILUS_PREFERENCES_SIDEBAR_OPACITY))
    {
        sync_sidebar ();
    }
}

static void
on_parsing_error (GtkCssProvider *provider,
                  GtkCssSection  *section,
                  GError         *error,
                  gpointer        user_data)
{
    g_message ("Omarchy theme CSS, line %zu: %s",
               gtk_css_section_get_start_location (section)->lines + 1,
               error->message);
}

static AdwOverlaySplitView *
find_split_view (GtkWidget *widget)
{
    if (ADW_IS_OVERLAY_SPLIT_VIEW (widget))
    {
        return ADW_OVERLAY_SPLIT_VIEW (widget);
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        AdwOverlaySplitView *found = find_split_view (child);

        if (found != NULL)
        {
            return found;
        }
    }
    return NULL;
}

static void
on_collapsed_changed (AdwOverlaySplitView *split_view,
                      GParamSpec          *pspec,
                      GtkWindow           *window)
{
    sync_window (window);
}

/* Windows with a sidebar (the browser and the file chooser) follow the
 * transparency setting; the rest (properties, dialogs) keep their own
 * background. */
static void
on_window_added (GtkApplication *application,
                 GtkWindow      *window,
                 gpointer        user_data)
{
    AdwOverlaySplitView *split_view = find_split_view (GTK_WIDGET (window));

    if (split_view == NULL || g_object_get_data (G_OBJECT (window), SPLIT_VIEW_KEY) != NULL)
    {
        return;
    }
    g_object_set_data (G_OBJECT (window), SPLIT_VIEW_KEY, split_view);
    g_signal_connect_object (split_view, "notify::collapsed",
                             G_CALLBACK (on_collapsed_changed), window, G_CONNECT_DEFAULT);
    sync_window (window);
}

void
nautilus_omarchy_theme_init (GtkApplication *application)
{
    GdkDisplay *display = gdk_display_get_default ();

    if (theme != NULL || display == NULL)
    {
        return;
    }

    theme = g_new0 (Theme, 1);
    theme->application = application;
    theme->monitors = g_ptr_array_new_with_free_func (g_object_unref);

    /* Above the app's own style.css; the sidebar's opacity above the theme,
     * since the effective value already accounts for what the theme says. */
    theme->palette = gtk_css_provider_new ();
    g_signal_connect (theme->palette, "parsing-error", G_CALLBACK (on_parsing_error), NULL);
    gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (theme->palette),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    theme->sidebar = gtk_css_provider_new ();
    gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (theme->sidebar),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 3);

    g_signal_connect (application, "window-added", G_CALLBACK (on_window_added), NULL);
    g_signal_connect (nautilus_preferences, "changed", G_CALLBACK (on_preferences_changed), NULL);

    reload ();
    watch ();
}

static char *
format_percent (GtkScale *scale,
                double    value,
                gpointer  user_data)
{
    return g_strdup_printf ("%d%%", (int) round (value * 100.0));
}

static void
on_opacity_value_changed (GtkAdjustment *adjustment,
                          gpointer       user_data)
{
    /* Programmatic updates only mirror the effective value; a move by the
     * user takes over from the theme's opacity. */
    if (theme->updating_adjustment)
    {
        return;
    }
    g_settings_set_double (nautilus_preferences, NAUTILUS_PREFERENCES_SIDEBAR_OPACITY,
                           round (gtk_adjustment_get_value (adjustment) * 100.0) / 100.0);
}

static void
on_opacity_reset_clicked (GtkButton *button,
                          gpointer   user_data)
{
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SIDEBAR_OPACITY);
}

void
nautilus_omarchy_preferences_setup (GtkBuilder *builder)
{
    GtkWidget *follow_row = GTK_WIDGET (gtk_builder_get_object (builder, "follow_omarchy_theme_row"));
    GtkWidget *transparent_row = GTK_WIDGET (gtk_builder_get_object (builder, "transparent_sidebar_row"));
    GtkWidget *opacity_row = GTK_WIDGET (gtk_builder_get_object (builder, "sidebar_opacity_row"));
    GtkScale *scale = GTK_SCALE (gtk_builder_get_object (builder, "sidebar_opacity_scale"));
    GtkWidget *reset = GTK_WIDGET (gtk_builder_get_object (builder, "sidebar_opacity_reset_button"));

    g_settings_bind (nautilus_preferences, NAUTILUS_PREFERENCES_TRANSPARENT_SIDEBAR,
                     transparent_row, "active", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (nautilus_preferences, NAUTILUS_PREFERENCES_TRANSPARENT_SIDEBAR,
                     opacity_row, "sensitive", G_SETTINGS_BIND_GET);

    /* Outside Omarchy there is no theme to follow. */
    gtk_widget_set_visible (follow_row, omarchy_detected ());
    if (omarchy_detected ())
    {
        g_autofree char *name = theme_display_name ();

        g_settings_bind (nautilus_preferences, NAUTILUS_PREFERENCES_FOLLOW_OMARCHY_THEME,
                         follow_row, "active", G_SETTINGS_BIND_DEFAULT);
        if (name != NULL)
        {
            g_autofree char *subtitle = g_strdup_printf (_("Current theme: %s"), name);

            adw_action_row_set_subtitle (ADW_ACTION_ROW (follow_row), subtitle);
        }
    }

    gtk_scale_set_format_value_func (scale, format_percent, NULL, NULL);

    if (theme == NULL)
    {
        gtk_widget_set_visible (reset, FALSE);
        return;
    }
    g_set_weak_pointer (&theme->opacity_adjustment, gtk_range_get_adjustment (GTK_RANGE (scale)));
    g_set_weak_pointer (&theme->opacity_reset, reset);
    sync_preferences ();
    g_signal_connect (theme->opacity_adjustment, "value-changed",
                      G_CALLBACK (on_opacity_value_changed), NULL);
    g_signal_connect (reset, "clicked", G_CALLBACK (on_opacity_reset_clicked), NULL);
}
