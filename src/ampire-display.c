#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <regex.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <ncurses.h>

#include "tinyfiledialogs.h"
#include "ampire-display.h"
#include "ampire-flag.h"
#include "ampire-io.h"
#include "ampire-utils.h"
#include "ampire-ncurses-helpers.h"
#include "ampire-global.h"
#include "dyn_array.h"
#include "ds/strmap.h"
#include "ds/array.h"
#include "config.h"

#define Mix_GetError    SDL_GetError

typedef enum {
        MAT_NORMAL,
        MAT_SHUFFLE,
        MAT_LOOP,
} Music_Adv_Type;

typedef struct {
        size_t          uuid;
        Str_Array      *songfps;
        char           *pname;                   // Playlist name
        Str_Array       songnames;               // The stripped songname from the path
        Size_T_Array    history_idxs;
        size_t          sel_songfps_index;
        size_t          scroll_offset;
        size_t          playlist_scroll_offset;
        int             sel_fst_song;            // We have selected at least one song
        int             paused;                  // Is the song currently paused
        Music_Adv_Type  mat;                     // What happens after the song ends, normal, shuffle, or loop
        ssize_t         currently_playing_index; // Currently playing music index into `songfps`
        Mix_Music      *current_music;           // Currently playing music
        Uint64          start_ticks;             // Time when song started
        Uint64          paused_ticks;            // Accumulated paused time
        Uint64          pause_start;             // Time when pause began
        char           *prevsearch;              // Previous search used for [n] and [N]
        int             numtracks;               // The number of songs in the playlist
        int             upnext_idx;              // The index of the next song to be played
        int             playlist_modified;       // Has the current playlist been modified?
        int             playlist_saved;
        Size_T_Array    queue;                   // The 'next-up' songs
        Size_T_Array    shuffle_queue;           // Queue for shuffling songs
} Ctx;

static int                   g_volume               = 68;
static int                   g_last_volume          = 0;
static int                   g_scrn_width           = 0;
static int                   g_scrn_height          = 0;
static volatile sig_atomic_t g_oneshot_keep_running = 1;
static volatile sig_atomic_t g_resize_flag          = 0;
static volatile bool         g_need_next_song       = false;
static int                   g_playlist_page        = 0;
static int                   g_total_playlist_pages = 0;
static int                   g_original_playlist_sz = 0;

DYN_ARRAY_TYPE(Ctx, Ctx_Array);

// Used for SDL function(s) with sig (*)(void) but
// we still need to use the context. This should
// be *always* set whenever the context switches!
static Ctx *g_ctx = NULL;

static WINDOW *left_win;  // Window for song list
static WINDOW *right_win; // Window for currently playing info

static void pause_audio(Ctx *ctx);
static void shuffle_song_idxs(Ctx *ctx);

static void cleanup(void) {
        if ((g_config.flags & FT_ONESHOT) == 0) {
                // Do not need to clean up ncurses
                // if --oneshot is used as ncurses
                // is not initialized with this flag.
                if (left_win) delwin(left_win);
                if (right_win) delwin(right_win);
                endwin();
        }
        Mix_HookMusicFinished(NULL);
        Mix_HaltMusic();
        if (g_ctx && g_ctx->current_music) {
                Mix_FreeMusic(g_ctx->current_music);
                g_ctx->current_music = NULL;
        }
        Mix_CloseAudio();
        SDL_Quit();
}

