#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include "ampire-io.h"
#include "ds/array.h"
#include "dyn_array.h"
#include "ampire-flag.h"

static int is_music_f(const char *fp) {
        size_t last = 0;
        for (size_t i = 0; fp[i]; ++i) {
                if (fp[i] == '.') {
                        last = i;
                }
        }
        char buf[256] = {0};
        (void)strncpy(buf, fp+last+1, strlen(fp+last+1));
        return !strcmp(buf, "wav") || !strcmp(buf, "ogg") || !strcmp(buf, "mp3") || !strcmp(buf, "opus");
}

static void walk(const char *directory, Str_Array *arr) {
        DIR *dir = opendir(directory);
        if (dir == NULL) {
                perror("opendir");
                return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
                // Skip "." and ".."
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                }

                // Build full path to file
                char path[1024] = {0};
                snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

                struct stat st;
                if (stat(path, &st) == -1) {
                        perror("stat");
                        continue;
                }

                // Check if it's a regular file and a music file.
                if (S_ISREG(st.st_mode) && is_music_f(entry->d_name)) {
                        dyn_array_append(*arr, strdup(path));
                } else if (S_ISDIR(st.st_mode) && (g_flags & FT_RECURSIVE)) {
                        walk(path, arr);
                }
        }

        closedir(dir);
}

Str_Array io_flatten_dirs(const Str_Array *dirs) {
        Str_Array arr; dyn_array_init_type(arr);

        for (size_t i = 0; i < dirs->len; ++i) {
                walk(dirs->data[i], &arr);
        }

        return arr;
}

static char *get_config_fp(void) {
        char *buf = malloc(1024);
        memset(buf, '\0', 1024);
        const char *home = getenv("HOME");
        strcat(buf, home);
        strcat(buf, "/");
        strcat(buf, ".ampire");
        return buf;
}

#include <ncurses.h>

void io_write_to_config_file(const Str_Array *filepaths) {
        char *configfp = get_config_fp();
        FILE *f = fopen(configfp, "a");

        // Find the longest filepath length
        size_t max_filepath_len = 0;
        for (size_t i = 0; i < filepaths->len; ++i) {
                size_t len = strlen(filepaths->data[i]);
                if (len > max_filepath_len) {
                        max_filepath_len = len;
                }
        }

        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        int win_height = 10;
        // Add padding (e.g., 10 for "saving: " and some extra space)
        int win_width = max_filepath_len + 10;
        // Ensure window doesn't exceed screen width
        if (win_width > max_x - 2) {
                win_width = max_x - 2;
        }
        int start_y = (max_y - win_height) / 2; // Center vertically
        int start_x = (max_x - win_width) / 2;  // Center horizontally

        WINDOW *win = newwin(win_height, win_width, start_y, start_x);
        box(win, 0, 0);

        for (size_t i = 0; i < filepaths->len; ++i) {
                wmove(win, 1, 1);
                wclrtoeol(win); // Clear the line to the end
                mvwprintw(win, 1, 1, "saving: %s", filepaths->data[i]);
                wrefresh(win);
                fprintf(f, "%s\n", filepaths->data[i]);
        }

        delwin(win);
        free(configfp);
        fclose(f);
}

Str_Array io_read_config_file(void) {
        Str_Array filepaths; dyn_array_init_type(filepaths);

        char *configfp = get_config_fp();

        FILE *f = fopen(configfp, "r");
        if (!f) {
                f = fopen(configfp, "w");
                goto done;
        }

        char *line = NULL;
        size_t len = 0;
        ssize_t read = 0;
        while((read = getline(&line, &len, f)) != -1) {
                if (!strcmp(line, "\n")) continue;
                if (line[read-1] == '\n') {
                        line[read-1] = '\0';
                }
                dyn_array_append(filepaths, strdup(line));
        }

        free(line);
done:
        free(configfp);
        fclose(f);
        return filepaths;
}
