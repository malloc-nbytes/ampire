#include <ctype.h>

#include "ampire-utils.h"

int str_isdigit(const char *s) {
        for (size_t i = 0; s[i]; ++i) {
                if (!isdigit(s[i])) return 0;
        }
        return 1;
}