volatile static int getrand(Ctx *ctx) {
        int r = 0;
        if (ctx->songfps->len == 1) return 0;
        while ((r = rand()%ctx->songfps->len) == ctx->currently_playing_index);
        return r;
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

static void adjust_scroll_offset(Ctx *ctx) {
        if (ctx->songfps->len == 0) return;

        int max_y, max_x;
        getmaxyx(left_win, max_y, max_x);
        size_t visible_rows = max_y - 2; // Account for borders

        if (ctx->sel_songfps_index < ctx->scroll_offset) {
                // Selection is above visible area
                ctx->scroll_offset = ctx->sel_songfps_index;
        } else if (ctx->sel_songfps_index >= ctx->scroll_offset + visible_rows) {
                // Selection is below visible area
                ctx->scroll_offset = ctx->sel_songfps_index - visible_rows + 1;
        }
}

// Format time in seconds to MM:SS
static void format_time(int seconds, char *buf, size_t bufsize) {
        int min = seconds / 60;
        int sec = seconds % 60;
        snprintf(buf, bufsize, "%02d:%02d", min, sec);
}

void handle_upnext(Ctx *ctx) {
        size_t r = 0;
        if (ctx->queue.len > 0) {
                r = ctx->queue.data[0];
        }
        else if (ctx->mat == MAT_NORMAL) {
                r = (ctx->currently_playing_index + 1) % ctx->songfps->len;
        } else if (ctx->mat == MAT_SHUFFLE) {
                //r = getrand(ctx);
                if (ctx->shuffle_queue.len == 0) {
                        shuffle_song_idxs(ctx);
                }
                r = ctx->shuffle_queue.data[0];
                dyn_array_rm_at(ctx->shuffle_queue, 0);
        } else {
                r = ctx->currently_playing_index;
        }
        ctx->upnext_idx = r;
}

static void play_music(Ctx *ctx, const char *song) {
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
        if (ctx->current_music) {
                Mix_FreeMusic(ctx->current_music);
                ctx->current_music = NULL;
        }

        ctx->current_music = Mix_LoadMUS(song);
        if (!ctx->current_music) {
                fprintf(stderr, "Failed to load music '%s': %s\n", song, Mix_GetError());
                Mix_CloseAudio();
                SDL_Quit();
                exit(1);
        }

        Mix_VolumeMusic(g_volume);

        // Play once to allow music_finished callback
        if (Mix_PlayMusic(ctx->current_music, 1) < 0) {
                fprintf(stderr, "Failed to play music: %s\n", Mix_GetError());
                Mix_FreeMusic(ctx->current_music);
                ctx->current_music = NULL;
                Mix_CloseAudio();
                SDL_Quit();
                exit(1);
        }
}

static void music_finished(void);

static void start_song(Ctx *ctx) {
        if (ctx->songfps->len == 0) {
                return;
        }

        if (ctx->paused) {
                pause_audio(ctx);
        }

        Mix_HookMusicFinished(music_finished);
        ctx->currently_playing_index = ctx->sel_songfps_index;
        play_music(ctx, ctx->songfps->data[ctx->sel_songfps_index]);
        ctx->paused = 0;

        if (g_config.flags & FT_NOTIF) {
                tinyfd_notifyPopup("[ampire]: Up Next", ctx->songnames.data[ctx->sel_songfps_index], "info");
        }

        ctx->start_ticks = SDL_GetTicks(); // Record start time
        ctx->paused_ticks = 0;
        ctx->pause_start = 0;

        ctx->sel_fst_song = 1;

        handle_upnext(ctx);
}

static void music_finished(void) {
        /* assert(g_ctx); */
        /* g_ctx->currently_playing_index = g_ctx->sel_songfps_index = g_ctx->upnext_idx; */
        /* dyn_array_append(g_ctx->history_idxs, g_ctx->currently_playing_index); */
        /* if (g_ctx->queue.len > 0) { */
        /*         dyn_array_rm_at(g_ctx->queue, 0); */
        /* } */
        /* Mix_HaltMusic(); */
        /* start_song(g_ctx); */
        /* adjust_scroll_offset(g_ctx); */

        assert(g_ctx);

        g_ctx->currently_playing_index = g_ctx->sel_songfps_index = g_ctx->upnext_idx;
        dyn_array_append(g_ctx->history_idxs, g_ctx->currently_playing_index);

        if (g_ctx->queue.len > 0) {
                dyn_array_rm_at(g_ctx->queue, 0);
        }

        Mix_HaltMusic();
        g_need_next_song = true;
}

static void pause_audio(Ctx *ctx) {
        if (!ctx->sel_fst_song) return;
        ctx->paused = !ctx->paused;
        Mix_PauseAudio(ctx->paused);
        if (ctx->paused) {
                ctx->pause_start = SDL_GetTicks();
        } else {
                // Add pause duration
                ctx->paused_ticks += SDL_GetTicks() - ctx->pause_start;
                ctx->pause_start = 0;
        }
}

static void seek_music(Ctx *ctx, double seconds) {
        if (ctx->currently_playing_index == -1 || !ctx->current_music || !ctx->sel_fst_song) {
                return;
        }

        // Position in seconds
        Uint64 current_ticks = SDL_GetTicks();
        Uint64 elapsed_ms = ctx->paused ? (ctx->pause_start - ctx->start_ticks - ctx->paused_ticks)
                : (current_ticks - ctx->start_ticks - ctx->paused_ticks);
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

        // Show the new position
        ctx->start_ticks = SDL_GetTicks() - (Uint64)(new_position * 1000) - ctx->paused_ticks;
}

static void handle_key_up(Ctx *ctx) {
        if (!ctx) return;

        if (ctx->songfps->len == 0) return;

        if (ctx->sel_songfps_index > 0) {
                ctx->sel_songfps_index--;
        } else {
                ctx->sel_songfps_index = ctx->songfps->len - 1;
        }

        adjust_scroll_offset(ctx);
}

static void handle_key_down(Ctx *ctx) {
        if (!ctx) return;

        if (ctx->songfps->len == 0) return;

        if (ctx->sel_songfps_index < ctx->songfps->len - 1) {
                ctx->sel_songfps_index++;
        } else {
                ctx->sel_songfps_index = 0;
        }

        adjust_scroll_offset(ctx);
}

static void handle_key_right(Ctx *ctx) {
        if (!ctx) return;
        seek_music(ctx, 10.0);
}

static void handle_key_left(Ctx *ctx) {
        if (!ctx) return;
        seek_music(ctx, -10.0);
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

        g_scrn_width = max_x;
        g_scrn_height = max_y;

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

        // Enable scrolling for the left window
        scrollok(left_win, TRUE);
}

int iota(int forward) {
        assert(forward == -1 || forward >= 0);
        static int __f = 0;
        int res = __f;
        if (forward == -1) {
                __f = 1;
        } else {
                __f += forward;
        }
        return res;
}

static void draw_currently_playing(Ctx *ctx, Ctx_Array *ctxs) {
        (void)iota(-1);

        werase(right_win);
        box(right_win, 0, 0);
        int max_y, max_x;
        getmaxyx(right_win, max_y, max_x);

        if (!(g_config.flags & FT_DISABLE_PLAYER_LOGO) && max_x > 50 && max_y > 35) {
                mvwprintw(right_win, iota(1), 1, "   (");
                mvwprintw(right_win, iota(1), 1, "   )\\       )          (   (      (");
                mvwprintw(right_win, iota(1), 1, "((((_)(    (     `  )  )\\  )(    ))\\");
                mvwprintw(right_win, iota(1), 1, " )\\ _ )\\   )\\  ' /(/( ((_)(()\\  /((_)");
                mvwprintw(right_win, iota(1), 1, " (_)_\\(_)_((_)) ((_)_\\ (_) ((_)(_))");
                mvwprintw(right_win, iota(1), 1, "  / _ \\ | '  \\()| '_ \\)| || '_|/ -_)");
                mvwprintw(right_win, iota(1), 1, " /_/ \\_\\|_|_|_| | .__/ |_||_|  \\___| v" VERSION);
                mvwprintw(right_win, iota(2), 1, "                |_|");
        }

        for (size_t i = g_playlist_page*g_config.playlist_sz;
             i < ctxs->len && i < g_playlist_page*g_config.playlist_sz + g_config.playlist_sz; ++i) {
                if (i == ctx->uuid) {
                        wattron(right_win, A_REVERSE);
                }
                mvwprintw(right_win, iota(0), 1, "[%s]", ctxs->data[i].pname);
                if (ctxs->data[i].playlist_modified) {
                        const size_t N = strlen("[] ") + strlen(ctxs->data[i].pname);
                        mvwprintw(right_win, iota(0), N, " (modified)");
                }
                (void)iota(1);
                if (i == ctx->uuid) {
                        wattroff(right_win, A_REVERSE);
                }
        }

        mvwprintw(right_win, iota(0), 1, "<[%d", g_playlist_page);
        mvwprintw(right_win, iota(1), 5, "%d]>", g_total_playlist_pages-1-g_playlist_page);

        (void)iota(1);

        // Display currently playing info in right window
        if (ctx && ctx->currently_playing_index != -1) {
                // ... (unchanged code for "Now Playing", playlist info, etc.)
                // Ensure iota is incremented correctly to avoid overlap
                const char *base_text = !ctx->paused ? "-=-=- Now Playing -=-=" : "-=-=- PAUSED -=-=";
                int base_len = strlen(base_text);
                int display_width = max_x - 2;
                if (display_width < 0) display_width = 0;
                if (display_width > base_len) display_width = base_len;
                char display_text[display_width + 1];
                int frame = (SDL_GetTicks() / 200) % base_len;
                for (int i = 0; i < display_width; ++i) {
                        display_text[i] = base_text[(frame + i) % base_len];
                }
                display_text[display_width] = '\0';
                wattron(right_win, A_BOLD);
                mvwprintw(right_win, iota(2), 1, "%s", display_text);
                wattroff(right_win, A_BOLD);

                mvwprintw(right_win, iota(1), 1, "> Playlist: %s (%d tracks)", ctx->pname, ctx->numtracks);
                mvwprintw(right_win, iota(1), 1, "> Current: %.*s", max_x - 2, shstr(ctx->songnames.data[ctx->currently_playing_index], max_x/2));

                Uint64 current_ticks = SDL_GetTicks();
                Uint64 elapsed_ms = ctx->paused ? (ctx->pause_start - ctx->start_ticks - ctx->paused_ticks)
                        : (current_ticks - ctx->start_ticks - ctx->paused_ticks);
                int time_played = elapsed_ms / 1000;

                char time_str[16];
                format_time(time_played, time_str, sizeof(time_str));

                if (ctx->paused) {
                        wattron(right_win, A_REVERSE | A_BLINK);
                        mvwprintw(right_win, iota(1), 1, "Paused");
                        wattroff(right_win, A_REVERSE | A_BLINK);
                } else {
                        mvwprintw(right_win, iota(1), 1, "> Elapsed: %s", time_str);
                }

                (void)iota(1);

                mvwprintw(right_win, iota(0), 1, "Mode: ");
                mvwprintw(right_win, iota(1), strlen("Mode: ")+1, "%s",
                          ctx->mat == MAT_NORMAL ? "Normal" : ctx->mat == MAT_SHUFFLE ? "Shuffle" : "Loop");

                int total_blocks = 12;
                int filled_blocks = (g_volume * total_blocks + MIX_MAX_VOLUME) / MIX_MAX_VOLUME;
                mvwprintw(right_win, iota(0), 1, "Volume: [");
                if (g_volume == 0) {
                        mvwprintw(right_win, iota(1), strlen("Volume: [") + 1, "MUTE");
                } else {
                        for (int i = 0; i < total_blocks+1; i++) {
                                if (i < filled_blocks) {
                                        if (i+1 == filled_blocks) { waddch(right_win, '|'); }
                                        else { waddch(right_win, '*'); }
                                } else {
                                        waddch(right_win, '.');
                                }
                        }
                }
                waddch(right_win, ']');
                if (g_volume > 0) {
                        mvwprintw(right_win, iota(1), total_blocks + strlen("Volume: [") + 4, "%d%%", (g_volume*100)/MIX_MAX_VOLUME);
                }

                if (ctx->history_idxs.len > 0) {
                        int histsz = g_config.history_sz;
                        int available_lines = max_y - iota(0) - 5;
                        if (available_lines < histsz) {
                                histsz = available_lines > 0 ? available_lines : 0;
                        }
                        histsz = histsz > g_config.history_sz ? g_config.history_sz : histsz;

                        size_t start = ctx->history_idxs.len > histsz ? ctx->history_idxs.len - histsz : 0;
                        mvwprintw(right_win, iota(0), 1, "History");
                        if (start == 0) {
                                (void)iota(1);
                        }
                        if (start != 0) {
                                mvwprintw(right_win, iota(1), strlen("History")+1, " [%zu hidden]", ctx->history_idxs.len-histsz);
                        }
                        for (size_t i = start, j = 0; i < ctx->history_idxs.len; ++i, ++j) {
                                if (i != ctx->history_idxs.len - 1) {
                                        wattron(right_win, A_DIM);
                                } else {
                                        wattron(right_win, A_BOLD);
                                }
                                mvwprintw(right_win, iota(0)+j, 3, "| %s", shstr(ctx->songnames.data[ctx->history_idxs.data[i]], max_x/2));
                                if (!ctx->paused && i == ctx->history_idxs.len - 1) {
                                        const char *equalizer_frames[] = {"|   ", "||  ", "||| ", "||||"};
                                        int frame_count = sizeof(equalizer_frames) / sizeof(equalizer_frames[0]);
                                        int frame = (SDL_GetTicks() / 200) % frame_count;
                                        int N = strlen(ctx->songnames.data[ctx->history_idxs.data[i]]);
                                        int loc = N > max_x/2 ? max_x/2 + 3 : N;
                                        mvwprintw(right_win, iota(0)+j, loc+6, "%s", equalizer_frames[frame]);
                                }
                                if (i != ctx->history_idxs.len - 1) {
                                        wattroff(right_win, A_DIM);
                                } else {
                                        wattroff(right_win, A_BOLD);
                                }
                        }
                        iota(ctx->history_idxs.len >= histsz ? histsz : ctx->history_idxs.len);
                        mvwprintw(right_win, iota(0), 1, "Up Next");
                        mvwprintw(right_win, iota(1), strlen("Up Next")+1, ": [%s]", shstr(ctx->songnames.data[ctx->upnext_idx], max_x/2));
                }
        } else {
                if (ctx) {
                        mvwprintw(right_win, iota(1), 1, "Playlist: %s", ctx->pname);
                }
                mvwprintw(right_win, iota(1), 1, "No Song Playing");
        }

        wrefresh(right_win);
}

static void draw_song_list(Ctx *ctx) {
        werase(left_win);
        box(left_win, 0, 0);

        int max_y, max_x;
        getmaxyx(left_win, max_y, max_x);
        size_t visible_rows = max_y - 2; // Account for borders

        if (!ctx) {
                mvwprintw(left_win, 1, 1, "No Music! Opening Ampire with no music is currently unimplemented.");
                mvwprintw(left_win, 2, 1, "Provide paths to the files from the command line i.e.,.");
                mvwprintw(left_win, 3, 4, "ampire /home/<user>/Music/electronic -r /home/<user>/Music/");
                mvwprintw(left_win, 4, 1, "Then save those as playlists with [CTRL] + s.");
                mvwprintw(left_win, 6, 1, "See ampire --help for more information.");
        }

        if (ctx) {
                // Display songs starting from scroll_offset (for scrolling)
                for (size_t i = ctx->scroll_offset; i < ctx->songfps->len && i < ctx->scroll_offset + visible_rows; ++i) {
                        size_t display_row = i - ctx->scroll_offset + 1; // Row relative to window (1 to avoid top border)
                        if (i == ctx->sel_songfps_index) {
                                wattron(left_win, A_REVERSE);
                        }
                        // Print at x=1 to avoid left border, truncate to fit inside right border
                        int is_in_queue = 0;
                        for (size_t j = 0; j < ctx->queue.len; ++j) {
                                if (ctx->queue.data[j] == i) {
                                        mvwprintw(left_win, display_row, 1, "*");
                                        is_in_queue = 1;
                                        break;
                                }
                        }
                        mvwprintw(left_win, display_row, 1+is_in_queue, "%.*s", max_x - 2, shstr(ctx->songnames.data[i], max_x/2 + 10));
                        if (i == ctx->sel_songfps_index) {
                                wattroff(left_win, A_REVERSE);
                        }
                        if (!ctx->paused && i == ctx->currently_playing_index) {
                                const char *equalizer_frames[] = {"|", "/", "-", "\\"};
                                int frame_count = sizeof(equalizer_frames) / sizeof(equalizer_frames[0]);
                                int frame = (SDL_GetTicks() / 200) % frame_count; // Cycle every 200ms
                                int N = strlen(ctx->songnames.data[i]);
                                int loc = N > max_x/2 + 10 ? max_x/2 + 13 : N;
                                mvwprintw(left_win, display_row, loc + 2, "%s", equalizer_frames[frame]);
                        }
                }
        }

        wrefresh(left_win);
}

static void resize_signal_handler(int sig) {
        g_resize_flag = 1;
}

static void handle_resize(void) {
        if (!g_resize_flag || !g_ctx) return;
        g_resize_flag = 0;

        endwin();
        refresh();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        int half_width = max_x / 2;

        g_scrn_width = max_x;
        g_scrn_height = max_y;

        if (left_win) delwin(left_win);
        if (right_win) delwin(right_win);
        left_win = right_win = NULL;

        left_win = newwin(max_y, half_width, 0, 0);
        right_win = newwin(max_y, max_x - half_width, 0, half_width);

        if (!left_win || !right_win) {
                if (left_win) delwin(left_win);
                if (right_win) delwin(right_win);
                endwin();
                fprintf(stderr, "Failed to recreate windows after resize\n");
                cleanup();
                exit(1);
        }
        scrollok(left_win, TRUE);

        size_t visible_rows = max_y - 2;
        if (g_ctx->sel_songfps_index < g_ctx->scroll_offset) {
                g_ctx->scroll_offset = g_ctx->sel_songfps_index;
        } else if (g_ctx->sel_songfps_index >= g_ctx->scroll_offset + visible_rows) {
                g_ctx->scroll_offset = g_ctx->sel_songfps_index - visible_rows + 1;
        }
}

static void draw_windows(Ctx *ctx, Ctx_Array *ctxs) {
        draw_song_list(ctx);
        draw_currently_playing(ctx, ctxs);
}

static char *get_song_name(char *path) {
        ssize_t sl = -1;
        for (size_t i = 0; path[i]; ++i) {
                if (path[i] == '/') sl = i;
        }
        return sl != -1 ? path+sl+1 : path;
}

static void shuffle_song_idxs(Ctx *ctx) {
        if (ctx->numtracks <= 0) {
                return;
        }

        for (size_t i = 0; i < ctx->numtracks; i++) {
                dyn_array_append(ctx->shuffle_queue, i);
        }

        // Knuth shuffle
        for (int i = ctx->numtracks - 1; i > 0; i--) {
                int j = rand() % (i + 1);

                size_t temp                = ctx->shuffle_queue.data[i];
                ctx->shuffle_queue.data[i] = ctx->shuffle_queue.data[j];
                ctx->shuffle_queue.data[j] = temp;
        }
}

static void handle_adv_type(Ctx *ctx) {
        if (ctx->mat == MAT_NORMAL) {
                ctx->mat = MAT_SHUFFLE;
                shuffle_song_idxs(ctx);
        }
        else if (ctx->mat == MAT_SHUFFLE) {ctx->mat = MAT_LOOP; }
        else if (ctx->mat == MAT_LOOP) { ctx->mat = MAT_NORMAL; }
        else { assert(0); }

        // Handle case where no first song has been selected.
        // This allows to just play a random song without
        // selecting one first.
        if (!ctx->sel_fst_song) {
                if (ctx->songfps->len != 0) {
                        ctx->sel_fst_song = 1;
                        shuffle_song_idxs(ctx);
                        ctx->currently_playing_index = ctx->shuffle_queue.data[0];
                        ctx->sel_songfps_index = ctx->shuffle_queue.data[0];
                        start_song(ctx);
                        dyn_array_append(ctx->history_idxs, ctx->currently_playing_index);
                        adjust_scroll_offset(ctx);
                }
        }
        handle_upnext(ctx);
}

static void handle_next_song(Ctx *ctx) {
        if (!ctx || ctx->currently_playing_index == -1 || !ctx->current_music || !ctx->sel_fst_song) {
                return;
        }

        Mix_HaltMusic();

        // Adjust scroll offset to keep selection visible
        adjust_scroll_offset(ctx);
}

static void handle_prev_song(Ctx *ctx) {
        if (!ctx || ctx->currently_playing_index == -1 || !ctx->current_music || !ctx->sel_fst_song) {
                return;
        }

        Uint64 current_ticks = SDL_GetTicks();
        Uint64 elapsed_ms = ctx->paused ? (ctx->pause_start - ctx->start_ticks - ctx->paused_ticks)
                : (current_ticks - ctx->start_ticks - ctx->paused_ticks);
        int time_played = elapsed_ms / 1000;

        if (time_played > 1 || ctx->history_idxs.len <= 1) {
                // Restart current song
                int old_upnext = ctx->upnext_idx;
                ctx->sel_songfps_index = ctx->currently_playing_index = dyn_array_at(ctx->history_idxs, ctx->history_idxs.len - 1);
                start_song(ctx);
                ctx->upnext_idx = old_upnext;
        } else if (ctx->history_idxs.len > 1) {
                // Play previous song from history
                ctx->sel_songfps_index = ctx->currently_playing_index = dyn_array_at(ctx->history_idxs, ctx->history_idxs.len - 2);
                start_song(ctx);
                dyn_array_rm_at(ctx->history_idxs, ctx->history_idxs.len - 1);
        }

        adjust_scroll_offset(ctx);
}

char *get_userin(const char *message, const char *autofill) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Calculate window dimensions
        int win_width = 30; // Fixed width for simplicity
        int win_height = message ? 4 : 3; // 4 if message (2 borders, 1 message, 1 input), 3 if no message
        if (message && strlen(message) + 4 > win_width) {
                win_width = strlen(message) + 4; // Adjust width to fit message + padding
        }
        if (autofill && strlen(autofill) + 4 > win_width) {
                win_width = strlen(autofill) + 4; // Adjust width to fit autofill + padding
        }
        if (win_width > max_x * 0.8) win_width = max_x * 0.8; // Cap at 80% of screen width

        // Center the window
        int start_y = (max_y - win_height) / 2;
        int start_x = (max_x - win_width) / 2;

        // Create window
        WINDOW *input_win = newwin(win_height, win_width, start_y, start_x);
        if (!input_win) return NULL;

        // Draw box
        box(input_win, 0, 0);

        // Display message if provided
        if (message) {
                mvwprintw(input_win, 1, 2, "%.*s", win_width - 4, message);
        }

        // Initialize input buffer (27 chars max to fit inside borders)
        char input[28] = {0};
        size_t input_len = 0;

        // Autofill input if provided
        if (autofill) {
                strncpy(input, autofill, sizeof(input) - 1);
                input[sizeof(input) - 1] = '\0';
                input_len = strlen(input);
        }

        // Enable input settings
        keypad(input_win, TRUE);
        nodelay(input_win, FALSE); // Block for input
        echo();

        int ch;
        while (1) {
                // Display input
                mvwprintw(input_win, win_height - 2, 2, "%-*s", win_width - 4, input);
                wrefresh(input_win);

                ch = wgetch(input_win);

                if (ch == ENTER) {
                        delwin(input_win);
                        noecho();
                        return input_len > 0 ? strdup(input) : strdup("");
                } else if (ch == ESCAPE || ch == CTRL('c')) {
                        // Cancel input
                        delwin(input_win);
                        noecho();
                        return NULL;
                } else if (ch == BACKSPACE || ch == 127) {
                        if (input_len > 0) {
                                input[--input_len] = '\0';
                        }
                } else if (ch >= 32 && ch <= 126 && input_len < sizeof(input) - 1) {
                        input[input_len++] = ch;
                        input[input_len] = '\0';
                }
                // Ignore other characters
        }
}

