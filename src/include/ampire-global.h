#ifndef AMPIRE_GLOBAL_H
#define AMPIRE_GLOBAL_H

#include <stdint.h>

extern struct {
        uint32_t flags;
        int volume;
        int playlist;
} g_config;

#endif // AMPIRE_GLOBAL_H
