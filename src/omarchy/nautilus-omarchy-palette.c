/* nautilus-omarchy-palette.c: the Omarchy palette as libadwaita CSS.
 *
 * A C port of Bubo's src/omarchy/palette.rs, itself a port of Rustle's
 * rustle-core omarchy.rs. Keep them in step: a theme must read the same in
 * Files as in the terminal and in its sibling apps.
 *
 * Omarchy keeps the active theme at ~/.local/state/omarchy/current/theme.
 * Its palette lives in colors.toml; this resolves that palette with the
 * cascade omarchy-theme-color uses (legacy themes only define color0 to
 * color15) and renders it as the colour variables libadwaita and every stock
 * widget already read.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "nautilus-omarchy-palette.h"

#include <math.h>
#include <string.h>

typedef GHashTable Colors;

static const char *
lookup (Colors     *colors,
        const char *key)
{
    const char *value = g_hash_table_lookup (colors, key);

    return (value != NULL && *value != '\0') ? value : NULL;
}

static void
set (Colors     *colors,
     const char *key,
     const char *value)
{
    g_hash_table_insert (colors, g_strdup (key), g_strdup (value));
}

static gboolean
key_char_ok (char ch)
{
    return g_ascii_isalnum (ch) || ch == '_' || ch == '-';
}

static gboolean
value_char_ok (char ch)
{
    return g_ascii_isalnum (ch) || (ch != '\0' && strchr ("#(),._+/% -", ch) != NULL);
}

/* `key = "value"` pairs, quotes and inline comments stripped. Keys and values
 * outside the charset omarchy accepts are dropped rather than smuggled into
 * CSS. */
static Colors *
parse (const char *toml)
{
    Colors *colors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_auto (GStrv) lines = g_strsplit (toml, "\n", -1);

    for (char **line = lines; *line != NULL; line++)
    {
        char *eq = strchr (*line, '=');
        g_autoptr (GString) key = g_string_new (NULL);
        g_autofree char *value = NULL;
        const char *quote;
        gboolean ok = TRUE;

        if (eq == NULL)
        {
            continue;
        }

        for (const char *p = *line; p < eq; p++)
        {
            if (*p != '"' && *p != '\'' && *p != ' ' && *p != '\t')
            {
                g_string_append_c (key, *p);
            }
        }
        if (key->len == 0 || key->str[0] == '#')
        {
            continue;
        }

        quote = strpbrk (eq + 1, "\"'");
        if (quote != NULL)
        {
            const char *end = strpbrk (quote + 1, "\"'");

            value = end != NULL ? g_strndup (quote + 1, end - quote - 1) : g_strdup (quote + 1);
            /* The text after an unclosed quote runs to the end of the line. */
            g_strchomp (value);
        }
        else
        {
            value = g_strstrip (g_strdup (eq + 1));
        }

        for (const char *p = key->str; *p != '\0'; p++)
        {
            ok = ok && key_char_ok (*p);
        }
        for (const char *p = value; *p != '\0'; p++)
        {
            ok = ok && value_char_ok (*p);
        }
        if (ok)
        {
            set (colors, key->str, value);
        }
    }

    return colors;
}

/* Fill `key` from the first of `from` that is set, only when the theme left
 * `key` unset. */
static void
alias_any (Colors            *colors,
           const char        *key,
           const char * const *from)
{
    if (lookup (colors, key) != NULL)
    {
        return;
    }
    for (; *from != NULL; from++)
    {
        const char *value = lookup (colors, *from);

        if (value != NULL)
        {
            set (colors, key, value);
            return;
        }
    }
}

#define ALIAS_ANY(colors, key, ...) \
        alias_any ((colors), (key), (const char * const[]) { __VA_ARGS__, NULL })

static void
alias (Colors     *colors,
       const char *key,
       const char *from)
{
    ALIAS_ANY (colors, key, from);
}

/* Overwrite `key` with `from` whenever `from` is set. */
static void
mirror (Colors     *colors,
        const char *key,
        const char *from)
{
    const char *value = lookup (colors, from);

    if (value != NULL)
    {
        set (colors, key, value);
    }
}

