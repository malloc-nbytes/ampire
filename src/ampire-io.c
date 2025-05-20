#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

#include "ampire-io.h"
#include "ds/array.h"
#include "dyn_array.h"
#include "ampire-flag.h"
#include "ampire-ncurses-helpers.h"
#include "ampire-global.h"

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

static void walk(const char *path, Str_Array *arr) {
        // Convert input path to absolute path
        char *abs_path = realpath(path, NULL);
        if (!abs_path) {
                perror("realpath");
                display_temp_message("Failed to resolve absolute path!");
                return;
        }

        struct stat st;
        if (stat(abs_path, &st) == -1) {
                perror("stat");
                free(abs_path);
                return;
        }

        // Check if it's a regular file and a music file
        if (S_ISREG(st.st_mode) && is_music_f(abs_path)) {
                dyn_array_append(*arr, strdup(abs_path));
                free(abs_path);
                return;
        }

        // If it's not a directory, print error and return
        if (!S_ISDIR(st.st_mode)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Path %s is not a directory or a supported file format", abs_path);
                display_temp_message(msg);
                free(abs_path);
                return;
        }

        // Open directory
        DIR *dir = opendir(abs_path);
        if (!dir) {
                perror("opendir");
                free(abs_path);
                return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
                // Skip "." and ".."
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                }

                // Build full absolute subpath
                char subpath[PATH_MAX];
                snprintf(subpath, sizeof(subpath), "%s/%s", abs_path, entry->d_name);

                // Verify subpath is valid by getting its absolute path
                char *abs_subpath = realpath(subpath, NULL);
                if (!abs_subpath) {
                        perror("realpath");
                        continue; // Skip invalid paths
                }

                if (stat(abs_subpath, &st) == -1) {
                        perror("stat");
                        free(abs_subpath);
                        continue;
                }

                // Check if it's a regular file and a music file
                if (S_ISREG(st.st_mode) && is_music_f(entry->d_name)) {
                        dyn_array_append(*arr, strdup(abs_subpath));
                } else if (S_ISDIR(st.st_mode) && (g_config.flags & FT_RECURSIVE)) {
                        // Recursive call with absolute subpath
                        walk(abs_subpath, arr);
                }

                free(abs_subpath);
        }

        closedir(dir);
        free(abs_path);
}

