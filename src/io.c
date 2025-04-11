#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include "io.h"
#include "dyn_array.h"

static int is_music_f(const char *fp) {
        size_t last = 0;
        for (size_t i = 0; fp[i]; ++i) {
                if (fp[i] == '.') {
                        last = i;
                }
        }
        char buf[256] = {0};
        (void)strncpy(buf, fp + last + 1, strlen(fp + last + 1));
        return !strcmp(buf, "wav") || !strcmp(buf, "ogg") || !strcmp(buf, "mp3");
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
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                        continue;

                // Build full path to file
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

                struct stat st;
                if (stat(path, &st) == -1) {
                        perror("stat");
                        continue;
                }

                // Check if it's a regular file
                if (S_ISREG(st.st_mode) && is_music_f(entry->d_name)) {
                        dyn_array_append(*arr, strdup(path));
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
