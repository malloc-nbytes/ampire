#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "ds/strmap.h"
#include "ampire-utils.h"

static void strmap_default_val_free(uint8_t *v) {
        free(v);
}

Str_Map strmap_create(strmap_hash_sig hash, strmap_destroy_val_sig destroy) {
        return (Str_Map) {
                .tbl = {
                        .buckets = malloc(sizeof(__Str_Map_Node *) * STRMAP_INIT_CAP),
                        .len = 0,
                        .cap = STRMAP_INIT_CAP,
                },
                .hash = hash ? hash : djb2,
                .destroy = destroy ? destroy : strmap_default_val_free,
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

uint8_t *strmap_get(Str_Map *m, const char *k) {
        if (!m || !k) return NULL;

        unsigned long index = m->hash(k) % m->tbl.cap;
        __Str_Map_Node *bucket = m->tbl.buckets[index];

        while (bucket) {
                if (!strcmp(bucket->k, k)) {
                        return bucket->v;
                }
                bucket = bucket->n;
        }

        return NULL;
}


int strmap_contains(Str_Map *m, const char *k) {
        return strmap_get(m, k) != NULL;
}

void strmap_free(Str_Map *m) {
        for (size_t i = 0; i < m->tbl.cap; ++i) {
                if (m->tbl.buckets[i]) {
                        free(m->tbl.buckets[i]->k);
                        m->destroy(m->tbl.buckets[i]->v);
                }
                free(m->tbl.buckets[i]);
        }
}

size_t strmap_len(const Str_Map *m) {
        return m->tbl.len;
}
