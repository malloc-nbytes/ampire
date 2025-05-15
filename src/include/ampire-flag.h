#ifndef FLAG_H
#define FLAG_H

#include <stddef.h>

enum {
        FT_NOTIF = 1 << 0,
        FT_RECURSIVE = 1 << 1,
        FT_CLR_SAVED_SONGS = 1 << 2,
        FT_SHOW_SAVES = 1 << 3,
};

extern size_t g_flags;

#endif // FLAG_H
