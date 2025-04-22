#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <regex.h>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <ncurses.h>

#include "tinyfiledialogs.h"
#include "display.h"
#include "flag.h"

#define Mix_GetError    SDL_GetError

#define CTRL(x) ((x) & 0x1F)
#define BACKSPACE 263
#define ESCAPE 27
#define ENTER 10
#define SPACE 32

typedef enum {
        MAT_NORMAL,
        MAT_SHUFFLE,
        MAT_LOOP,
} Music_Adv_Type;

static struct {
        const Str_Array *songfps;
        Str_Array songnames;
        Size_T_Array history_idxs;
        size_t sel_songfps_index;
        size_t scroll_offset;
        int sel_fst_song;
        int paused;
        Music_Adv_Type mat;
        ssize_t currently_playing_index;
        Mix_Music *current_music;
        Uint64 start_ticks;      // Time when song started
        Uint64 paused_ticks;     // Accumulated paused time
        Uint64 pause_start;      // Time when pause began
        char *prevsearch;
} ctx = {
        .songfps = NULL,
        .history_idxs = {0},
        .songnames = {0},
        .sel_songfps_index = 0,
        .scroll_offset = 0,
        .sel_fst_song = 0,
        .paused = 0,
        .mat = MAT_NORMAL,
        .currently_playing_index = -1,
        .current_music = NULL,
        .start_ticks = 0,
        .paused_ticks = 0,
        .pause_start = 0,
        .prevsearch = NULL,
};

static WINDOW *left_win;  // Window for song list
static WINDOW *right_win; // Window for currently playing info

static void pause_audio(void);

static void cleanup(void) {
        if (left_win) delwin(left_win);
        if (right_win) delwin(right_win);
        Mix_HookMusicFinished(NULL);
        Mix_HaltMusic();
        if (ctx.current_music) {
                Mix_FreeMusic(ctx.current_music);
                ctx.current_music = NULL;
        }
        Mix_CloseAudio();
        endwin();
        SDL_Quit();
}

int regex(const char *pattern, const char *s) {
        regex_t regex;
        int reti;

        reti = regcomp(&regex, pattern, REG_ICASE);
        if (reti) {
                perror("regex");
                return 0;
        }

        reti = regexec(&regex, s, 0, NULL, 0);

        regfree(&regex);

        if (!reti) return 1;
        else return 0;
}

static void adjust_scroll_offset(void) {
        if (ctx.songfps->len == 0) return;

        int max_y, max_x;
        getmaxyx(left_win, max_y, max_x);
        size_t visible_rows = max_y - 2; // Account for borders

        if (ctx.sel_songfps_index < ctx.scroll_offset) {
                // Selection is above visible area
                ctx.scroll_offset = ctx.sel_songfps_index;
        } else if (ctx.sel_songfps_index >= ctx.scroll_offset + visible_rows) {
                // Selection is below visible area
                ctx.scroll_offset = ctx.sel_songfps_index - visible_rows + 1;
        }
}

// Format time in seconds to MM:SS
static void format_time(int seconds, char *buf, size_t bufsize) {
        int min = seconds / 60;
        int sec = seconds % 60;
        snprintf(buf, bufsize, "%02d:%02d", min, sec);
}

