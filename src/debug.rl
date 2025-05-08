#!/usr/local/bin/earl

module Debug

import "std/system.rl"; as sys
import "std/datatypes/list.rl";

@const let flags   = "-O0 -ggdb -o ampire-debug-build";
@const let files   = List::to_str(sys::get_all_files_by_ext(".", "c"));
@const let include = "-Iinclude -I../external/SDL3/include -I../external/SDL3_mixer/include";
@const let link    = "-lncurses -ltinfo -lSDL3_mixer -lSDL3";

$f"cc {files} {link} {include} {flags}";