static void handle_search(Ctx *ctx, size_t startfrom, int rev, char *prevsearch) {
        if (!ctx) return;

        char *query = NULL;
        if (prevsearch) {
                query = prevsearch;
        } else {
                query = get_userin("Entery Query (RegEx Supported):", NULL);
                if (!query) return;
                ctx->prevsearch = strdup(query);
        }
        ssize_t found = -1;
        if (rev) {
                for (int i = (int)startfrom; i >= 0; --i) {
                        if (regex(query, ctx->songnames.data[i])) {
                                found = i;
                                break;
                        }
                }
        } else {
                for (size_t i = startfrom; i < ctx->songnames.len; ++i) {
                        if (regex(query, ctx->songnames.data[i])) {
                                found = i;
                                break;
                        }
                }
        }
        if (found == -1) {
                if (g_config.flags & FT_NOTIF) {
                        tinyfd_notifyPopup("[ampire]: Could not find song", query, "warning");
                }
                return;
        }

        ctx->sel_songfps_index = found;
        adjust_scroll_offset(ctx);
}

static Ctx ctx_create(Playlist *p) {
        static size_t uuid = 0;
        Ctx ctx = (Ctx) {
                .uuid                    = uuid++,
                .songfps                 = &p->songfps,
                .pname                   = p->name,
                .history_idxs            = dyn_array_empty(Size_T_Array),
                .songnames               = dyn_array_empty(Str_Array),
                .sel_songfps_index       = 0,
                .scroll_offset           = 0,
                .playlist_scroll_offset  = 0,
                .sel_fst_song            = 0,
                .paused                  = 0,
                .mat                     = MAT_NORMAL,
                .currently_playing_index = -1,
                .current_music           = NULL,
                .start_ticks             = 0,
                .paused_ticks            = 0,
                .pause_start             = 0,
                .prevsearch              = NULL,
                .numtracks               = p->songfps.len,
                .upnext_idx              = 0,
                .playlist_modified       = 0,
                .playlist_saved          = 0,
                .queue                   = dyn_array_empty(Size_T_Array),
                .shuffle_queue           = dyn_array_empty(Size_T_Array),
        };
        for (size_t i = 0; i < p->songfps.len; ++i) {
                dyn_array_append(ctx.songnames,
                                 strdup(get_song_name(ctx.songfps->data[i])));
        }
        return ctx;
}