static void play_music(const char *song) {
        assert(song);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
                exit(1);
        }

        // Audio format to minimize ALSA adjustments
        SDL_AudioSpec desired = {
                .freq = 44100,           // 44.1 kHz
                .format = SDL_AUDIO_S16,  // 16-bit signed audio
                .channels = 2,           // Stereo
        };

        // Redirect stderr to suppress ALSA messages
        FILE *orig_stderr = stderr;
        stderr = fopen("/dev/null", "w");
        if (!stderr) stderr = orig_stderr; // Fallback if /dev/null fails

        if (Mix_OpenAudio(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired) < 0) {
                fprintf(orig_stderr, "SDL_mixer initialization failed: %s\n", Mix_GetError());
                if (stderr != orig_stderr) fclose(stderr);
                stderr = orig_stderr;
                SDL_Quit();
                exit(1);
        }

        if (stderr != orig_stderr) fclose(stderr);
        stderr = orig_stderr;

        // Free previous music if exists
        if (ctx.current_music) {
                Mix_FreeMusic(ctx.current_music);
                ctx.current_music = NULL;
        }

        ctx.current_music = Mix_LoadMUS(song);
        if (!ctx.current_music) {
                fprintf(stderr, "Failed to load music '%s': %s\n", song, Mix_GetError());
                Mix_CloseAudio();
                SDL_Quit();
                exit(1);
        }

        // Play once to allow music_finished callback
        if (Mix_PlayMusic(ctx.current_music, 1) < 0) {
                fprintf(stderr, "Failed to play music: %s\n", Mix_GetError());
                Mix_FreeMusic(ctx.current_music);
                ctx.current_music = NULL;
                Mix_CloseAudio();
                SDL_Quit();
                exit(1);
        }
}

static void music_finished(void);

static void start_song(void) {
        if (ctx.paused) {
                pause_audio();
        }

        Mix_HookMusicFinished(music_finished);
        ctx.currently_playing_index = ctx.sel_songfps_index;
        play_music(ctx.songfps->data[ctx.sel_songfps_index]);
        ctx.paused = 0;

        if ((g_flags & FT_NONOTIF) == 0) {
                tinyfd_notifyPopup("[cmus]: Up Next", ctx.songnames.data[ctx.sel_songfps_index], "info");
        }

        ctx.start_ticks = SDL_GetTicks(); // Record start time
        ctx.paused_ticks = 0;
        ctx.pause_start = 0;

        ctx.sel_fst_song = 1;
}

static void music_finished(void) {
        size_t r = 0;
        if (ctx.mat == MAT_NORMAL) {
                r = (ctx.currently_playing_index + 1) % ctx.songfps->len;
        } else if (ctx.mat == MAT_SHUFFLE) {
                while ((r = (size_t)rand()%ctx.songfps->len) == ctx.currently_playing_index);
        } else if (ctx.mat == MAT_LOOP) {
                r = ctx.currently_playing_index;
        } else { assert(0); }
        ctx.currently_playing_index = ctx.sel_songfps_index = r;
        dyn_array_append(ctx.history_idxs, ctx.currently_playing_index);
        Mix_HaltMusic();
        start_song();
        adjust_scroll_offset();
}

static void pause_audio(void) {
        if (!ctx.sel_fst_song) return;
        ctx.paused = !ctx.paused;
        Mix_PauseAudio(ctx.paused);
        if (ctx.paused) {
                // Start of pause
                ctx.pause_start = SDL_GetTicks();
        } else {
                // Add pause duration
                ctx.paused_ticks += SDL_GetTicks() - ctx.pause_start;
                ctx.pause_start = 0;
        }
}

static void seek_music(double seconds) {
        if (ctx.currently_playing_index == -1 || !ctx.current_music || !ctx.sel_fst_song) {
                return;
        }

        // Position in seconds
        Uint64 current_ticks = SDL_GetTicks();
        Uint64 elapsed_ms = ctx.paused ? (ctx.pause_start - ctx.start_ticks - ctx.paused_ticks)
                : (current_ticks - ctx.start_ticks - ctx.paused_ticks);
        double current_position = elapsed_ms / 1000.;

        double new_position = current_position + seconds;

        if (new_position < 0) {
                new_position = 0;
        }

        // Update playback
        if (Mix_SetMusicPosition(new_position) < 0) {
                fprintf(stderr, "Failed to seek music: %s\n", Mix_GetError());
                return;
        }

        // Reflect the new position
        ctx.start_ticks = SDL_GetTicks() - (Uint64)(new_position * 1000) - ctx.paused_ticks;
}

