/* fork: tests for src/omarchy/nautilus-omarchy-palette.c.
 * The palette itself is checked byte for byte against Bubo's Rust
 * implementation; see FORK.md. These pin the behaviour that matters most.
 * (The sidebar's opacity is a setting, not part of the palette.) */
#include <glib.h>
#include <string.h>

#include "omarchy/nautilus-omarchy-palette.h"

static void
test_palette_mode_and_colours (void)
{
    gboolean light = TRUE;
    g_autofree char *css = nautilus_omarchy_palette_css ("mode = \"dark\"\n"
                                                         "accent = \"#7aa2f7\"\n"
                                                         "background = \"#1a1b26\"\n"
                                                         "foreground = \"#c0caf5\"\n",
                                                         FALSE, &light);

    g_assert_false (light);
    g_assert_nonnull (strstr (css, "--accent-bg-color: #7aa2f7;"));
    g_assert_nonnull (strstr (css, "--accent-fg-color: #000000;"));
    g_assert_nonnull (strstr (css, "--window-bg-color: #1a1b26;"));
    /* dark_background is derived 25% toward black. */
    g_assert_nonnull (strstr (css, "--sidebar-bg-color: #14141d;"));
}

static void
test_palette_legacy_and_light_file (void)
{
    gboolean light = FALSE;
    g_autofree char *css = nautilus_omarchy_palette_css ("color0 = \"#eff1f5\"\n"
                                                         "color7 = \"#4c4f69\"\n",
                                                         TRUE, &light);

    g_assert_true (light);
    g_assert_nonnull (strstr (css, "--window-bg-color: #eff1f5;"));
    g_assert_nonnull (strstr (css, "--window-fg-color: #4c4f69;"));
}

static void
test_palette_rejects_injection (void)
{
    g_autofree char *css = nautilus_omarchy_palette_css ("background = \"#000000; } * { color: red\"\n"
                                                         "accent = \"#123456 }\"\n"
                                                         "foreground = \"#ffffff\"\n",
                                                         FALSE, NULL);

    g_assert_null (strstr (css, "* {"));
    g_assert_nonnull (strstr (css, "--window-bg-color: #1d1d20;"));
    g_assert_null (strstr (css, "#123456"));
    g_assert_nonnull (strstr (css, "--accent-bg-color: blue;"));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/omarchy/palette/mode-and-colours", test_palette_mode_and_colours);
    g_test_add_func ("/omarchy/palette/legacy-and-light-file", test_palette_legacy_and_light_file);
    g_test_add_func ("/omarchy/palette/rejects-injection", test_palette_rejects_injection);

    return g_test_run ();
}