static void save_playlist(Ctx *ctx) {
        if (!ctx) return;
        char *name = NULL;
        while (1) {
                if (ctx->playlist_saved) break;

                name = get_userin("Enter Playlist Name:", ctx->pname);
                if (!name) {
                        return;
                } else if (!strcmp(name, "")) {
                        display_temp_message("Name Cannot be Empty");
                } else if (!strcmp(name, "unnamed")) {
                        if (prompt_yes_no("Name is set to `unnamed`, continue?")) {
                                break;
                        }
                } else {
                        break;
                }
        }

        if (ctx->playlist_saved) {
                io_replace_playlist_songs(ctx->pname, ctx->songfps);
        } else {
                io_write_to_config_file(name, ctx->songfps);
                ctx->pname = name;
        }

        ctx->playlist_modified = 0;
        ctx->playlist_saved = 1;
}

static void volume_up(Ctx *ctx) {
        if (!ctx) return;
        g_volume += 10;
        if (g_volume > MIX_MAX_VOLUME) {
                g_volume = MIX_MAX_VOLUME;
        }
        Mix_VolumeMusic(g_volume);
}

static void volume_down(Ctx *ctx) {
        if (!ctx) return;
        g_volume -= 10;
        if (g_volume < 0) {
                g_volume = 0;
        }
        Mix_VolumeMusic(g_volume);
}

