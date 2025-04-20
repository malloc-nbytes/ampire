#include <assert.h>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <ncurses.h>

#include "display.h"

#define Mix_GetError    SDL_GetError

#define CTRL(x) ((x) & 0x1F)
#define BACKSPACE 263
#define ESCAPE 27
#define ENTER 10
#define SPACE 32

static struct {
        const Str_Array *songfps;
        size_t sel_songfps_index;
        int sel_fst_song;
        int paused;
        ssize_t currently_playing_index;
        Mix_Music *current_music;
        Uint64 start_ticks;      // Time when song started
        Uint64 paused_ticks;     // Accumulated paused time
        Uint64 pause_start;      // Time when pause began
} ctx = {
        .songfps = NULL,
        .sel_songfps_index = 0,
        .sel_fst_song = 0,
        .paused = 0,
        .currently_playing_index = -1,
        .current_music = NULL,
        .start_ticks = 0,
        .paused_ticks = 0,
        .pause_start = 0,
};

static void cleanup(void) {
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

        // Define explicit audio format to minimize ALSA adjustments
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

        // Restore stderr
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

        ctx.start_ticks = SDL_GetTicks(); // Record start time
        ctx.paused_ticks = 0;
        ctx.pause_start = 0;

        ctx.sel_fst_song = 1;
}

static void music_finished(void) {
        ctx.currently_playing_index = (ctx.currently_playing_index + 1) % ctx.songfps->len;
        // ctx.sel_songfps_index = ctx.currently_playing_index;
        Mix_HaltMusic();
        start_song();
}

static void pause_audio(void) {
        if (!ctx.sel_fst_song) return;
        ctx.paused = !ctx.paused;
        Mix_PauseAudio(ctx.paused);
        if (ctx.paused) {
                ctx.pause_start = SDL_GetTicks(); // Start of pause
        } else {
                ctx.paused_ticks += SDL_GetTicks() - ctx.pause_start; // Add pause duration
                ctx.pause_start = 0;
        }
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

static void init_ncurses(void) {
        initscr();
        raw();
        keypad(stdscr, TRUE);
        noecho();
        curs_set(0);
        timeout(500);
}

static void show_song_list(void) {
        clear();
        for (size_t i = 0; i < ctx.songfps->len; ++i) {
                if (i == ctx.sel_songfps_index) {
                        attron(A_REVERSE);
                }
                mvprintw(i, 0, "%s\n", ctx.songfps->data[i]);
                if (i == ctx.sel_songfps_index) {
                        attroff(A_REVERSE);
                }
        }
        if (ctx.currently_playing_index != -1) {
                printw("=======\n");
                printw("Now Playing: %s\n", ctx.songfps->data[ctx.currently_playing_index]);

                // Calculate time played
                Uint64 current_ticks = SDL_GetTicks();
                Uint64 elapsed_ms = ctx.paused ? (ctx.pause_start - ctx.start_ticks - ctx.paused_ticks)
                        : (current_ticks - ctx.start_ticks - ctx.paused_ticks);
                int time_played = elapsed_ms / 1000; // Convert to seconds

                char time_str[16];
                format_time(time_played, time_str, sizeof(time_str));
                printw("Time: %s", time_str);
        }
        refresh();
}

void run(const Str_Array *songfps) {
        ctx.songfps = songfps;

        SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);

        atexit(cleanup);

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL could not initialize: %s\n", SDL_GetError());
                exit(1);
        }

        init_ncurses();

        int ch;
        while (1) {
                show_song_list();
                ch = getch();
                switch (ch) {
                case CTRL('q'): goto done;
                case KEY_UP: {
                        handle_key_up();
                } break;
                case KEY_DOWN: {
                        handle_key_down();
                } break;
                case SPACE: {
                        pause_audio();
                } break;
                case ENTER: {
                        start_song();
                } break;
                default: (void)0x0;
                }
        }
 done:
        (void)0x0;
}
