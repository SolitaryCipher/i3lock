#include "wallpaper.h"

xcb_pixmap_t get_root_pixmap(xcb_connection_t* conn, xcb_screen_t* screen)
{
	xcb_intern_atom_cookie_t atom_cookie =
		xcb_intern_atom(conn, 0, 13, "_XROOTPMAP_ID");
	xcb_intern_atom_reply_t* atom_reply =
		xcb_intern_atom_reply(conn, atom_cookie, NULL);
	xcb_atom_t atom = atom_reply->atom;
	free(atom_reply);

	xcb_get_property_cookie_t cookie =
		xcb_get_property(conn, 0, screen->root, atom, XCB_ATOM_PIXMAP, 0, 1);
	xcb_get_property_reply_t* reply =
		xcb_get_property_reply(conn, cookie, NULL);

	xcb_pixmap_t pixmap = *((xcb_pixmap_t*) xcb_get_property_value(reply));
	free(reply);

	return pixmap;
}

xcb_pixmap_t copy_root_pixmap(xcb_connection_t* conn, xcb_screen_t* screen)
{
	xcb_pixmap_t root_pixmap = get_root_pixmap(conn, screen);

	xcb_pixmap_t pixmap = xcb_generate_id(conn);
	xcb_create_pixmap(conn, screen->root_depth, pixmap, screen->root,
			screen->width_in_pixels, screen->height_in_pixels);

	xcb_gcontext_t gc = xcb_generate_id(conn);
	uint32_t values[1] = {0xffffff};

	xcb_create_gc(conn, gc, screen->root, XCB_GC_BACKGROUND, values);
	xcb_copy_area(conn, root_pixmap, pixmap, gc, 0, 0, 0, 0, screen->width_in_pixels, screen->height_in_pixels);
	xcb_free_gc(conn, gc);

	return pixmap;
}

