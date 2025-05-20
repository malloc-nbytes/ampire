#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "ds/strmap.h"
#include "ampire-utils.h"

Str_Map strmap_create(strmap_hash_sig hash, strmap_destroy_val_sig destroy) {
        return (Str_Map) {
                .tbl = {
                        .buckets = malloc(sizeof(__Str_Map_Node *) * STRMAP_INIT_CAP),
                        .len = 0,
                        .cap = STRMAP_INIT_CAP,
                },
                .hash = hash ? hash : djb2,
                .destroy = destroy,
        };
}

void strmap_insert(Str_Map *m, char *k, uint8_t *v) {
        if (!m || !k) return;

        unsigned long index = m->hash(k) % m->tbl.cap;
        __Str_Map_Node *new_bucket = malloc(sizeof(__Str_Map_Node));

        new_bucket->k = strdup(k);
        new_bucket->v = v;
        new_bucket->n = m->tbl.buckets[index];
        m->tbl.buckets[index] = new_bucket;

        m->tbl.len++;
}

/* void *s_umap_get(s_umap_t *map, const char *key) { */
/*         if (!map || !key) return NULL; */

/*         unsigned long index = map->hash(key) % map->cap; */
/*         s_umap_bucket_t *bucket = map->tbl[index]; */

/*         while (bucket) { */
/*                 if (!strcmp(bucket->key, key)) */
/*                         return bucket->value; */
/*                 bucket = bucket->next; */
/*         } */

/*         return NULL; */
/* } */


/* int s_umap_contains(s_umap_t *map, const char *key) { */
/*         return s_umap_get(map, key) != NULL; */
/* } */