static gboolean
rgb (const char *hex,
     guint8     *r,
     guint8     *g,
     guint8     *b)
{
    if (hex == NULL || hex[0] != '#' || strlen (hex) != 7)
    {
        return FALSE;
    }
    for (int i = 1; i < 7; i++)
    {
        if (!g_ascii_isxdigit (hex[i]))
        {
            return FALSE;
        }
    }
    *r = g_ascii_xdigit_value (hex[1]) * 16 + g_ascii_xdigit_value (hex[2]);
    *g = g_ascii_xdigit_value (hex[3]) * 16 + g_ascii_xdigit_value (hex[4]);
    *b = g_ascii_xdigit_value (hex[5]) * 16 + g_ascii_xdigit_value (hex[6]);
    return TRUE;
}

static guint8
blend (guint8 s,
       guint8 e,
       double amount)
{
    double value = s * (1.0 - amount) + e * amount + 0.5;

    return (guint8) CLAMP (value, 0.0, 255.0);
}

/* Blend two hex colours, `amount` of the way from `start` to `end`. A non-hex
 * value (an rgba() a theme wrote by hand) has nothing to blend: NULL. */
static char *
mix (const char *start,
     const char *end,
     double      amount)
{
    guint8 sr, sg, sb, er, eg, eb;

    if (!rgb (start, &sr, &sg, &sb) || !rgb (end, &er, &eg, &eb))
    {
        return NULL;
    }
    amount = CLAMP (amount, 0.0, 1.0);
    return g_strdup_printf ("#%02x%02x%02x",
                            blend (sr, er, amount),
                            blend (sg, eg, amount),
                            blend (sb, eb, amount));
}

/* Fill `key` by blending `base` toward `toward`, when the theme left it unset. */
static void
derive (Colors     *colors,
        const char *key,
        const char *base,
        const char *toward,
        double      amount)
{
    g_autofree char *value = NULL;

    if (lookup (colors, key) != NULL)
    {
        return;
    }
    value = mix (g_hash_table_lookup (colors, base), toward, amount);
    if (value != NULL)
    {
        set (colors, key, value);
    }
}

/* WCAG relative luminance. */
static gboolean
luminance (const char *color,
           double     *out)
{
    guint8 r, g, b;
    double channels[3];

    if (!rgb (color, &r, &g, &b))
    {
        return FALSE;
    }
    channels[0] = r;
    channels[1] = g;
    channels[2] = b;
    for (int i = 0; i < 3; i++)
    {
        double c = channels[i] / 255.0;

        channels[i] = c <= 0.04045 ? c / 12.92 : pow ((c + 0.055) / 1.055, 2.4);
    }
    *out = 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
    return TRUE;
}

/* The WCAG contrast ratio between two colours, or FALSE when either is not a
 * colour this can measure. */
static gboolean
contrast (const char *a,
          const char *b,
          double     *out)
{
    double la, lb;

    if (!luminance (a, &la) || !luminance (b, &lb))
    {
        return FALSE;
    }
    *out = (MAX (la, lb) + 0.05) / (MIN (la, lb) + 0.05);
    return TRUE;
}

/* Whether the contrast between `a` and `b` reaches `minimum`; colours that
 * can't be measured pass. */
static gboolean
contrast_at_least (const char *a,
                   const char *b,
                   double      minimum)
{
    double ratio;

    return !contrast (a, b, &ratio) || ratio >= minimum;
}

static double
rem_euclid (double value,
            double modulus)
{
    double r = fmod (value, modulus);

    return r < 0.0 ? r + fabs (modulus) : r;
}

