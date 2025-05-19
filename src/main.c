#include <assert.h>
#include <stdio.h>
#include <ctype.h>

#define CLAP_IMPL
#include "clap.h"
#include "dyn_array.h"
#include "ds/array.h"
#include "ampire-utils.h"
#include "ampire-io.h"
#include "ampire-display.h"
#include "ampire-flag.h"
#include "config.h"

#define FLAG_1HY_HELP 'h'
#define FLAG_1HY_RECURSIVE 'r'
#define FLAG_1HY_CLR_SAVED_SONGS 'c'
#define FLAG_2HY_HELP "help"
#define FLAG_2HY_NOTIF "notif"
#define FLAG_2HY_RECURSIVE "recursive"
#define FLAG_2HY_CLR_SAVED_SONGS "clear"
#define FLAG_2HY_SHOW_SAVES "show-saves"
#define FLAG_2HY_DISABLE_PLAYER_LOGO "no-player-logo"
#define FLAG_2HY_VOLUME "volume"

size_t g_flags = 0x0;

struct {
        int volume;
} g_config = {
        .volume = -1,
};

// TODO: fix memory leaks

void usage(void) {
        printf("(MIT License) Copyright (c) 2025 malloc-nbytes\n\n");
        printf("Ampire v" VERSION ", (compiler) " COMPILER_INFO "\n\n");
        printf("   (                                  \n");
        printf("   )\\       )          (   (      (   \n");
        printf("((((_)(    (     `  )  )\\  )(    ))\\  \n");
        printf(" )\\ _ )\\   )\\  ' /(/( ((_)(()\\  /((_) \n");
        printf(" (_)_\\(_)_((_)) ((_)_\\ (_) ((_)(_))   \n");
        printf("  / _ \\ | '  \\()| '_ \\)| || '_|/ -_)  \n");
        printf(" /_/ \\_\\|_|_|_| | .__/ |_||_|  \\___|  \n");
        printf("                |_|                  \n\n");
        printf("Usage: ampire [dir...] [options...]\n");
        printf("Options:\n");
        printf("    -%c, --%s             print this help message\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    -%c, --%s        enable recursive search for songs\n", FLAG_1HY_RECURSIVE, FLAG_2HY_RECURSIVE);
        printf("    -%c, --%s            clear saved songs in config file\n", FLAG_1HY_CLR_SAVED_SONGS, FLAG_2HY_CLR_SAVED_SONGS);
        printf("        --%s            display notifications on song change\n", FLAG_2HY_NOTIF);
        printf("        --%s       print all saved songs\n", FLAG_2HY_SHOW_SAVES);
        printf("        --%s   do not show the logo in the player\n", FLAG_2HY_DISABLE_PLAYER_LOGO);
        printf("        --%s=v         set the volume as `v` where 0 <= v <= 128 (note: not a percentage)\n", FLAG_2HY_VOLUME);
        exit(0);
}

int main(int argc, char **argv) {
        --argc, ++argv;
        clap_init(argc, argv);

        Str_Array dirs = dyn_array_empty(Str_Array);

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
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_SHOW_SAVES)) {
                        g_flags |= FT_SHOW_SAVES;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_DISABLE_PLAYER_LOGO)) {
                        g_flags |= FT_DISABLE_PLAYER_LOGO;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_VOLUME)) {
                        g_flags |= FT_VOLUME;
                        if (!arg.eq)              err("--volume expects a value after equals (=)\n");
                        if (!str_isdigit(arg.eq)) err_wargs("--volume expects a number, not `%s`\n", arg.eq);
                        int v = atoi(arg.eq);
                        if (v < 0 || v > 128)     err_wargs("volume level %d is out of range of [0..=128]", v);
                        g_config.volume = v;
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