static void handle_key_up(void) {
        if (ctx.songfps->len == 0) return;

        // Move selection up, wrapping to the end if at the top
        if (ctx.sel_songfps_index > 0) {
                ctx.sel_songfps_index--;
        } else {
                ctx.sel_songfps_index = ctx.songfps->len - 1;
        }

        // Adjust scroll offset
        adjust_scroll_offset();
}

static void handle_key_down(void) {
        if (ctx.songfps->len == 0) return;

        // Move selection down, wrapping to the start if at the end
        if (ctx.sel_songfps_index < ctx.songfps->len - 1) {
                ctx.sel_songfps_index++;
        } else {
                ctx.sel_songfps_index = 0;
        }

        // Adjust scroll offset
        adjust_scroll_offset();
}

static void handle_key_right(void) {
        seek_music(10.0);
}

static void handle_key_left(void) {
        seek_music(-10.0);
}

static void init_ncurses(void) {
        initscr();
        raw();
        keypad(stdscr, TRUE);
        noecho();
        curs_set(0);
        timeout(100);

        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        int half_width = max_x / 2;

        // Create left window (song list)
        left_win = newwin(max_y, half_width, 0, 0);
        if (!left_win) {
                endwin();
                fprintf(stderr, "Failed to create left window\n");
                exit(1);
        }

        // Create right window (currently playing info)
        right_win = newwin(max_y, max_x - half_width, 0, half_width);
        if (!right_win) {
                delwin(left_win);
                endwin();
                fprintf(stderr, "Failed to create right window\n");
                exit(1);
        }

        // Enable scrolling for the left window if needed
        scrollok(left_win, TRUE);
}

static void draw_currently_playing(void) {
        werase(right_win);
        box(right_win, 0, 0);
        int max_y, max_x;
        getmaxyx(right_win, max_y, max_x);
        // Display currently playing info in right window (inside borders)
        if (ctx.currently_playing_index != -1) {
                // "Now Playing" animation
                const char *base_text = "-=-=- Now Playing -=-=";
                int base_len = strlen(base_text); // 22 characters
                int display_width = max_x - 2; // Account for borders
                if (display_width > base_len) display_width = base_len; // Cap at base text length
                char display_text[display_width + 1];
                int frame = (SDL_GetTicks() / 200) % base_len; // Cycle every 200ms
                for (int i = 0; i < display_width; ++i) {
                        display_text[i] = base_text[(frame + i) % base_len];
                }
                display_text[display_width] = '\0';
                wattron(right_win, A_BOLD);
                mvwprintw(right_win, 1, 1, "%s", display_text);
                wattroff(right_win, A_BOLD);

                mvwprintw(right_win, 3, 1, "%.*s", max_x - 2, ctx.songnames.data[ctx.currently_playing_index]);

                Uint64 current_ticks = SDL_GetTicks();
                Uint64 elapsed_ms = ctx.paused ? (ctx.pause_start - ctx.start_ticks - ctx.paused_ticks)
                        : (current_ticks - ctx.start_ticks - ctx.paused_ticks);
                int time_played = elapsed_ms / 1000; // Convert to seconds

                char time_str[16];
                format_time(time_played, time_str, sizeof(time_str));

                if (ctx.paused) {
                        wattron(right_win, A_REVERSE | A_BLINK);
                        mvwprintw(right_win, 4, 1, "Paused");
                        wattroff(right_win, A_REVERSE | A_BLINK);
                } else {
                        mvwprintw(right_win, 4, 1, "Elapsed: [%s]", time_str);
                }

                mvwprintw(right_win, 5, 1, "Advance: %s",
                          ctx.mat == MAT_NORMAL ? "Normal" :
                          ctx.mat == MAT_SHUFFLE ? "Shuffle" : "Loop");

                if (ctx.history_idxs.len > 0) {
                        mvwprintw(right_win, 6, 1, "History");
                        size_t start = ctx.history_idxs.len > 5 ? ctx.history_idxs.len - 5 : 0;
                        if (start != 0) {
                                mvwprintw(right_win, 6, strlen("History")+1, " [...%d]", ctx.history_idxs.len-5);
                        }
                        for (size_t i = start, j = 0; i < ctx.history_idxs.len; ++i, ++j) {
                                mvwprintw(right_win, 7+j, 3, "%s", ctx.songnames.data[ctx.history_idxs.data[i]]);
                                if (!ctx.paused && i == ctx.history_idxs.len - 1) {
                                        const char *equalizer_frames[] = {"|   ", "||  ", "||| ", "||||"};
                                        int frame_count = sizeof(equalizer_frames) / sizeof(equalizer_frames[0]);
                                        int frame = (SDL_GetTicks() / 200) % frame_count; // Cycle every 200ms
                                        mvwprintw(right_win, 7+j, strlen(ctx.songnames.data[ctx.history_idxs.data[i]])+4, "%s", equalizer_frames[frame]);
                                }
                        }
                }
        } else {
                mvwprintw(right_win, 1, 1, "No Song Playing");
        }
        wrefresh(right_win);
}

