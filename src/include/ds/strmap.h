#ifndef STRMAP_H
#define STRMAP_H

#include <stdint.h>
#include <stddef.h>

#define STRMAP_INIT_CAP 1024

typedef unsigned long (*strmap_hash_sig)(const char *k);
typedef void (*strmap_destroy_val_sig)(uint8_t *v);

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
void strmap_insert(Str_Map *map, char *k, uint8_t *v);
uint8_t *strmap_get(Str_Map *m, const char *k);
int strmap_contains(Str_Map *m, const char *k);
void strmap_free(Str_Map *m);
size_t strmap_len(const Str_Map *m);

#endif // STRMAP_H
