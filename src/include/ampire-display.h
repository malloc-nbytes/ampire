#ifndef DISPLAY_H
#define DISPLAY_H

#include "ds/array.h"

typedef struct {
        Str_Array songfps;
        char *name;
        int from_cli;
} Playlist;

DYN_ARRAY_TYPE(Playlist, Playlist_Array);

void run(const Playlist_Array *playlists);

#endif // DISPLAY_H