/* #rrggbb as hue, saturation and lightness, each 0..1. */
static gboolean
hsl (const char *color,
     double     *hue,
     double     *saturation,
     double     *lightness)
{
    guint8 r8, g8, b8;
    double r, g, b, max, min, chroma, h;

    if (!rgb (color, &r8, &g8, &b8))
    {
        return FALSE;
    }
    r = r8 / 255.0;
    g = g8 / 255.0;
    b = b8 / 255.0;
    max = MAX (MAX (r, g), b);
    min = MIN (MIN (r, g), b);
    *lightness = (max + min) / 2.0;
    chroma = max - min;
    if (chroma == 0.0)
    {
        *hue = 0.0;
        *saturation = 0.0;
        return TRUE;
    }
    if (max == r)
    {
        h = rem_euclid ((g - b) / chroma, 6.0);
    }
    else if (max == g)
    {
        h = (b - r) / chroma + 2.0;
    }
    else
    {
        h = (r - g) / chroma + 4.0;
    }
    *hue = h / 6.0;
    *saturation = chroma / (1.0 - fabs (2.0 * *lightness - 1.0));
    return TRUE;
}

static guint8
hsl_channel (double value,
             double base)
{
    return (guint8) CLAMP (round ((value + base) * 255.0), 0.0, 255.0);
}

static char *
from_hsl (double hue,
          double saturation,
          double lightness)
{
    double chroma = (1.0 - fabs (2.0 * lightness - 1.0)) * saturation;
    double h = hue * 6.0;
    double second = chroma * (1.0 - fabs (rem_euclid (h, 2.0) - 1.0));
    double r, g, b, base;

    switch ((guint) h)
    {
        case 0: r = chroma; g = second; b = 0.0; break;
        case 1: r = second; g = chroma; b = 0.0; break;
        case 2: r = 0.0; g = chroma; b = second; break;
        case 3: r = 0.0; g = second; b = chroma; break;
        case 4: r = second; g = 0.0; b = chroma; break;
        default: r = chroma; g = 0.0; b = second; break;
    }
    base = lightness - chroma / 2.0;
    return g_strdup_printf ("#%02x%02x%02x",
                            hsl_channel (r, base),
                            hsl_channel (g, base),
                            hsl_channel (b, base));
}

/* The chroma an ink may carry before dimming it tints the whole window:
 * libadwaita composites .dim-label at 55%, and 55% of a saturated blue over a
 * pale surface is lavender. */
#define INK_MAX_SATURATION 0.30
/* The contrast body text is held to, so that its dimmed half stays legible. */
#define INK_CONTRAST 7.0
#define INK_STEP 0.005

/* A light theme's foreground is the ink its terminal writes with, where a
 * saturated mid-tone is the theme's signature. In a window that one colour
 * becomes every label, and libadwaita dims many to 55%. So the ink keeps the
 * theme's hue but is pulled under INK_MAX_SATURATION and deepened until it
 * reaches INK_CONTRAST. A foreground that already reads as ink is returned
 * as written, which is every light theme omarchy ships. Dark themes are left
 * alone. */
static char *
ink (const char *fg,
     const char *bg)
{
    double hue, saturation, lightness;

    if (!hsl (fg, &hue, &saturation, &lightness))
    {
        return g_strdup (fg);
    }
    if (saturation <= INK_MAX_SATURATION && contrast_at_least (fg, bg, INK_CONTRAST))
    {
        return g_strdup (fg);
    }
    saturation = MIN (saturation, INK_MAX_SATURATION);
    for (;;)
    {
        g_autofree char *candidate = from_hsl (hue, saturation, lightness);

        if (lightness <= 0.0 || contrast_at_least (candidate, bg, INK_CONTRAST))
        {
            return g_steal_pointer (&candidate);
        }
        lightness = MAX (lightness - INK_STEP, 0.0);
    }
}

/* libadwaita's stock hairline opacity, the ceiling past which a line stops
 * being a hairline, and the contrast a hairline needs to read as a line. */
#define BORDER_OPACITY 15
#define BORDER_OPACITY_MAX 40
#define BORDER_CONTRAST 1.4

/* The lowest --border-opacity, in percent, at which `fg` over each of the
 * surfaces reaches BORDER_CONTRAST. Colours that aren't hex keep the stock
 * opacity. */
