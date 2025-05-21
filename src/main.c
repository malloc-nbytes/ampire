#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

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
#define FLAG_1HY_VERSION 'v'
#define FLAG_2HY_HELP "help"
#define FLAG_2HY_NOTIF "notif"
#define FLAG_2HY_RECURSIVE "recursive"
#define FLAG_2HY_CLR_SAVED_SONGS "clear"
#define FLAG_2HY_SHOW_SAVES "show-saves"
#define FLAG_2HY_DISABLE_PLAYER_LOGO "no-player-logo"
#define FLAG_2HY_VOLUME "volume"
#define FLAG_2HY_PLAYLIST "playlist"
#define FLAG_2HY_VERSION "version"
#define FLAG_2HY_CONTROLS "controls"
#define FLAG_2HY_HISTORY_SZ "history-sz"

struct {
        uint32_t flags;
        int volume;
        int playlist;
        int history_sz;
} g_config = {
        .flags = 0x0,
        .volume = -1,
        .playlist = -1,
        .history_sz = 5,
};

// TODO: fix memory leaks

void usage(void) {
        printf("(MIT License) Copyright (c) 2025 malloc-nbytes\n\n");
        printf("Ampire v" VERSION ", (compiler) " COMPILER_INFO "\n\n");
        printf("Usage: ampire [dir...] [options...]\n");
        printf("Options:\n");
        printf("    -%c, --%s[=<flag>|*]  print this help message or get help on an individual flag\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    -%c, --%s          view version\n", FLAG_1HY_VERSION, FLAG_2HY_VERSION);
        printf("    -%c, --%s        enable recursive search for songs\n", FLAG_1HY_RECURSIVE, FLAG_2HY_RECURSIVE);
        printf("    -%c, --%s            clear saved songs in config file\n", FLAG_1HY_CLR_SAVED_SONGS, FLAG_2HY_CLR_SAVED_SONGS);
        printf("        --%s            display notifications on song change\n", FLAG_2HY_NOTIF);
        printf("        --%s       print all saved songs\n", FLAG_2HY_SHOW_SAVES);
        printf("        --%s   do not show the logo in the player\n", FLAG_2HY_DISABLE_PLAYER_LOGO);
        printf("        --%s=v         set the volume as `v` where 0 <= v <= 128 (note: not a percentage)\n", FLAG_2HY_VOLUME);
        printf("        --%s=p       set the playlist to `p`\n", FLAG_2HY_PLAYLIST);
        printf("        --%s=i     set the history size to `i`\n", FLAG_2HY_HISTORY_SZ);
        exit(0);
}

static void version(void) {
        printf("Ampire v" VERSION "\n");
        exit(0);
}

static void history_sz_info(void) {
        printf("--help(%s):\n", FLAG_2HY_HISTORY_SZ);
        printf("    Set the number of history items gets displayed.\n");
        printf("    This is useful if you have more/less screenspace\n");
        printf("    and want to change the default number (5).\n");
}

static void controls_info(void) {
        printf("--help(%s):\n", FLAG_2HY_CONTROLS);
        printf("    View the ampire player controls\n");
}

static void playlist_info(void) {
        printf("--help(%s):\n", FLAG_2HY_PLAYLIST);
        printf("    Set the starting playlist from the command line. This is usefull for\n");
        printf("    if you are scripting button events in your WM/DE to open ampire *and* you want\n");
        printf("    a specific playlist to be selected.\n");
        printf("    As of now, it accepts and integer between [1..=9], but in the future, it will\n");
        printf("    also accept a name.\n");
        printf("    Example:\n");
        printf("        ampire --playlist=3\n");
        printf("    This will set the playlist to the 3rd one saved.\n");
}

static void volume_info(void) {
        printf("--help(%s):\n", FLAG_2HY_VOLUME);
        printf("    Set the volume from the command line. This is useful for if you are\n");
        printf("    scripting button events in your WM/DE to open ampire *and* you want\n");
        printf("    to have it always launch with a specific volume.\n");
        printf("    The volume ranges from [0..=128], so it must be in that range, it is\n");
        printf("    NOT a percentage.\n");
        printf("    Example:\n");
        printf("        ampire --volume=65\n");
        printf("    This will set the volume to 50%%.\n");
}

static void disable_player_logo_info(void) {
        printf("--help(%s):\n", FLAG_2HY_DISABLE_PLAYER_LOGO);
        printf("    Do not show the ampire logo inside of the player. This can be used\n");
        printf("    to save screen space if you need it.\n");
}

