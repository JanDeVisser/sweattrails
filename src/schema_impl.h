#include "hash.h"

nodeptr entity_append(repo_t *repo, entity_t entity)
{
    dynarr_append(&repo->entities, entity);
    nodeptr ret = nodeptr_ptr(repo->entities.len - 1);
    repo->entities.items[ret.value].dummy_entity.id.entity_id = ret;
    return ret;
}

nodeptr _entity_append(repo_t *repo, entity_type_t t, void *entity, size_t size)
{
    entity_t e = { .type = t };
    memcpy(&e.dummy_entity, entity, size);
    return entity_append(repo, e);
}

bool hash_entity(repo_t *repo, nodeptr p)
{
    assert(p.ok && p.value < repo->entities.len);
    void        *entity = &repo->entities.items[p.value].dummy_entity;
    unsigned int h = hash(entity, sizeof(entity_t) - offsetof(entity_t, dummy_entity));
    bool         ret = repo->entities.items[p.value].hash == h;
    repo->entities.items[p.value].hash = h;
    return ret;
}
