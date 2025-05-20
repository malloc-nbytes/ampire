#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdbool.h>

#define err_wargs(msg, ...)                                             \
    do {                                                                \
        fprintf(stderr, "error: " msg "\n", __VA_ARGS__);               \
        exit(1);                                                        \
    } while (0)

#define err(msg)                                \
    do {                                        \
        fprintf(stderr, msg);                   \
        exit(1);                                \
    } while (0)

int str_isdigit(const char *s);

unsigned long djb2(const char *str);

#endif // UTILS_H
