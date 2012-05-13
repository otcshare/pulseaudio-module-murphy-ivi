#ifndef fooindexhashfoo
#define fooindexhashfoo

#include <stdint.h>

struct pa_index_hash;

struct pa_index_hash *pa_index_hash_init(uint32_t);
void pa_index_hash_free(struct pa_index_hash *);
void pa_index_hash_add(struct pa_index_hash *, uint32_t, void *);
void *pa_index_hash_remove(struct pa_index_hash *, uint32_t);
void *pa_index_hash_lookup(struct pa_index_hash *, uint32_t);


#endif /* fooindexhashfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
