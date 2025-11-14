
#define get_entity(T, repo, ix)                                     \
    (                                                               \
        {                                                           \
            repo_t *__repo = (repo);                                \
            nodeptr __ix = (ix);                                    \
            assert(__ix.ok && (__ix.value < __repo->entities.len)); \
            entity_t *__e = __repo->entities.items + __ix.value;    \
            assert(__e->type == EntityType_##T);                    \
            ((T##_t *) &(__e->T));                                  \
        })

typedef struct _fat_pointer {
    repo_t *repo;
    nodeptr ptr;
} ptr;

#define get_ptr(p)                                                          \
    (                                                                       \
        {                                                                   \
            ptr __p = (p);                                                  \
            assert(__p.ptr.ok && (__p.ptr.value < __p.repo->entities.len)); \
            (__p.repo->entities.items + __p.ptr.value);                     \
        })

#define get_p(T, ptr)                     \
    (                                     \
        {                                 \
            entity_t *__e = get_ptr(ptr); \
            (&__e->T);                    \
        })

#define make_ptr(other, ix) ((ptr) { .repo = other.repo, .ptr = ix })

nodeptr entity_append(repo_t *repo, entity_t entity);
nodeptr _entity_append(repo_t *repo, entity_type_t t, void *entity, size_t sz);
bool    hash_entity(repo_t *repo, nodeptr p);

#define hash_ptr(p) (hash_entity(p.repo, p.ptr))

#define append_entity_s(T, r, ...)                                      \
    (                                                                   \
        {                                                               \
            T##_t __e = { __VA_ARGS__ };                                \
            (_entity_append((r), EntityType_##T, &__e, sizeof(T##_t))); \
        })

#define append_entity(T, r, e)                                          \
    (                                                                   \
        {                                                               \
            T##_t __e = (e);                                            \
            (_entity_append((r), EntityType_##T, &__e, sizeof(T##_t))); \
        })
