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

typedef struct {
        const Str_Array *songfps;
        size_t sel_songfps_index;
        int sel_fst_song;
        int paused;
} Context;

static void cleanup(void) {
        Mix_HaltMusic();
        Mix_CloseAudio();
        endwin();
        SDL_Quit();
}

static void play_music(const char *song) {
        assert(song);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
                exit(1);
        }

        if (Mix_OpenAudio(0, NULL) < 0) {
                fprintf(stderr, "SDL_mixer initialization failed: %s\n", Mix_GetError());
                SDL_Quit();
                exit(1);
        }

        Mix_Music* music = Mix_LoadMUS(song);
        if (!music) {
                fprintf(stderr, "Failed to load music '%s': %s\n", song, Mix_GetError());
                Mix_CloseAudio();
                SDL_Quit();
                exit(1);
        }

        if (Mix_PlayMusic(music, -1) < 0) {
                fprintf(stderr, "Failed to play music: %s\n", Mix_GetError());
                Mix_FreeMusic(music);
                Mix_CloseAudio();
                SDL_Quit();
                exit(1);
        }
}

static void start_song(Context *ctx) {
        Mix_HaltMusic();
        play_music(ctx->songfps->data[ctx->sel_songfps_index]);
        ctx->paused = 0;
        ctx->sel_fst_song = 1;
}

static void pause_audio(Context *ctx) {
        if (!ctx->sel_fst_song) return;
        ctx->paused = !ctx->paused;
        Mix_PauseAudio(ctx->paused);
}

static void handle_key_down(Context *ctx) {
        if (ctx->sel_songfps_index < ctx->songfps->len - 1) {
                ctx->sel_songfps_index++;
        }
}

static void handle_key_up(Context *ctx) {
        if (ctx->sel_songfps_index > 0) {
                ctx->sel_songfps_index--;
        }
}

static void init_ncurses(void) {
        initscr();
        raw();
        keypad(stdscr, TRUE);
        noecho();
        curs_set(0);
}

static void show_song_list(Context *ctx) {
        clear();
        for (size_t i = 0; i < ctx->songfps->len; ++i) {
                if (i == ctx->sel_songfps_index) {
                        attron(A_REVERSE);
                }
                mvprintw(i, 0, "%s\n", ctx->songfps->data[i]);
                if (i == ctx->sel_songfps_index) {
                        attroff(A_REVERSE);
                }
        }
        refresh();
}

void run(const Str_Array *songfps) {
        atexit(cleanup);

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL could not initialize: %s\n", SDL_GetError());
                exit(1);
        }

        init_ncurses();

        Context ctx = (Context) {
                .songfps = songfps,
                .sel_songfps_index = 0,
                .sel_fst_song = 0,
                .paused = 0,
        };

        int ch;
        while (1) {
                show_song_list(&ctx);
                ch = getch();
                switch (ch) {
                case CTRL('q'): goto done;
                case KEY_UP: {
                        handle_key_up(&ctx);
                } break;
                case KEY_DOWN: {
                        handle_key_down(&ctx);
                } break;
                case SPACE: {
                        pause_audio(&ctx);
                } break;
                case ENTER: {
                        start_song(&ctx);
                } break;
                default: (void)0x0;
                }
        }
 done:
        (void)0x0;
}
