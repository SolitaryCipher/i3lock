/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "xinerama.h"

#define ICON_RADIUS (25 * icon_scale)
#define ICON_CENTER (32 * icon_scale)
#define ICON_SIZE (2 * ICON_CENTER)

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];

extern char color_icon[7];
extern char color_verify[7];
extern char color_wrong[7];

extern double icon_scale;

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Number of failed unlock attempts. */
extern int failed_attempts;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
pam_state_t pam_state;

/*
 * Returns the scaling factor of the current screen. E.g., on a 227 DPI MacBook
 * Pro 13" Retina screen, the scaling factor is 227/96 = 2.36.
 *
 */
static double scaling_factor(void) {
    const int dpi = (double)screen->height_in_pixels * 25.4 /
                    (double)screen->height_in_millimeters;
    return (dpi / 96.0);
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;
    int button_diameter_physical = ceil(scaling_factor() * ICON_SIZE);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          scaling_factor(), button_diameter_physical);

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, button_diameter_physical, button_diameter_physical);
    cairo_t *ctx = cairo_create(output);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    if (img) {
        if (!tile) {
            cairo_set_source_surface(xcb_ctx, img, 0, 0);
            cairo_paint(xcb_ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(xcb_ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
            cairo_fill(xcb_ctx);
            cairo_pattern_destroy(pattern);
        }
    } else {
        char strgroups[3][3] = {{color[0], color[1], '\0'},
                                {color[2], color[3], '\0'},
                                {color[4], color[5], '\0'}};
        uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                             (strtol(strgroups[1], NULL, 16)),
                             (strtol(strgroups[2], NULL, 16))};
        cairo_set_source_rgb(xcb_ctx, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
        cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
        cairo_fill(xcb_ctx);
    }

    char strgroups_base[3][3] = {
        {color_icon[0], color_icon[1], '\0'},
        {color_icon[2], color_icon[3], '\0'},
        {color_icon[4], color_icon[5], '\0'}};
    uint32_t rgb16_base[3] = {
        (strtol(strgroups_base[0], NULL, 16)),
        (strtol(strgroups_base[1], NULL, 16)),
        (strtol(strgroups_base[2], NULL, 16))};

    char strgroups_verify[3][3] = {
        {color_verify[0], color_verify[1], '\0'},
        {color_verify[2], color_verify[3], '\0'},
        {color_verify[4], color_verify[5], '\0'}};
    uint32_t rgb16_verify[3] = {
        (strtol(strgroups_verify[0], NULL, 16)),
        (strtol(strgroups_verify[1], NULL, 16)),
        (strtol(strgroups_verify[2], NULL, 16))};

    char strgroups_wrong[3][3] = {
        {color_wrong[0], color_wrong[1], '\0'},
        {color_wrong[2], color_wrong[3], '\0'},
        {color_wrong[4], color_wrong[5], '\0'}};
    uint32_t rgb16_wrong[3] = {
        (strtol(strgroups_wrong[0], NULL, 16)),
        (strtol(strgroups_wrong[1], NULL, 16)),
        (strtol(strgroups_wrong[2], NULL, 16))};

    if (unlock_indicator) {
        cairo_scale(ctx, scaling_factor(), scaling_factor());
        cairo_set_line_cap(ctx, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(ctx, CAIRO_LINE_JOIN_ROUND);

        /* Draw outer circle, using appropriate color */
        switch(pam_state) {
            case STATE_PAM_IDLE:
                cairo_set_source_rgb(ctx,
                        rgb16_base[0] / 255.0, rgb16_base[1] / 255.0, rgb16_base[2] / 255.0);
                break;
            case STATE_PAM_VERIFY:
                cairo_set_source_rgb(ctx,
                        rgb16_verify[0] / 255.0, rgb16_verify[1] / 255.0, rgb16_verify[2] / 255.0);
                break;
            case STATE_PAM_WRONG:
                cairo_set_source_rgb(ctx,
                        rgb16_wrong[0] / 255.0, rgb16_wrong[1] / 255.0, rgb16_wrong[2] / 255.0);
                break;
        }

        /* Draw the lock icon */
        cairo_set_line_width(ctx, 3 * icon_scale);
        cairo_arc(ctx, ICON_CENTER, ICON_CENTER, ICON_RADIUS, 0, 2 * M_PI);
        cairo_stroke(ctx);

        /* Draw keyhole */
        cairo_set_source_rgb(ctx,
                rgb16_base[0] / 255.0, rgb16_base[1] / 255.0, rgb16_base[2] / 255.0);
        cairo_set_line_width(ctx, icon_scale);
        cairo_arc(ctx, ICON_CENTER, ICON_CENTER + 4 * icon_scale, 3 * icon_scale, 0, 2 * M_PI);
        cairo_fill(ctx);

        cairo_set_line_width(ctx, 3 * icon_scale);
        cairo_move_to(ctx, ICON_CENTER, ICON_CENTER + 4 * icon_scale);
        cairo_rel_line_to(ctx, 0.0, 4.5 * icon_scale);
        cairo_stroke(ctx);

        /* Draw body */
        cairo_rectangle(ctx, ICON_CENTER - 11 * icon_scale, ICON_CENTER - 4 * icon_scale, 22 * icon_scale, 19 * icon_scale);
        cairo_stroke(ctx);

        /* Draw arm */
        cairo_arc(ctx, ICON_CENTER, ICON_CENTER - 11 * icon_scale, 7.5 * icon_scale, M_PI, 0);
        cairo_stroke(ctx);

        cairo_move_to(ctx, ICON_CENTER - 7.5 * icon_scale, ICON_CENTER - 11 * icon_scale);
        cairo_rel_line_to(ctx, 0, 7 * icon_scale);
        cairo_stroke(ctx);

        cairo_move_to(ctx, ICON_CENTER + 7.5 * icon_scale, ICON_CENTER - 11 * icon_scale);
        cairo_rel_line_to(ctx, 0, 7 * icon_scale);
        cairo_stroke(ctx);


        /* Draw dots for password */
        if (input_position > 0) {
            /* Color dots red if caps lock is on */
            if (modifier_string != NULL && strcmp(modifier_string, "Caps Lock") == 0) {
                cairo_set_source_rgb(ctx, rgb16_wrong[0] / 255.0, rgb16_wrong[1] / 255.0, rgb16_wrong[2] / 255.0);
            }

            int i;
            double dot_arc = (M_PI / 2.0) - ((M_PI / 25.0) * (input_position - 1) / 2.0);
            for(i = 0; i < input_position; ++i) {
                cairo_arc(ctx, ICON_CENTER, ICON_CENTER, ICON_RADIUS + 5 * icon_scale, dot_arc, dot_arc);
                cairo_stroke(ctx);
                dot_arc += M_PI / 25.0;
            }
        }
    }

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (button_diameter_physical / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (button_diameter_physical / 2)));
            cairo_set_source_surface(xcb_ctx, output, x, y);
            cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (button_diameter_physical / 2);
        int y = (last_resolution[1] / 2) - (button_diameter_physical / 2);
        cairo_set_source_surface(xcb_ctx, output, x, y);
        cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
        cairo_fill(xcb_ctx);
    }

    cairo_surface_destroy(xcb_output);
    cairo_surface_destroy(output);
    cairo_destroy(ctx);
    cairo_destroy(xcb_ctx);
    return bg_pixmap;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, pam_state = %d)\n", unlock_state, pam_state);
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_free_pixmap(conn, bg_pixmap);
    xcb_flush(conn);
}

/*
 * Hides the unlock indicator completely when there is no content in the
 * password buffer.
 *
 */
void clear_indicator(void) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else
        unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}
