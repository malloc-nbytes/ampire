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
