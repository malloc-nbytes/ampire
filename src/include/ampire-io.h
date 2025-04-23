#ifndef IO_H
#define IO_H

#include "ds/array.h"

Str_Array io_flatten_dirs(const Str_Array *dirs);
Str_Array io_read_config_file(void);
void io_write_to_config_file(const Str_Array *filepaths);

#endif // IO_H