static void draw_song_list(void) {
        werase(left_win);
        box(left_win, 0, 0);

        int max_y, max_x;
        getmaxyx(left_win, max_y, max_x);
        size_t visible_rows = max_y - 2; // Account for borders

        // Display songs starting from scroll_offset (for scrolling)
        for (size_t i = ctx.scroll_offset; i < ctx.songfps->len && i < ctx.scroll_offset + visible_rows; ++i) {
                size_t display_row = i - ctx.scroll_offset + 1; // Row relative to window (1 to avoid top border)
                if (i == ctx.sel_songfps_index) {
                        wattron(left_win, A_REVERSE);
                }
                // Print at x=1 to avoid left border, truncate to fit inside right border
                mvwprintw(left_win, display_row, 1, "%.*s", max_x - 2, ctx.songnames.data[i]);
                if (i == ctx.sel_songfps_index) {
                        wattroff(left_win, A_REVERSE);
                }
                if (!ctx.paused && i == ctx.currently_playing_index) {
                        const char *equalizer_frames[] = {"|", "/", "-", "\\"};
                        int frame_count = sizeof(equalizer_frames) / sizeof(equalizer_frames[0]);
                        int frame = (SDL_GetTicks() / 200) % frame_count; // Cycle every 200ms
                        mvwprintw(left_win, display_row, strlen(ctx.songnames.data[i]) + 2, "%s", equalizer_frames[frame]);
                }
        }

        wrefresh(left_win);
}

static void resize_windows(void) {
        endwin();
        refresh();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        int half_width = max_x / 2;

        if (left_win) delwin(left_win);
        if (right_win) delwin(right_win);

        left_win = newwin(max_y, half_width, 0, 0);
        right_win = newwin(max_y, max_x - half_width, 0, half_width);

        if (!left_win || !right_win) {
                endwin();
                fprintf(stderr, "Failed to recreate windows after resize\n");
                exit(1);
        }
        scrollok(left_win, TRUE);

        // Adjust scroll_offset to keep sel_songfps_index visible
        size_t visible_rows = max_y - 2;
        if (ctx.sel_songfps_index < ctx.scroll_offset) {
                ctx.scroll_offset = ctx.sel_songfps_index;
        } else if (ctx.sel_songfps_index >= ctx.scroll_offset + visible_rows) {
                ctx.scroll_offset = ctx.sel_songfps_index - visible_rows + 1;
        }
}

static void draw_windows(void) {
        draw_song_list();
        draw_currently_playing();
}

static char *get_song_name(char *path) {
        ssize_t sl = -1;
        for (size_t i = 0; path[i]; ++i) {
                if (path[i] == '/') sl = i;
        }
        return sl != -1 ? path+sl+1 : path;
}

