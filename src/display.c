#include <assert.h>
#include <stdlib.h>
#include <time.h>

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
        int sel_fst_song;
        int paused;
        Music_Adv_Type mat;
        ssize_t currently_playing_index;
        Mix_Music *current_music;
        Uint64 start_ticks;      // Time when song started
        Uint64 paused_ticks;     // Accumulated paused time
        Uint64 pause_start;      // Time when pause began
} ctx = {
        .songfps = NULL,
        .history_idxs = {0},
        .songnames = {0},
        .sel_songfps_index = 0,
        .sel_fst_song = 0,
        .paused = 0,
        .mat = MAT_NORMAL,
        .currently_playing_index = -1,
        .current_music = NULL,
        .start_ticks = 0,
        .paused_ticks = 0,
        .pause_start = 0,
};

static WINDOW *left_win;  // Window for song list
static WINDOW *right_win; // Window for currently playing info

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

static void handle_key_down(void) {
        if (ctx.sel_songfps_index < ctx.songfps->len - 1) {
                ctx.sel_songfps_index++;
        } else {
                ctx.sel_songfps_index = 0;
        }
}

static void handle_key_up(void) {
        if (ctx.sel_songfps_index > 0) {
                ctx.sel_songfps_index--;
        } else {
                ctx.sel_songfps_index = ctx.songfps->len-1;
        }
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

        // Calculate dimensions
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

                mvwprintw(right_win, 4, 1, ctx.paused ? "Paused" : "Elapsed: [%s]", time_str);

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

        // Display song list in left window (inside borders)
        int max_y, max_x;
        getmaxyx(left_win, max_y, max_x);
        for (size_t i = 0; i < ctx.songfps->len && i < (size_t)(max_y - 2); ++i) {
                if (i == ctx.sel_songfps_index) {
                        wattron(left_win, A_REVERSE);
                }
                // Print at x=1 to avoid left border, truncate to fit inside right border
                mvwprintw(left_win, i + 1, 1, "%.*s", max_x - 2, ctx.songnames.data[i]);
                if (i == ctx.sel_songfps_index) {
                        wattroff(left_win, A_REVERSE);
                }
                if (!ctx.paused && i == ctx.currently_playing_index) {
                        const char *equalizer_frames[] = {"|", "/", "-", "\\"};
                        int frame_count = sizeof(equalizer_frames) / sizeof(equalizer_frames[0]);
                        int frame = (SDL_GetTicks() / 200) % frame_count; // Cycle every 200ms
                        mvwprintw(left_win, i+1, strlen(ctx.songnames.data[i]) + 2, "%s", equalizer_frames[frame]);
                }
                if (!ctx.paused && i == ctx.currently_playing_index) {
                }
        }

        // Refresh both windows
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
                ctx.currently_playing_index = ctx.sel_songfps_index = (size_t)rand()%ctx.songfps->len;
                start_song();
                dyn_array_append(ctx.history_idxs, ctx.currently_playing_index);
        }
}

static void handle_next_song(void) {
        if (ctx.currently_playing_index == -1 || !ctx.current_music || !ctx.sel_fst_song) {
                return;
        }
        Mix_HaltMusic();
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
                ctx.sel_songfps_index = ctx.currently_playing_index = dyn_array_at(ctx.history_idxs, ctx.history_idxs.len-1);
                start_song();
        } else if (ctx.history_idxs.len > 1) {
                ctx.sel_songfps_index = ctx.currently_playing_index = dyn_array_at(ctx.history_idxs, ctx.history_idxs.len-2);
                start_song();
                dyn_array_rm_at(ctx.history_idxs, ctx.history_idxs.len-1);
        }
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
                case KEY_LEFT: {
                        handle_key_left();
                } break;
                case KEY_RIGHT: {
                        handle_key_right();
                } break;
                case SPACE: {
                        pause_audio();
                } break;
                case 'a': {
                        handle_adv_type();
                } break;
                case '.':
                case '>': {
                        handle_next_song();
                } break;
                case ',':
                case '<': {
                        handle_prev_song();
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