static guint
border_opacity (const char         *fg,
                const char * const *surfaces)
{
    for (guint opacity = BORDER_OPACITY; opacity <= BORDER_OPACITY_MAX; opacity++)
    {
        gboolean all = TRUE;

        for (const char * const *surface = surfaces; *surface != NULL && all; surface++)
        {
            g_autofree char *line = mix (*surface, fg, opacity / 100.0);

            all = line == NULL || contrast_at_least (line, *surface, BORDER_CONTRAST);
        }
        if (all)
        {
            return opacity;
        }
    }
    return BORDER_OPACITY_MAX;
}

/* Black or white, whichever stays legible on `color` (Rec. 601 luma, the
 * threshold GTK uses to pick icon shades). */
static const char *
readable_on (const char *color)
{
    guint8 r, g, b;

    if (rgb (color, &r, &g, &b) && 0.299 * r + 0.587 * g + 0.114 * b > 140.0)
    {
        return "#000000";
    }
    return "#ffffff";
}

/* mode, then the legacy theme_type, then a light.mode file beside
 * colors.toml, then the background's brightness. */
static gboolean
resolve_mode (Colors   *colors,
              gboolean  light_mode_file)
{
    const char *keys[] = { "mode", "theme_type" };
    guint8 r, g, b;

    for (guint i = 0; i < G_N_ELEMENTS (keys); i++)
    {
        const char *mode = lookup (colors, keys[i]);

        if (mode != NULL)
        {
            return g_ascii_strcasecmp (mode, "light") == 0;
        }
    }
    if (light_mode_file)
    {
        return TRUE;
    }
    if (rgb (g_hash_table_lookup (colors, "background"), &r, &g, &b))
    {
        return (guint) r + g + b > 382;
    }
    return FALSE;
}

/* Port of omarchy-theme-color's parse-and-resolve. */
static void
resolve (Colors *colors)
{
    static const char * const legacy[][2] =
    {
        { "background", "bg" },
        { "dark_background", "dark_bg" },
        { "darker_background", "darker_bg" },
        { "lighter_background", "lighter_bg" },
        { "foreground", "fg" },
        { "dark_foreground", "dark_fg" },
        { "light_foreground", "light_fg" },
        { "bright_foreground", "bright_fg" },
    };
    static const char * const named_ansi[][2] =
    {
        { "red", "color1" },
        { "green", "color2" },
        { "yellow", "color3" },
        { "blue", "color4" },
        { "magenta", "color5" },
        { "cyan", "color6" },
        { "bright_red", "color9" },
        { "bright_green", "color10" },
        { "bright_yellow", "color11" },
        { "bright_blue", "color12" },
        { "bright_magenta", "color13" },
        { "bright_cyan", "color14" },
    };
    static const char * const brights[][2] =
    {
        { "bright_red", "red" },
        { "bright_yellow", "yellow" },
        { "bright_green", "green" },
        { "bright_cyan", "cyan" },
        { "bright_blue", "blue" },
        { "bright_magenta", "magenta" },
    };
    static const char * const ansi_named[][2] =
    {
        { "color0", "background" },
        { "color1", "red" },
        { "color2", "green" },
        { "color3", "yellow" },
        { "color4", "blue" },
        { "color5", "magenta" },
        { "color6", "cyan" },
        { "color7", "foreground" },
        { "color8", "muted" },
        { "color9", "bright_red" },
        { "color10", "bright_green" },
        { "color11", "bright_yellow" },
        { "color12", "bright_blue" },
        { "color13", "bright_magenta" },
        { "color14", "bright_cyan" },
        { "color15", "bright_foreground" },
    };

    /* The short palette names an older theme may use instead. */
    for (guint i = 0; i < G_N_ELEMENTS (legacy); i++)
    {
        alias (colors, legacy[i][0], legacy[i][1]);
    }

    /* Themes predating the semantic palette define only ANSI names. */
    alias (colors, "background", "color0");
    alias (colors, "foreground", "color7");
    mirror (colors, "color0", "background");
    mirror (colors, "color7", "foreground");

    for (guint i = 0; i < G_N_ELEMENTS (named_ansi); i++)
    {
        alias (colors, named_ansi[i][0], named_ansi[i][1]);
    }
    alias (colors, "magenta", "purple");
    alias (colors, "bright_magenta", "bright_purple");

    ALIAS_ANY (colors, "light_foreground", "color7", "foreground");
    ALIAS_ANY (colors, "bright_foreground", "color15", "foreground");
    /* Unconditional, exactly as omarchy resolves it. */
    mirror (colors, "cursor", "bright_foreground");
    ALIAS_ANY (colors, "lighter_background", "color0", "background");
    ALIAS_ANY (colors, "dark_foreground", "color8", "foreground");
    ALIAS_ANY (colors, "muted", "color8", "dark_foreground");
    ALIAS_ANY (colors, "selection", "selection_background", "color8", "color0", "background");
    alias (colors, "selection_background", "selection");
    alias (colors, "selection_foreground", "bright_foreground");
    alias (colors, "orange", "yellow");
    derive (colors, "brown", "orange", "#000000", 0.5);

    derive (colors, "dark_background", "background", "#000000", 0.25);
    derive (colors, "darker_background", "background", "#000000", 0.5);
    for (guint i = 0; i < G_N_ELEMENTS (brights); i++)
    {
        derive (colors, brights[i][0], brights[i][1], "#ffffff", 0.2);
    }

    for (guint i = 0; i < G_N_ELEMENTS (ansi_named); i++)
    {
        alias (colors, ansi_named[i][0], ansi_named[i][1]);
    }
}