static void handle_adv_type(void) {
        if (ctx.mat == MAT_NORMAL) { ctx.mat = MAT_SHUFFLE; }
        else if (ctx.mat == MAT_SHUFFLE) { ctx.mat = MAT_LOOP; }
        else if (ctx.mat == MAT_LOOP) { ctx.mat = MAT_NORMAL; }
        else { assert(0); }

        // Handle case where no first song has been selected.
        // This allows to just play a random song without
        // selecting one first.
        if (!ctx.sel_fst_song) {
                ctx.sel_fst_song = 1;
                ctx.currently_playing_index = ctx.sel_songfps_index = (size_t)rand() % ctx.songfps->len;
                start_song();
                dyn_array_append(ctx.history_idxs, ctx.currently_playing_index);
                adjust_scroll_offset();
        }
}

static void handle_next_song(void) {
        if (ctx.currently_playing_index == -1 || !ctx.current_music || !ctx.sel_fst_song) {
                return;
        }

        // Select next song based on playback mode
        size_t r = 0;
        if (ctx.mat == MAT_NORMAL) {
                r = (ctx.currently_playing_index + 1) % ctx.songfps->len;
        } else if (ctx.mat == MAT_SHUFFLE) {
                while ((r = (size_t)rand() % ctx.songfps->len) == ctx.currently_playing_index);
        } else if (ctx.mat == MAT_LOOP) {
                r = ctx.currently_playing_index;
        } else {
                assert(0);
        }

        ctx.currently_playing_index = ctx.sel_songfps_index = r;

        Mix_HaltMusic();
        //start_song();

        // Adjust scroll offset to keep selection visible
        adjust_scroll_offset();
}

static void handle_prev_song(void) {
        if (ctx.currently_playing_index == -1 || !ctx.current_music || !ctx.sel_fst_song) {
                return;
        }

        Uint64 current_ticks = SDL_GetTicks();
        Uint64 elapsed_ms = ctx.paused ? (ctx.pause_start - ctx.start_ticks - ctx.paused_ticks)
                : (current_ticks - ctx.start_ticks - ctx.paused_ticks);
        int time_played = elapsed_ms / 1000;

        if (time_played > 1 || ctx.history_idxs.len <= 1) {
                // Restart current song
                ctx.sel_songfps_index = ctx.currently_playing_index = dyn_array_at(ctx.history_idxs, ctx.history_idxs.len - 1);
                start_song();
        } else if (ctx.history_idxs.len > 1) {
                // Play previous song from history
                ctx.sel_songfps_index = ctx.currently_playing_index = dyn_array_at(ctx.history_idxs, ctx.history_idxs.len - 2);
                start_song();
                dyn_array_rm_at(ctx.history_idxs, ctx.history_idxs.len - 1);
        }

        adjust_scroll_offset();
}

static char *get_search(void) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Define dimensions (3 rows x 30 columns, including borders)
        int win_height = 3;
        int win_width = 30;
        int start_y = (max_y - win_height) / 2; // Center vertically
        int start_x = (max_x - win_width) / 2;  // Center horizontally

        WINDOW *search_win = newwin(win_height, win_width, start_y, start_x);
        if (!search_win) {
                return NULL; // Failed to create window
        }

        box(search_win, 0, 0);

        // 27 chars max to fit inside borders with padding
        char input[28] = {0}; // +1 for null
        size_t input_len = 0;

        // Enable input settings
        keypad(search_win, TRUE);
        nodelay(search_win, FALSE); // Block for input (override timeout(100))
        echo();

        int ch;
        while (1) {
                mvwprintw(search_win, 1, 1, "%-*s", win_width - 2, input); // Pad to clear old text
                wrefresh(search_win);

                ch = wgetch(search_win);

                if (ch == ENTER) {
                        delwin(search_win);
                        noecho();
                        return input_len > 0 ? strdup(input) : strdup("");
                } else if (ch == ESCAPE || ch == CTRL('c')) {
                        // Cancel input
                        delwin(search_win);
                        noecho();
                        return NULL;
                } else if (ch == BACKSPACE || ch == 127) {
                        // Delete last character
                        if (input_len > 0) {
                                input[--input_len] = '\0';
                        }
                } else if (ch >= 32 && ch <= 126 && input_len < sizeof(input) - 1) {
                        // Append printable character
                        input[input_len++] = ch;
                        input[input_len] = '\0';
                }
                // Ignore other characters (e.g., arrow keys, function keys)
        }
}