static void handle_mute(void) {
        if (g_volume == 0) {
                g_volume = g_last_volume;
        } else {
                g_last_volume = g_volume;
                g_volume = 0;
        }
        Mix_VolumeMusic(g_volume);
}

static void remove_duplicates(Ctx *ctx) {
        if (!prompt_yes_no("Remove duplicate tracks in playlist?")) return;

        Str_Array found = dyn_array_empty(Str_Array);
        Str_Array tmp = dyn_array_empty(Str_Array);
        Size_T_Array idxs = dyn_array_empty(Size_T_Array);

        // Find duplicates
        for (size_t i = 0; i < ctx->songnames.len; ++i) {
                int is_duplicate = 0;
                for (size_t j = 0; j < tmp.len; ++j) {
                        if (!strcmp(ctx->songnames.data[i], tmp.data[j])) {
                                is_duplicate = 1;
                                break;
                        }
                }
                if (is_duplicate) {
                        char buf[256] = {0};
                        sprintf(buf, "Duplicate: %s", ctx->songnames.data[i]);
                        display_temp_message_wsleep(buf, -1);
                        dyn_array_append(found, ctx->songnames.data[i]);
                        dyn_array_append(idxs, i); // Collect index of duplicate
                } else {
                        dyn_array_append(tmp, ctx->songnames.data[i]);
                }
        }

        if (!found.len) {
                display_temp_message("No duplicates found");
                goto cleanup;
        }

        // Sort indices to avoid index shifting issues
        for (size_t i = 0; i < idxs.len; ++i) {
                for (size_t j = i + 1; j < idxs.len; ++j) {
                        if (idxs.data[i] < idxs.data[j]) {
                                size_t temp = idxs.data[i];
                                idxs.data[i] = idxs.data[j];
                                idxs.data[j] = temp;
                        }
                }
        }

        // Remove duplicates from songfps and songnames
        for (size_t i = 0; i < idxs.len; ++i) {
                dyn_array_rm_at(*ctx->songfps, idxs.data[i]);
                dyn_array_rm_at(ctx->songnames, idxs.data[i]);
                --ctx->numtracks;
        }

        char buf[256] = {0};
        sprintf(buf, "Removed %zu tracks", idxs.len);
        display_temp_message(buf);
        ctx->playlist_modified = 1;

 cleanup:
        dyn_array_free(found);
        dyn_array_free(tmp);
        dyn_array_free(idxs);
}

