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

void cleanup(void) {
        Mix_HaltMusic();
        Mix_CloseAudio();
        endwin();
        SDL_Quit();
}

void start_audio(const char *song) {
        endwin();
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

void run(const Str_Array *songfps) {
        int num_decoders = Mix_GetNumMusicDecoders();
        printf("Supported music decoders:\n");
        for (int i = 0; i < num_decoders; i++) {
                printf("  %s\n", Mix_GetMusicDecoder(i));
        }
        exit(0);

        atexit(cleanup);

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                fprintf(stderr, "SDL could not initialize: %s\n", SDL_GetError());
                exit(1);
        }

        initscr();
        raw();
        keypad(stdscr, TRUE);
        noecho();
        curs_set(0);

        int sel = 0;
        int ch;

        while (1) {
                clear();
                for (int i = 0; i < (int)songfps->len; ++i) {
                        if (i == sel) {
                                attron(A_REVERSE);
                        }
                        mvprintw(i, 0, "%s\n", songfps->data[i]);
                        if (i == sel) {
                                attroff(A_REVERSE);
                        }
                }
                refresh();
                ch = getch();
                if (ch == CTRL('q')) {
                        break;
                } else if (ch == KEY_UP) {
                        if (sel > 0) {
                                sel--;
                        }
                } else if (ch == KEY_DOWN) {
                        if (sel < (int)songfps->len - 1) {
                                sel++;
                        }
                } else if (ch == ENTER) {
                        //Mix_HaltMusic();
                        start_audio(songfps->data[sel]);
                }
        }
}
