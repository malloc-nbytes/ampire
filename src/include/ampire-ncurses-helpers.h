#ifndef AMPIRE_NCURSES_HELPERS_H
#define AMPIRE_NCURSES_HELPERS_H

#define CTRL(x) ((x) & 0x1F)
#define BACKSPACE 263
#define ESCAPE 27
#define ENTER 10
#define SPACE 32

void display_temp_message(const char *message);
int prompt_yes_no(const char *message);

#endif // AMPIRE_NCURSES_HELPERS_H
