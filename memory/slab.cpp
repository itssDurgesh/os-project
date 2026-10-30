// Slab allocator. Each slab is one frame: a header, then equal-sized object
// slots. Free slots are chained together inside the slots themselves.

#include "console.h"
#include "memory.h"

#define SLAB_HEADER_SIZE 32  // keeps objects 16-byte aligned

struct Slab {
    SlabCache *cache;
    Slab *previous;
    Slab *next;
    void *free_list;
    uint32_t used;
};

static_assert(sizeof(Slab) <= SLAB_HEADER_SIZE, "slab header too big");

static void list_add(SlabCache *cache, Slab *slab)
{
    slab->previous = nullptr;
    slab->next = cache->partial;
    if (cache->partial != nullptr)
        cache->partial->previous = slab;
    cache->partial = slab;
}

static void list_remove(SlabCache *cache, Slab *slab)
{
    if (slab->previous != nullptr)
        slab->previous->next = slab->next;
    else
        cache->partial = slab->next;

    if (slab->next != nullptr)
        slab->next->previous = slab->previous;
}

static Slab *slab_create(SlabCache *cache)
{
    uint32_t frame = alloc_frame();
    Slab *slab;
    char *object;

    if (frame == 0)
        return nullptr;

    slab = (Slab *)phys_to_virt(frame);
    slab->cache = cache;
    slab->used = 0;
    slab->free_list = nullptr;

    object = (char *)slab + SLAB_HEADER_SIZE;
    for (uint32_t i = 0; i < cache->objects_per_slab; i++) {
        *(void **)object = slab->free_list;
        slab->free_list = object;
        object += cache->object_size;
    }

    list_add(cache, slab);
    return slab;
}

void slab_cache_init(SlabCache *cache, size_t object_size)
{
    if (object_size < sizeof(void *))
        object_size = sizeof(void *);

    cache->object_size = object_size;
    cache->objects_per_slab = (PAGE_SIZE - SLAB_HEADER_SIZE) / object_size;
    cache->partial = nullptr;

    if (cache->objects_per_slab == 0)
        panic("slab object too big: %u bytes", (uint32_t)object_size);
}

void *slab_alloc(SlabCache *cache)
{
    Slab *slab = cache->partial;
    void *object;

    if (slab == nullptr) {
        slab = slab_create(cache);
        if (slab == nullptr)
            return nullptr;
    }

    object = slab->free_list;
    slab->free_list = *(void **)object;
    slab->used++;

    if (slab->free_list == nullptr)
        list_remove(cache, slab);

    return object;
}

void slab_free(void *object)
{
    Slab *slab = (Slab *)((uint32_t)object & PAGE_MASK);
    SlabCache *cache = slab->cache;

    if (slab->free_list == nullptr)
        list_add(cache, slab);  // it was full, so it wasn't on the list

    *(void **)object = slab->free_list;
    slab->free_list = object;

    if (--slab->used == 0) {
        list_remove(cache, slab);
        free_frame(virt_to_phys(slab));
    }
}
