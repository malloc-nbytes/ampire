#include <stdio.h>

#define CLAP_IMPL
#include "clap.h"
#include "dyn_array.h"
#include "ds/array.h"
#include "ampire-utils.h"
#include "ampire-io.h"
#include "ampire-display.h"
#include "ampire-flag.h"

#define FLAG_1HY_HELP 'h'
#define FLAG_1HY_RECURSIVE 'r'
#define FLAG_1HY_CLR_SAVED_SONGS 'c'
#define FLAG_2HY_HELP "help"
#define FLAG_2HY_NOTIF "notif"
#define FLAG_2HY_RECURSIVE "recursive"
#define FLAG_2HY_CLR_SAVED_SONGS "clear"

size_t g_flags = 0x0;

void usage(void) {
        printf("Usage: ampire <dir> [options...]\n");
        printf("Options:\n");
        printf("    -%c, --%s         print this help message\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    -%c, --%s    enable recursive search for songs\n", FLAG_1HY_RECURSIVE, FLAG_2HY_RECURSIVE);
        printf("    -%c, --%s        clear saved songs in config file\n", FLAG_1HY_CLR_SAVED_SONGS, FLAG_2HY_CLR_SAVED_SONGS);
        printf("        --%s        display notifications on song change\n", FLAG_2HY_NOTIF);
        exit(0);
}

int main(int argc, char **argv) {
        --argc, ++argv;
        clap_init(argc, argv);

        Str_Array dirs; dyn_array_init_type(dirs);

        Clap_Arg arg = {0};
        while (clap_next(&arg)) {
                if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_HELP) {
                        usage();
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_RECURSIVE) {
                        g_flags |= FT_RECURSIVE;
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_CLR_SAVED_SONGS) {
                        g_flags |= FT_CLR_SAVED_SONGS;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_HELP)) {
                        usage();
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_NOTIF)) {
                        g_flags |= FT_NOTIF;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_RECURSIVE)) {
                        g_flags |= FT_RECURSIVE;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_CLR_SAVED_SONGS)) {
                        g_flags |= FT_CLR_SAVED_SONGS;
                } else if (arg.hyphc > 0) {
                        err_wargs("invalid flag: %s", arg.start);
                } else {
                        dyn_array_append(dirs, arg.start);
                }
        }

        if (g_flags & FT_CLR_SAVED_SONGS) {
                io_clear_config_file();
        }

        Playlist_Array playlists = io_read_config_file();
        Playlist_Array cli_playlists = io_flatten_dirs(&dirs);

        // If the user just wants to clear the saved songs
        // and don't provide any music to open.
        if ((g_flags & FT_CLR_SAVED_SONGS) && cli_playlists.len == 0) {
                exit(0);
        }

        if (cli_playlists.len > 0) {
                for (size_t i = 0; i < cli_playlists.len; ++i) {
                        dyn_array_append(playlists, cli_playlists.data[i]);
                }
        }

        if (playlists.len > 9) {
                printf("Ampire currently only supports up to 9 separate playlists");
                exit(1);
        }

        run(&playlists);

        // TODO: memory free().

        return 0;
}
