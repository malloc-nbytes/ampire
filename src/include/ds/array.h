#ifndef ARRAY_H
#define ARRAY_H

#include <stddef.h>

#include "dyn_array.h"

DYN_ARRAY_TYPE(char *, Str_Array);
DYN_ARRAY_TYPE(size_t, Size_T_Array);

#endif // ARRAY_H