static void show_saves_info(void) {
        printf("--help(%s):\n", FLAG_2HY_SHOW_SAVES);
        printf("    Show all saved playlists as well as the paths that the songs are located at\n");
}

static void clear_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_CLR_SAVED_SONGS, FLAG_2HY_CLR_SAVED_SONGS);
        printf("    All saved playlists gets saved in `/home/$USER/.ampire`.\n");
        printf("    If you want to reset this file, you can call this flag to clear it.\n");
        printf("    Note: ampire will exit upon invocation of this flag.\n");
}

static void notif_info(void) {
        printf("--help(%s):\n", FLAG_2HY_NOTIF);
        printf("    Enable usage of whatever notification system(s) you have installed\n");
        printf("    on the system to get desktop notifications on song changes, search failings, etc.\n");
        printf("    Note: If no notification programs are installed, ampire may produce bugs.\n");
}

static void recursive_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_RECURSIVE, FLAG_2HY_RECURSIVE);
        printf("    Enable recursive search for music in the directory(s) provided\n");
        printf("    Example:\n");
        printf("        Listing of: /home/$USER/Music/:\n");
        printf("        ├── breakcore\n");
        printf("        │   ├── song1.mp3\n");
        printf("        │   ├── song2.mp3\n");
        printf("        │   ├── song3.mp3\n");
        printf("        ├── electronic\n");
        printf("        │   ├── song4.mp3\n");
        printf("        │   ├── song5.mp3\n");
        printf("        ├── song6.mp3\n");
        printf("        Calling `ampire -r /home/$USER/Music` will get the music tracks:\n");
        printf("            [song1.mp3, song2.mp3, song3.mp3, song4.mp3, song5.mp3, song6.mp3]\n");
}

static void version_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_VERSION, FLAG_2HY_VERSION);
        printf("    See the version information\n");
}

static void help_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    Show the help menu or help on individual flags with --help=<flag>\n");
        printf("    Examples:\n");
        printf("        ampire --help\n");
        printf("        ampire -h\n");
        printf("        ampire --help=version\n");
        printf("        ampire -h=volume\n");
}

static void show_help_for_flag(const char *flag) {
        void (*help[])(void) = {
                help_info,
                version_info,
                recursive_info,
                clear_info,
                notif_info,
                show_saves_info,
                disable_player_logo_info,
                volume_info,
                playlist_info,
                controls_info,
                history_sz_info,
        };

#define OHYEQ(n, flag, actual) ((n) == 1 && (flag)[0] == (actual))
        size_t n = strlen(flag);
        if (OHYEQ(n, flag, FLAG_1HY_HELP) || !strcmp(flag, FLAG_2HY_HELP)) {
                help[0]();
        } else if (OHYEQ(n, flag, FLAG_1HY_VERSION) || !strcmp(flag, FLAG_2HY_VERSION)) {
                help[1]();
        } else if (OHYEQ(n, flag, FLAG_1HY_RECURSIVE) || !strcmp(flag, FLAG_2HY_RECURSIVE)) {
                help[2]();
        } else if (OHYEQ(n, flag, FLAG_1HY_CLR_SAVED_SONGS) || !strcmp(flag, FLAG_2HY_CLR_SAVED_SONGS)) {
                help[3]();
        } else if (!strcmp(flag, FLAG_2HY_NOTIF)) {
                help[4]();
        } else if (!strcmp(flag, FLAG_2HY_SHOW_SAVES)) {
                help[5]();
        } else if (!strcmp(flag, FLAG_2HY_DISABLE_PLAYER_LOGO)) {
                help[6]();
        } else if (!strcmp(flag, FLAG_2HY_VOLUME)) {
                help[7]();
        } else if (!strcmp(flag, FLAG_2HY_PLAYLIST)) {
                help[8]();
        } else if (!strcmp(flag, FLAG_2HY_CONTROLS)) {
                help[9]();
        } else if (!strcmp(flag, FLAG_2HY_HISTORY_SZ)) {
                help[10]();
        } else if (OHYEQ(n, flag, '*')) {
                for (size_t i = 0; i < sizeof(help)/8; ++i) {
                        help[i]();
                }
        } else {
                err_wargs("help(%s) info does not exist", flag);
        }
        exit(0);
#undef OHYEQ
}

