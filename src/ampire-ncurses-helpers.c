#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <ncurses.h>

#include "ampire-ncurses-helpers.h"

void display_temp_message_wsleep(const char *message, int secs) {
        if (!message) return;

        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Calculate window size: at least 30 chars wide or message length + padding, max 80% of screen
        int msg_len = strlen(message);
        int win_width = msg_len + 4; // 2 for borders, 2 for padding
        if (win_width < 30) win_width = 30;
        if (win_width > max_x * 0.8) win_width = max_x * 0.8;

        // Calculate number of lines needed (wrap text)
        int lines = 1;
        if (msg_len > win_width - 4) {
                lines = (msg_len + (win_width - 4) - 1) / (win_width - 4);
        }
        int win_height = lines + 4; // 2 for borders, 1 for countdown, 1 for padding

        // Center the window
        int start_y = (max_y - win_height) / 2;
        int start_x = (max_x - win_width) / 2;

        WINDOW *msg_win = newwin(win_height, win_width, start_y, start_x);
        if (!msg_win) return;

        box(msg_win, 0, 0);

        int current_line = 1;
        int chars_left = msg_len;
        int pos = 0;
        while (chars_left > 0) {
                int chars_to_print = chars_left > (win_width - 4) ? (win_width - 4) : chars_left;
                mvwprintw(msg_win, current_line, 2, "%.*s", chars_to_print, message + pos);
                pos += chars_to_print;
                chars_left -= chars_to_print;
                current_line++;
        }

        wrefresh(msg_win);

        for (int i = secs; i >= 0; --i) {
                mvwprintw(msg_win, win_height - 2, 2, "Closing in %d seconds...", i);
                wrefresh(msg_win);
                sleep(1);
        }

        delwin(msg_win);
        refresh();
}

int prompt_yes_no(const char *message) {
        if (!message) return 0;

        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Calculate window size: message length + "[y/n]" + padding, min 20, max 80% of screen
        int msg_len = strlen(message) + 6; // +6 for " [y/n]"
        int win_width = msg_len + 4; // +4 for borders and padding
        if (win_width < 20) win_width = 20;
        if (win_width > max_x * 0.8) win_width = max_x * 0.8;

        int win_height = 3; // Borders + 1 row for message and prompt

        // Center the window
        int start_y = (max_y - win_height) / 2;
        int start_x = (max_x - win_width) / 2;

        // Create window
        WINDOW *prompt_win = newwin(win_height, win_width, start_y, start_x);
        if (!prompt_win) return 0;

        // Draw box
        box(prompt_win, 0, 0);

        mvwprintw(prompt_win, 1, 2, "%.*s [y/n]", win_width - 8, message);

        keypad(prompt_win, TRUE);
        nodelay(prompt_win, FALSE); // Block for input
        noecho();

        int result = -1; // -1 until valid input is received
        int ch;

        while (result == -1) {
                wrefresh(prompt_win);
                ch = wgetch(prompt_win);

                switch (tolower(ch)) {
                case 'y':
                        result = 1;
                        break;
                case 'n':
                        result = 0;
                        break;
                case ESCAPE:
                case CTRL('c'):
                        result = 0;
                        break;
                default:
                        break;
                }
        }

        delwin(prompt_win);
        refresh(); // Restore main screen

        return result;
}
