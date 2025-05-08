#ifndef IO_H
#define IO_H

#include "ds/array.h"
#include "ampire-display.h"

Playlist_Array io_flatten_dirs(const Str_Array *dirs);
Playlist_Array io_read_config_file(void);
void io_write_to_config_file(const char *pname, const Str_Array *filepaths);
void io_clear_config_file(void);
int io_del_playlist(const char *pname);

#endif // IO_H
