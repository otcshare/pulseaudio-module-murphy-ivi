#include <stdio.h>
#include <sys/types.h>
#include <errno.h>


#include <pulsecore/macro.h>
#include <pulse/xmalloc.h>

#include "index-hash.h"


struct pa_index_hash_entry {
    struct pa_index_hash_entry *next;
    uint32_t                    index;
    void                       *value;
};

struct pa_index_hash {
    uint32_t                     mask;
    struct pa_index_hash_entry **table;
};


struct pa_index_hash *pa_index_hash_init(uint32_t bits)
{
    struct pa_index_hash *hash;
    uint32_t max;
    size_t   size;

    if (bits > 16)
        bits = 16;

    max  = 1UL << bits; 
    size = sizeof(struct pa_index_hash_entry *) * max;

    hash = pa_xmalloc0(size);

    hash->mask  = max - 1;
    hash->table = pa_xmalloc0(size);

    return hash;
}

void pa_index_hash_free(struct pa_index_hash *hash)
{
    pa_xfree(hash->table);
    pa_xfree(hash);
}

void pa_index_hash_add(struct pa_index_hash *hash, uint32_t index, void *value)
{
    struct pa_index_hash_entry *entry, *prev;

    pa_assert(hash);
    pa_assert(hash->table);

    prev = (struct pa_index_hash_entry *)(hash->table + (index & hash->mask));

    while ((entry = prev->next) != NULL) {
        if (index == entry->index) {
            entry->value = value;
            return;
        }

        prev = entry;
    }

    entry = pa_xmalloc0(sizeof(struct pa_index_hash_entry));

    entry->index = index;
    entry->value = value; 

    prev->next = entry;
}

void *pa_index_hash_remove(struct pa_index_hash *hash, uint32_t index)
{
    struct pa_index_hash_entry *entry, *prev;
    void *value;

    pa_assert(hash);
    pa_assert(hash->table);
    
    prev = (struct pa_index_hash_entry *)(hash->table + (index & hash->mask));

    while ((entry = prev->next) != NULL) {
        if (index == entry->index) {
            prev->next = entry->next;

            value = entry->value;
            pa_xfree(entry);

            return value;
        }

        prev = entry;
    }

    return NULL;
}

void *pa_index_hash_lookup(struct pa_index_hash *hash, uint32_t index)
{
    struct pa_index_hash_entry *entry;

    pa_assert(hash);
    pa_assert(hash->table);

    for (entry = hash->table[index & hash->mask]; entry; entry = entry->next) {
        if (index == entry->index)
            return entry->value;
    }

    return NULL;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
