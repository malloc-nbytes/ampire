#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "ampire-utils.h"

int str_isdigit(const char *s) {
        for (size_t i = 0; s[i]; ++i) {
                if (!isdigit(s[i])) return 0;
        }
        return 1;
}

unsigned long djb2(const char *str) {
        unsigned long hash = 5381;
        int c;

        while ((c = *str++)) {
                hash = ((hash << 5) + hash) + c;
        }

        return hash;
}

char *shstr(char *s, int len) {
        assert(len < 256);
        static char buf[256] = {0};
        if (strlen(s) > len) {
                (void)strncpy(buf, s, len);
                buf[len++] = '.'; buf[len++] = '.'; buf[len++] = '.';
                buf[len++] = '\0';
                return buf;
        }
        return s;
}