Playlist_Array io_flatten_dirs(const Str_Array *dirs) {
        Playlist_Array pa = dyn_array_empty(Playlist_Array);

        for (size_t i = 0; i < dirs->len; ++i) {
                Str_Array arr = dyn_array_empty(Str_Array);
                walk(dirs->data[i], &arr);
                dyn_array_append(pa, ((Playlist) {
                        .songfps = arr,
                        .name = dirs->data[i],
                        .from_cli = 1,
                }));
        }

        return pa;
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

void io_write_to_config_file(const char *pname, const Str_Array *filepaths) {
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

        fprintf(f, "__ampire-playlist\n");
        fprintf(f, "%s\n", pname);
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

int io_replace_playlist_songs(const char *pname, const Str_Array *songfps) {
        if (!pname || !songfps) {
                display_temp_message("Invalid playlist name or song list!");
                return 0;
        }

        // Get config file path
        char *configfp = get_config_fp();
        if (!configfp) {
                display_temp_message("Failed to get config file path!");
                return 0;
        }

        // Open file for reading
        FILE *f = fopen(configfp, "r");
        if (!f) {
                perror("fopen");
                display_temp_message("Failed to open config file for reading!");
                free(configfp);
                return 0;
        }

        // Read all lines into a dynamic array
        Str_Array old_lines = dyn_array_empty(Str_Array);
        char *line = NULL;
        size_t len = 0;
        ssize_t read;

        while ((read = getline(&line, &len, f)) != -1) {
                if (read > 0 && line[read - 1] == '\n') {
                        line[read - 1] = '\0';
                        read--;
                }
                if (read == 0 || !strcmp(line, "")) {
                        continue;
                }
                char *copy = strdup(line);
                if (!copy) {
                        perror("strdup");
                        display_temp_message("Memory allocation failed!");
                        fclose(f);
                        free(line);
                        free(configfp);
                        dyn_array_free(old_lines);
                        return 0;
                }
                dyn_array_append(old_lines, copy);
        }

        if (ferror(f)) {
                perror("fread");
                display_temp_message("Error reading config file!");
                fclose(f);
                free(line);
                free(configfp);
                dyn_array_free(old_lines);
                return 0;
        }
        fclose(f);
        free(line);

        // Create new lines, replacing songs for the target playlist
        Str_Array new_lines = dyn_array_empty(Str_Array);
        int skip = 0; // Flag to skip existing songs in the target playlist
        int expect_name = 0; // Flag to expect playlist name after __ampire-playlist

        for (size_t i = 0; i < old_lines.len; i++) {
                char *current_line = old_lines.data[i];

                if (!strcmp(current_line, "__ampire-playlist")) {
                        skip = 0;
                        expect_name = 1;
                        dyn_array_append(new_lines, strdup(current_line));
                        continue;
                }

                if (expect_name) {
                        expect_name = 0;
                        dyn_array_append(new_lines, strdup(current_line));
                        if (!strcmp(current_line, pname)) {
                                skip = 1; // Start skipping existing songs
                                // Add new songs from songfps
                                for (size_t j = 0; j < songfps->len; j++) {
                                        char *song_copy = strdup(songfps->data[j]);
                                        if (!song_copy) {
                                                perror("strdup");
                                                display_temp_message("Memory allocation failed!");
                                                dyn_array_free(old_lines);
                                                dyn_array_free(new_lines);
                                                free(configfp);
                                                return 0;
                                        }
                                        dyn_array_append(new_lines, song_copy);
                                }
                        }
                        continue;
                }

                if (!skip) {
                        // Keep lines that aren't part of the target playlist
                        char *copy = strdup(current_line);
                        if (!copy) {
                                perror("strdup");
                                display_temp_message("Memory allocation failed!");
                                dyn_array_free(old_lines);
                                dyn_array_free(new_lines);
                                free(configfp);
                                return 0;
                        }
                        dyn_array_append(new_lines, copy);
                }
        }

        // Open file for writing
        f = fopen(configfp, "w");
        if (!f) {
                perror("fopen");
                display_temp_message("Failed to open config file for writing!");
                free(configfp);
                dyn_array_free(old_lines);
                dyn_array_free(new_lines);
                return 0;
        }

        // Write new lines
        for (size_t i = 0; i < new_lines.len; i++) {
                if (fprintf(f, "%s\n", new_lines.data[i]) < 0) {
                        perror("fprintf");
                        display_temp_message("Error writing to config file!");
                        fclose(f);
                        free(configfp);
                        dyn_array_free(old_lines);
                        dyn_array_free(new_lines);
                        return 0;
                }
        }

        if (ferror(f)) {
                perror("fwrite");
                display_temp_message("Error writing to config file!");
                fclose(f);
                free(configfp);
                dyn_array_free(old_lines);
                dyn_array_free(new_lines);
                return 0;
        }

        fclose(f);
        free(configfp);
        dyn_array_free(old_lines);
        dyn_array_free(new_lines);

        return 1;
}

void io_clear_config_file(void) {
        char *configfp = get_config_fp();
        FILE *f = fopen(configfp, "w");
        if (!f) {
                perror("fopen");
                exit(0);
        }
        printf("Cleared saved music at: %s\n", configfp);
        fclose(f);
        free(configfp);
}

Playlist_Array io_read_config_file(void) {
        Playlist_Array playlists; dyn_array_init_type(playlists);

        char *configfp = get_config_fp();

        FILE *f = fopen(configfp, "r");
        if (!f) {
                f = fopen(configfp, "w");
                goto done;
        }

        char *line = NULL;
        size_t len = 0;
        ssize_t read = 0;
        int wait_playlist_name = 0;
        ssize_t playlist_idx = -1;
        while((read = getline(&line, &len, f)) != -1) {
                if (!strcmp(line, "\n"))  continue;
                if (line[read-1] == '\n') line[read-1] = '\0';

                if (!strcmp(line, "__ampire-playlist")) {
                        wait_playlist_name = 1;
                } else if (wait_playlist_name) {
                        wait_playlist_name = 0;
                        Playlist p = {
                                .songfps = dyn_array_empty(Str_Array),
                                .name = strdup(line),
                                .from_cli = 0,
                        };
                        dyn_array_append(playlists, p);
                        ++playlist_idx;
                } else {
                        dyn_array_append(playlists.data[playlist_idx].songfps, strdup(line));
                }
        }

        if (g_config.flags & FT_SHOW_SAVES) {
            for (size_t i = 0; i < playlists.len; ++i) {
                    printf("Playlist: %s:\n", playlists.data[i].name);
                    for (size_t j = 0; j < playlists.data[i].songfps.len; ++j) {
                            printf("  load: %s\n", playlists.data[i].songfps.data[j]);
                    }
            }
            exit(0);
        }

        free(line);
done:
        free(configfp);
        fclose(f);
        return playlists;
}

int io_del_playlist(const char *pname) {
        if (!pname) {
                display_temp_message("No playlist name provided!");
                return 0;
        }

        // Confirm deletion with user
        char prompt[256];
        snprintf(prompt, sizeof(prompt), "Delete playlist '%s'?", pname);
        if (!prompt_yes_no(prompt)) {
                return 0;
        }

        // Get config file path
        char *configfp = get_config_fp();
        if (!configfp) {
                display_temp_message("Failed to get config file path!");
                return 0;
        }

        // Open file for reading
        FILE *f = fopen(configfp, "r");
        if (!f) {
                perror("fopen");
                display_temp_message("Failed to open config file for reading!");
                free(configfp);
                return 0;
        }

        // Read all lines into a dynamic array
        Str_Array old_lines = dyn_array_empty(Str_Array);
        char *line = NULL;
        size_t len = 0;
        ssize_t read;

        while ((read = getline(&line, &len, f)) != -1) {
                // Remove trailing newline if present
                if (read > 0 && line[read - 1] == '\n') {
                        line[read - 1] = '\0';
                        read--;
                }
                // Skip empty lines when storing
                if (read == 0 || !strcmp(line, "")) {
                        continue;
                }
                // Duplicate line for storage
                char *copy = strdup(line);
                if (!copy) {
                        perror("strdup");
                        display_temp_message("Memory allocation failed!");
                        fclose(f);
                        free(line);
                        free(configfp);
                        dyn_array_free(old_lines);
                        return 0;
                }
                dyn_array_append(old_lines, copy);
        }

        // Check for read errors
        if (ferror(f)) {
                perror("fread");
                display_temp_message("Error reading config file!");
                fclose(f);
                free(line);
                free(configfp);
                dyn_array_free(old_lines);
                return 0;
        }
        fclose(f);
        free(line);

        // Filter out the target playlist
        Str_Array new_lines = dyn_array_empty(Str_Array);
        int skip = 0; // Flag to skip lines in the target playlist
        int expect_name = 0; // Flag to expect playlist name after __ampire-playlist

        for (size_t i = 0; i < old_lines.len; i++) {
                char *current_line = old_lines.data[i];

                if (!strcmp(current_line, "__ampire-playlist")) {
                        // Start of a new playlist
                        skip = 0;
                        expect_name = 1;
                        dyn_array_append(new_lines, strdup(current_line));
                        continue;
                }

                if (expect_name) {
                        // This is the playlist name
                        expect_name = 0;
                        if (!strcmp(current_line, pname)) {
                                // Target playlist found; skip it and its file paths
                                skip = 1;
                                // Remove the __ampire-playlist marker for this playlist
                                dyn_array_rm_at(new_lines, new_lines.len - 1);
                        } else {
                                // Keep the playlist name
                                dyn_array_append(new_lines, strdup(current_line));
                        }
                        continue;
                }

                if (!skip) {
                        // Keep file paths or other lines not in the target playlist
                        dyn_array_append(new_lines, strdup(current_line));
                }
        }

        // Open file for writing
        f = fopen(configfp, "w");
        if (!f) {
                perror("fopen");
                display_temp_message("Failed to open config file for writing!");
                free(configfp);
                dyn_array_free(old_lines);
                dyn_array_free(new_lines);
                return 0;
        }

        // Write filtered lines
        for (size_t i = 0; i < new_lines.len; i++) {
                if (fprintf(f, "%s\n", new_lines.data[i]) < 0) {
                        perror("fprintf");
                        display_temp_message("Error writing to config file!");
                        fclose(f);
                        free(configfp);
                        dyn_array_free(old_lines);
                        dyn_array_free(new_lines);
                        return 0;
                }
        }

        // Check for write errors
        if (ferror(f)) {
                perror("fwrite");
                display_temp_message("Error writing to config file!");
                fclose(f);
                free(configfp);
                dyn_array_free(old_lines);
                dyn_array_free(new_lines);
                return 0;
        }

        fclose(f);
        free(configfp);

        dyn_array_free(old_lines);
        dyn_array_free(new_lines);

        return 1;
}