static void controls(void) {
        printf("| Keybinds            | Action                                                    |\n");
        printf("|---------------------+-----------------------------------------------------------|\n");
        printf("| [ DOWN ], [ j ]     | Move down in the song selection                           |\n");
        printf("| [ UP ], [ k ]       | Move up in the song selection                             |\n");
        printf("| [ RIGHT ], [ l ]    | Seek forward in the music 10 seconds                      |\n");
        printf("| [ LEFT ], [ h ]     | Seek backward in the music 10 seconds                     |\n");
        printf("| [ . ], [ > ], [ L ] | Next song                                                 |\n");
        printf("| [ , ], [ < ], [ H ] | Previous song from history                                |\n");
        printf("| [ - ], [ _ ]        | Volume down                                               |\n");
        printf("| [ + ], [ = ]        | Volume up                                                 |\n");
        printf("| [ a ]               | Toggle song advancement between normal, shuffle, and loop |\n");
        printf("| [ m ]               | Mute/unmute                                               |\n");
        printf("| [ SPACE ]           | Pause / play                                              |\n");
        printf("| [ f ]               | Open a file dialogue (unimplemented)                      |\n");
        printf("| [ / ]               | Search the song list with regex                           |\n");
        printf("| [ n ]               | Search for next match                                     |\n");
        printf("| [ N ]               | Search for previous match                                 |\n");
        printf("| [ d ]               | Delete song list                                          |\n");
        printf("| [ g ]               | Jump to first song                                        |\n");
        printf("| [ G ]               | Jump to last song                                         |\n");
        printf("| [ ! ]               | Remove duplicate tracks from playlist                     |\n");
        printf("| [ ENTER ]           | Play song from song list                                  |\n");
        printf("| [ CTRL+l ]          | Redraw screen (if you resize the screen, run this)        |\n");
        printf("| [ CTRL+q ]          | Quit                                                      |\n");
        printf("| [ CTRL+s ]          | Save song list                                            |\n");
        printf("| [ 1..=9 ]           | Select song list                                          |\n");
        exit(0);
}

int main(int argc, char **argv) {
        --argc, ++argv;
        clap_init(argc, argv);

        Str_Array dirs = dyn_array_empty(Str_Array);

        Clap_Arg arg = {0};
        while (clap_next(&arg)) {
                if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_HELP && arg.eq) {
                        show_help_for_flag(arg.eq);
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_HELP) && arg.eq) {
                        show_help_for_flag(arg.eq);
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_HELP) {
                        usage();
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_VERSION) {
                        version();
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_RECURSIVE) {
                        g_config.flags |= FT_RECURSIVE;
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_CLR_SAVED_SONGS) {
                        g_config.flags |= FT_CLR_SAVED_SONGS;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_VERSION)) {
                        version();
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_HELP)) {
                        usage();
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_NOTIF)) {
                        g_config.flags |= FT_NOTIF;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_RECURSIVE)) {
                        g_config.flags |= FT_RECURSIVE;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_CLR_SAVED_SONGS)) {
                        g_config.flags |= FT_CLR_SAVED_SONGS;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_SHOW_SAVES)) {
                        g_config.flags |= FT_SHOW_SAVES;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_DISABLE_PLAYER_LOGO)) {
                        g_config.flags |= FT_DISABLE_PLAYER_LOGO;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_VOLUME)) {
                        if (!arg.eq)              err("--volume expects a value after equals (=)\n");
                        if (!str_isdigit(arg.eq)) err_wargs("--volume expects a number, not `%s`\n", arg.eq);
                        int v = atoi(arg.eq);
                        if (v < 0 || v > 128)     err_wargs("volume level %d is out of range of [0..=128]", v);
                        g_config.volume = v;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_PLAYLIST)) {
                        if (!arg.eq)              err("--playlist expects a value after equals (=)\n");
                        if (!str_isdigit(arg.eq)) err_wargs("--playlist expects a number, not `%s`\n", arg.eq);
                        g_config.playlist = atoi(arg.eq);
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_HISTORY_SZ)) {
                        if (!arg.eq)              err("--history-sz expects a value after equals (=)\n");
                        if (!str_isdigit(arg.eq)) err_wargs("--history-sz expects a number, not `%s`\n", arg.eq);
                        g_config.history_sz = atoi(arg.eq);
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_CONTROLS)) {
                        controls();
                } else if (arg.hyphc > 0) {
                        err_wargs("invalid flag: %s", arg.start);
                } else {
                        dyn_array_append(dirs, arg.start);
                }
        }

        if (g_config.flags & FT_CLR_SAVED_SONGS) {
                io_clear_config_file();
        }

        Playlist_Array playlists = io_read_config_file();
        Playlist_Array cli_playlists = io_flatten_dirs(&dirs);

        // If the user just wants to clear the saved songs
        // and don't provide any music to open.
        if ((g_config.flags & FT_CLR_SAVED_SONGS) && cli_playlists.len == 0) {
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