static void handle_search(size_t startfrom, int rev, char *prevsearch) {
        char *query = NULL;
        if (prevsearch) {
                query = prevsearch;
        } else {
                query = get_search();
                if (!query) return;
                ctx.prevsearch = strdup(query);
        }
        ssize_t found = -1;
        if (rev) {
                for (int i = (int)startfrom; i >= 0; --i) {
                        if (regex(query, ctx.songnames.data[i])) {
                                found = i;
                                break;
                        }
                }
        } else {
                for (size_t i = startfrom; i < ctx.songnames.len; ++i) {
                        if (regex(query, ctx.songnames.data[i])) {
                                found = i;
                                break;
                        }
                }
        }
        if (found == -1) {
                tinyfd_notifyPopup("[cmus]: Could not find song", query, "warning");
                return;
        }

        ctx.sel_songfps_index = found;
        adjust_scroll_offset();
}

// Does not take ownership of songfps
void run(const Str_Array *songfps) {
        srand((unsigned int)time(NULL));

        ctx.songfps = songfps;

        dyn_array_init_type(ctx.songnames);
        dyn_array_init_type(ctx.history_idxs);

        for (size_t i = 0; i < songfps->len; ++i) {
                dyn_array_append(ctx.songnames,
                                 strdup(get_song_name(ctx.songfps->data[i])));
        }

        SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);

        atexit(cleanup);

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL could not initialize: %s\n", SDL_GetError());
                exit(1);
        }

        init_ncurses();

        int ch;
        while (1) {
                draw_windows();
                ch = getch();
                switch (ch) {
                case 'q':
                case 'Q':
                case CTRL('q'): goto done;
                case CTRL('l'): {
                        resize_windows();
                } break;
                case 'k':
                case KEY_UP: {
                        handle_key_up();
                } break;
                case 'j':
                case KEY_DOWN: {
                        handle_key_down();
                } break;
                case 'h':
                case KEY_LEFT: {
                        handle_key_left();
                } break;
                case 'l':
                case KEY_RIGHT: {
                        handle_key_right();
                } break;
                case SPACE: {
                        pause_audio();
                } break;
                case 'a': {
                        handle_adv_type();
                } break;
                case 'L':
                case '.':
                case '>': {
                        handle_next_song();
                } break;
                case 'H':
                case ',':
                case '<': {
                        handle_prev_song();
                } break;
                case 'n': {
                        if (ctx.sel_songfps_index < ctx.songnames.len-1) {
                                handle_search(ctx.sel_songfps_index+1, 0, ctx.prevsearch);
                        }
                } break;
                case 'N': {
                        if (ctx.sel_songfps_index > 0) {
                                handle_search(ctx.sel_songfps_index-1, 1, ctx.prevsearch);
                        }
                } break;
                case '/': {
                        handle_search(0, 0, NULL);
                } break;
                case 'f': {
                        char const *const filter_patterns[] = {"*.wav", "*.ogg", "*.mp3"};
                        char *path = tinyfd_openFileDialog("Select a directory", ".",
                                                           sizeof(filter_patterns)/sizeof(*filter_patterns),
                                                           filter_patterns,
                                                           "Music Files", 1);
                        assert(0 && "selecting files is unimplemented");
                } break;
                case ENTER: {
                        start_song();
                        dyn_array_append(ctx.history_idxs, ctx.currently_playing_index);
                } break;
                default: (void)0x0;
                }
        }
 done:
        for (size_t i = 0; i < ctx.songnames.len; ++i) {
                free(ctx.songnames.data[i]);
        }

        dyn_array_free(ctx.songnames);
}
