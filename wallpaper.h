#ifndef _WALLPAPER_H_
#define _WALLPAPER_H_

#include <xcb/xcb.h>
#include <stdlib.h>

xcb_pixmap_t get_root_pixmap(xcb_connection_t* conn, xcb_screen_t* screen);
xcb_pixmap_t copy_root_pixmap(xcb_connection_t* conn, xcb_screen_t* screen);

#endif /* ifndef _WALLPAPER_H_ */
