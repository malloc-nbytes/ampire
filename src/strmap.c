#include "ds/strmap.h"
#include "ampire-utils.h"

Str_Map strmap_create(strmap_hash_sig hash, strmap_destroy_val_sig destroy) {
        return (Str_Map) {
                .tbl = {
                        .buckets = NULL,
                        .len = 0,
                        .cap = 0,
                },
                .hash = hash ? hash : djb2,
                .destroy = destroy,
        };
}
