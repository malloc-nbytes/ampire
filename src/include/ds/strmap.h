#ifndef STRMAP_H
#define STRMAP_H

#include <stdint.h>
#include <stddef.h>

typedef unsigned long (*strmap_hash_sig)(const char *k);
typedef void (*strmap_destroy_val_sig)(void *v);

typedef struct __Str_Map_Node {
        char *k;
        uint8_t *v;
        struct __Str_Map_Node *n;
} __Str_Map_Node;

typedef struct {
        struct {
                __Str_Map_Node **buckets;
                size_t len;
                size_t cap;
        } tbl;
        strmap_hash_sig hash;
        strmap_destroy_val_sig destroy;
} Str_Map;

Str_Map strmap_create(strmap_hash_sig hash, strmap_destroy_val_sig destroy);

#endif // STRMAP_H
