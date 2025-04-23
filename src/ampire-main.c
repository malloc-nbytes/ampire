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
#define FLAG_2HY_HELP "help"
#define FLAG_2HY_NOTIF "notif"
#define FLAG_2HY_RECURSIVE "recursive"

size_t g_flags = 0x0;

void usage(void) {
        printf("Usage: ampire <dir> [options...]\n");
        printf("Options:\n");
        printf("    -%c, --%s         print this help message\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("        --%s     display notifications on song change\n", FLAG_2HY_NOTIF);
        printf("    -%c, --%s    enable recursive search for songs\n", FLAG_1HY_RECURSIVE, FLAG_2HY_RECURSIVE);
        exit(0);
}

int main(int argc, char **argv) {
        if (argc < 2) {
                usage();
        }
        --argc, ++argv;
        clap_init(argc, argv);

        Str_Array dirs; dyn_array_init_type(dirs);

        Clap_Arg arg = {0};
        while (clap_next(&arg)) {
                if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_HELP) {
                        usage();
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_RECURSIVE) {
                        g_flags |= FT_RECURSIVE;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_HELP)) {
                        usage();
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_NOTIF)) {
                        g_flags |= FT_NOTIF;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_RECURSIVE)) {
                        g_flags |= FT_RECURSIVE;
                } else if (arg.hyphc > 0) {
                        err_wargs("invalid flag: %s", arg.start);
                } else {
                        dyn_array_append(dirs, arg.start);
                }
        }

        Str_Array songfps = io_flatten_dirs(&dirs);

        run(&songfps);

        for (size_t i = 0; i < songfps.len; ++i) {
                free(songfps.data[i]);
        }
        dyn_array_free(songfps);
        dyn_array_free(dirs);

        return 0;
}
