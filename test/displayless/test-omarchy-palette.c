/* fork: tests for src/omarchy/nautilus-omarchy-palette.c.
 * The palette itself is checked byte for byte against Bubo's Rust
 * implementation; see FORK.md. These pin the behaviour that matters most. */
#include <glib.h>
#include <string.h>

#include "omarchy/nautilus-omarchy-palette.h"

static void
assert_opacity (const char *css,
                double      expected)
{
    double opacity = -1.0;

    g_assert_true (nautilus_omarchy_sidebar_opacity_in (css, &opacity));
    g_assert_cmpfloat_with_epsilon (opacity, expected, 1e-9);
}

static void
test_opacity_last_value_wins (void)
{
    assert_opacity ("/* --nautilus-sidebar-opacity: 0.1; */\n"
                    ":root {\n  --nautilus-sidebar-opacity: 0.8;\n}\n"
                    "window { --nautilus-sidebar-opacity:0.6 }\n", 0.6);
    assert_opacity (":root { --nautilus-sidebar-opacity: 1; }", 1.0);
}

static void
test_opacity_clamped (void)
{
    assert_opacity (":root { --nautilus-sidebar-opacity: 2; }", 1.0);
    assert_opacity (":root { --nautilus-sidebar-opacity: -0.5; }", 0.0);
}

static void
test_opacity_garbage_ignored (void)
{
    const char *cases[] =
    {
        "",
        ":root { --nautilus-sidebar-opacity: lots; }",
        ":root { --nautilus-sidebar-opacity: NaN; }",
        ":root { --nautilus-sidebar-opacity: inf; }",
        /* GTK's alpha() rejects a percentage. */
        ":root { --nautilus-sidebar-opacity: 75%; }",
        ":root { --nautilus-sidebar-opacity-x: 0.5; }",
        ":root { --x--nautilus-sidebar-opacity: 0.5; }",
        ".sidebar-pane { opacity: var(--nautilus-sidebar-opacity); }",
        "/* --nautilus-sidebar-opacity: 0.5",
        /* A later declaration replaces an earlier one, usable or not. */
        ":root { --nautilus-sidebar-opacity: 0.5; }\n"
        ":root { --nautilus-sidebar-opacity: lots; }",
    };

    for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
        g_assert_false (nautilus_omarchy_sidebar_opacity_in (cases[i], NULL));
    }
}

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

    g_test_add_func ("/omarchy/opacity/last-value-wins", test_opacity_last_value_wins);
    g_test_add_func ("/omarchy/opacity/clamped", test_opacity_clamped);
    g_test_add_func ("/omarchy/opacity/garbage-ignored", test_opacity_garbage_ignored);
    g_test_add_func ("/omarchy/palette/mode-and-colours", test_palette_mode_and_colours);
    g_test_add_func ("/omarchy/palette/legacy-and-light-file", test_palette_legacy_and_light_file);
    g_test_add_func ("/omarchy/palette/rejects-injection", test_palette_rejects_injection);

    return g_test_run ();
}
