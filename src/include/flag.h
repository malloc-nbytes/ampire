#ifndef FLAG_H
#define FLAG_H

#include <stddef.h>

enum {
        FT_NONOTIF = 1 << 0,
};

extern size_t g_flags;

#endif // FLAG_H