static void handle_oneshot_sigint(int sig) {
        g_oneshot_keep_running = 0;
}

static void
reset_view(Ctx *ctx)
{
        ctx->sel_songfps_index = ctx->currently_playing_index;
        adjust_scroll_offset(ctx);
}

// Does not take ownership of playlists
void run(const Playlist_Array *playlists) {
        g_original_playlist_sz = g_config.playlist_sz;

        srand((unsigned int)time(NULL));
        Ctx_Array ctxs = dyn_array_empty(Ctx_Array);
        size_t ctx_idx = 0;

        for (size_t i = 0; i < playlists->len; ++i) {
                dyn_array_append(ctxs, ctx_create(&playlists->data[i]));
                if (!playlists->data[i].from_cli) {
                        ctxs.data[ctxs.len-1].playlist_saved = 1;
                }
        }
        g_ctx = &ctxs.data[0];

        if (g_config.volume != -1) {
                g_volume = g_config.volume;
        }

        if (g_config.playlist != -1) {
                if (g_config.playlist > ctxs.len || g_config.playlist < 1) {
                        err_wargs("playlist %d is out of range of %zu", g_config.playlist, ctxs.len);
                }
                ctx_idx = g_config.playlist-1;
                g_ctx = &ctxs.data[ctx_idx];
        }

        g_total_playlist_pages = (int)ceilf(((float)ctxs.len) / ((float)g_config.playlist_sz));

        SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL could not initialize: %s\n", SDL_GetError());
                exit(1);
        }

        atexit(cleanup);

        if (g_config.flags & FT_ONESHOT) {
                // --oneshot,-o mode, not using ncurses and the TUI.
                const char *fp = playlists->data[playlists->len-1].songfps.data[0];
                signal(SIGINT, handle_oneshot_sigint);
                play_music(g_ctx, fp);
                while (g_oneshot_keep_running);
                return;
        }

        init_ncurses();
        signal(SIGWINCH, resize_signal_handler);

        int ch;
        while (1) {
        start:
                handle_resize();
                draw_windows(g_ctx, &ctxs);
                ch = getch();

                if (g_need_next_song) {
                        g_need_next_song = false;
                        start_song(g_ctx);
                        adjust_scroll_offset(g_ctx);
                }

                // TODO: enable this feature again.
                // it currently is bugged when you use it
                // and it goes out of the scope of the
                // g_config.playlist_sz.
                /* if (isdigit(ch)) { */
                /*         int idx = (ch-'0') - 1; */
                /*         if (idx < ctxs.len) { */
                /*                 ctx_idx = idx; */
                /*                 g_ctx = &ctxs.data[ctx_idx]; */
                /*         } */
                /* } */

                switch (ch) {
                case 'q':
                case 'Q':
                case CTRL('q'): goto done;
                case CTRL('l'): {
                        resize_signal_handler(1);
                } break;
                case '!': {
                        remove_duplicates(g_ctx);
                } break;
                case CTRL('s'): {
                        save_playlist(g_ctx);
                } break;
                case 'k':
                case KEY_UP: {
                        handle_key_up(g_ctx);
                } break;
                case 'g': {
                        g_ctx->sel_songfps_index = 0;
                        adjust_scroll_offset(g_ctx);
                } break;
                case 'G': {
                        g_ctx->sel_songfps_index = g_ctx->songfps->len - 1;
                        adjust_scroll_offset(g_ctx);
                } break;
                case 'J': {
                        if (ctx_idx < ctxs.len-1) {
                                g_ctx = &ctxs.data[++ctx_idx];
                                if (ctx_idx % g_config.playlist_sz == 0) {
                                        ++g_playlist_page;
                                }
                        }
                } break;
                case 'K': {
                        if (ctx_idx != 0) {
                                if (ctx_idx % g_config.playlist_sz == 0) {
                                        --g_playlist_page;
                                }
                                g_ctx = &ctxs.data[--ctx_idx];
                        }
                } break;
                case 'j':
                case KEY_DOWN: {
                        handle_key_down(g_ctx);
                } break;
                case 'h':
                case KEY_LEFT: {
                        handle_key_left(g_ctx);
                } break;
                case 'l':
                case KEY_RIGHT: {
                        handle_key_right(g_ctx);
                } break;
                case SPACE: {
                        pause_audio(g_ctx);
                } break;
                case 'a': {
                        handle_adv_type(g_ctx);
                } break;
                case 'L':
                case '.':
                case '>': {
                        handle_next_song(g_ctx);
                } break;
                case 'H':
                case ',':
                case '<': {
                        handle_prev_song(g_ctx);
                } break;
                case 'n': {
                        if (g_ctx && g_ctx->sel_songfps_index < g_ctx->songnames.len-1) {
                                handle_search(g_ctx, g_ctx->sel_songfps_index+1, 0, g_ctx->prevsearch);
                        }
                } break;
                case 'N': {
                        if (g_ctx && g_ctx->sel_songfps_index > 0) {
                                handle_search(g_ctx, g_ctx->sel_songfps_index-1, 1, g_ctx->prevsearch);
                        }
                } break;
                case '/': {
                        handle_search(g_ctx, 0, 0, NULL);
                } break;
                case 'm': {
                        handle_mute();
                } break;
                case '-':
                case '_': {
                        volume_down(g_ctx);
                } break;
                case '=':
                case '+': {
                        volume_up(g_ctx);
                } break;
                case 'u': {
                        dyn_array_append(g_ctx->queue, g_ctx->sel_songfps_index);
                        handle_upnext(g_ctx);
                } break;
                case '[': {
                        if (g_playlist_page > 0) {
                                --g_playlist_page;
                                ctx_idx = g_playlist_page*g_config.playlist_sz;
                                g_ctx = &ctxs.data[g_playlist_page*g_config.playlist_sz];
                        }
                } break;
                case ']': {
                        if (g_playlist_page < g_total_playlist_pages-1) {
                                ++g_playlist_page;
                                ctx_idx = g_playlist_page*g_config.playlist_sz;
                                g_ctx = &ctxs.data[g_playlist_page*g_config.playlist_sz];
                        }
                } break;
                case 'z': {
                        reset_view(g_ctx);
                } break;
                case 'd':
                case 'D': {
                        if (g_ctx && io_del_playlist(g_ctx->pname)) {
                                // TODO: handle memory
                                dyn_array_rm_at(ctxs, ctx_idx);
                                for (size_t i = ctx_idx; i < ctxs.len; ++i) {
                                        --ctxs.data[i].uuid;
                                }
                                if (ctx_idx >= ctxs.len) {
                                        ctx_idx = ctxs.len-1;
                                }
                                if (ctxs.len == 0) {
                                        g_ctx = NULL;
                                } else {
                                        g_ctx = &ctxs.data[ctx_idx];
                                }
                        }
                } break;
                case 'f': {
                        char const *const filter_patterns[] = {"*.wav", "*.ogg", "*.mp3", "*.opus"};
                        char *path = tinyfd_openFileDialog("Select a directory", ".",
                                                           sizeof(filter_patterns)/sizeof(*filter_patterns),
                                                           filter_patterns,
                                                           "Music Files", 1);
                        exit(0);
                } break;
                case ENTER: {
                        if (!g_ctx) break;
                        for (size_t i = 0; i < ctxs.len; ++i) {
                                if (ctxs.data[i].uuid != g_ctx->uuid) {
                                        ctxs.data[i].currently_playing_index = -1;
                                }
                        }
                        start_song(g_ctx);
                        dyn_array_append(g_ctx->history_idxs, g_ctx->currently_playing_index);
                } break;
                default: (void)0x0;
                }
        }
 done:
        for (size_t i = 0; i < ctxs.len; ++i) {
                if (ctxs.data[i].playlist_modified) {
                        if (prompt_yes_no("You have unsaved changes, really quit?")) {
                                break;
                        } else {
                                goto start;
                        }
                }

        }
/*         for (size_t i = 0; i < ctx.songnames.len; ++i) { */
/*                 free(ctx.songnames.data[i]); */
/*         } */

/*         dyn_array_free(ctx.songnames); */
}