/* A palette key, or the value of `fallback` when the theme is missing it even
 * after the cascade, or `fallback` itself. */
static const char *
get (Colors     *colors,
     const char *key,
     const char *fallback)
{
    const char *value = lookup (colors, key);

    if (value != NULL)
    {
        return value;
    }
    value = g_hash_table_lookup (colors, fallback);
    return value != NULL ? value : fallback;
}

static void
property (GString    *css,
          const char *name,
          const char *value)
{
    g_string_append_printf (css, "  %s: %s;\n", name, value);
}

char *
nautilus_omarchy_palette_css (const char *colors_toml,
                              gboolean    light_mode_file,
                              gboolean   *light_out)
{
    g_autoptr (GHashTable) colors = parse (colors_toml);
    GString *css;
    const char *bg, *accent, *chrome, *backdrop, *raised;
    g_autofree char *fg = NULL;
    g_autofree char *border = NULL;
    g_autofree char *opacity_text = NULL;
    gboolean light;
    guint opacity;
    static const char * const bars[] = { "--headerbar", "--sidebar", "--secondary-sidebar" };
    static const char * const surfaces[] = { "--card", "--popover", "--dialog" };
    static const char * const states[][3] =
    {
        { "--success", "green", "bright_green" },
        { "--warning", "yellow", "bright_yellow" },
        { "--error", "red", "bright_red" },
        { "--destructive", "red", "bright_red" },
    };

    resolve (colors);
    light = resolve_mode (colors, light_mode_file);
    if (light_out != NULL)
    {
        *light_out = light;
    }

    bg = get (colors, "background", "#1d1d20");
    fg = light ? ink (get (colors, "foreground", "#d0cfcc"), bg)
               : g_strdup (get (colors, "foreground", "#d0cfcc"));
    accent = get (colors, "accent", "blue");
    /* Chrome sits one step off the content background in both modes. */
    chrome = get (colors, "dark_background", "background");
    backdrop = get (colors, "darker_background", "background");
    raised = get (colors, "lighter_background", "background");
    /* Stock libadwaita's 15% hairline assumes a near-black or near-white
     * foreground; a light theme's mid-tone text needs more to show. */
    opacity = border_opacity (fg, (const char * const[]) { bg, chrome, NULL });
    border = g_strdup_printf ("color-mix(in srgb, %s %u%%, transparent)", fg, opacity);
    opacity_text = g_strdup_printf ("%u%%", opacity);

    css = g_string_new ("/* Generated by Files from the active Omarchy theme; rebuilt on\n"
                        " * every theme change. A nautilus.css in the theme is layered after it. */\n"
                        ":root {\n");
    property (css, "--accent-bg-color", accent);
    property (css, "--accent-fg-color", readable_on (accent));
    property (css, "--accent-color", accent);
    property (css, "--window-bg-color", bg);
    property (css, "--window-fg-color", fg);
    property (css, "--view-bg-color", bg);
    property (css, "--view-fg-color", fg);
    property (css, "--border-opacity", opacity_text);
    for (guint i = 0; i < G_N_ELEMENTS (bars); i++)
    {
        g_autofree char *bg_name = g_strconcat (bars[i], "-bg-color", NULL);
        g_autofree char *fg_name = g_strconcat (bars[i], "-fg-color", NULL);
        g_autofree char *backdrop_name = g_strconcat (bars[i], "-backdrop-color", NULL);
        g_autofree char *border_name = g_strconcat (bars[i], "-border-color", NULL);

        property (css, bg_name, chrome);
        property (css, fg_name, fg);
        property (css, backdrop_name, backdrop);
        /* libadwaita applies --border-opacity to the headerbar's border
         * itself; the sidebars' are used as written. */
        property (css, border_name, i == 0 ? fg : border);
    }
    for (guint i = 0; i < G_N_ELEMENTS (surfaces); i++)
    {
        g_autofree char *bg_name = g_strconcat (surfaces[i], "-bg-color", NULL);
        g_autofree char *fg_name = g_strconcat (surfaces[i], "-fg-color", NULL);

        property (css, bg_name, i == 0 ? raised : chrome);
        property (css, fg_name, fg);
    }
    for (guint i = 0; i < G_N_ELEMENTS (states); i++)
    {
        const char *base = get (colors, states[i][1], states[i][1]);
        g_autofree char *bg_name = g_strconcat (states[i][0], "-bg-color", NULL);
        g_autofree char *fg_name = g_strconcat (states[i][0], "-fg-color", NULL);
        g_autofree char *color_name = g_strconcat (states[i][0], "-color", NULL);

        property (css, bg_name, base);
        property (css, fg_name, readable_on (base));
        property (css, color_name, get (colors, states[i][2], base));
    }
    g_string_append (css, "}\n");

    return g_string_free (css, FALSE);
}

static gboolean
name_char (char ch)
{
    return g_ascii_isalnum (ch) || ch == '-' || ch == '_' || (guchar) ch >= 0x80;
}

gboolean
nautilus_omarchy_sidebar_opacity_in (const char *css,
                                     double     *opacity)
{
    const gsize name_len = strlen (NAUTILUS_OMARCHY_SIDEBAR_OPACITY);
    gboolean found = FALSE;
    double value = 1.0;
    const char *at = css;
    const char *start;

    while ((start = strpbrk (at, "/-")) != NULL)
    {
        const char *rest;
        const char *end;
        g_autofree char *text = NULL;
        char *parsed_end;
        double number;

        if (g_str_has_prefix (start, "/*"))
        {
            const char *close = strstr (start + 2, "*/");

            if (close == NULL)
            {
                break;
            }
            at = close + 2;
            continue;
        }

        /* --x--nautilus-sidebar-opacity is a different property, and so is
         * --nautilus-sidebar-opacity-x; var(--nautilus-sidebar-opacity)
         * reads it rather than setting it. */
        if ((start > css && name_char (start[-1])) ||
            strncmp (start, NAUTILUS_OMARCHY_SIDEBAR_OPACITY, name_len) != 0)
        {
            at = start + 1;
            continue;
        }
        rest = start + name_len;
        while (g_ascii_isspace (*rest))
        {
            rest++;
        }
        if (*rest != ':')
        {
            at = start + 1;
            continue;
        }
        rest++;

        end = strpbrk (rest, ";}\n");
        if (end == NULL)
        {
            end = rest + strlen (rest);
        }
        text = g_strstrip (g_strndup (rest, end - rest));
        number = g_ascii_strtod (text, &parsed_end);
        found = *text != '\0' && *parsed_end == '\0' && isfinite (number);
        value = found ? CLAMP (number, 0.0, 1.0) : 1.0;
        at = end;
    }

    if (found && opacity != NULL)
    {
        *opacity = value;
    }
    return found;
}
