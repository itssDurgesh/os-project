/* kmalloc: small requests use a slab cache of the next size up, and big ones get
 * whole frames. Slab objects never sit on a page boundary because of the slab
 * header, so kfree() can tell the two kinds apart from the address alone. */

#include "kstring.h"
#include "memory.h"

#define SIZE_CLASSES 8
#define LARGEST_SLAB_OBJECT 2016    /* 4096 - 32 byte header, so one fits */

static const size_t class_sizes[SIZE_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, LARGEST_SLAB_OBJECT
};

static struct SlabCache caches[SIZE_CLASSES];

void kmalloc_init(void)
{
    for (int i = 0; i < SIZE_CLASSES; i++)
        slab_cache_init(&caches[i], class_sizes[i]);
}

void *kmalloc(size_t size)
{
    for (int i = 0; i < SIZE_CLASSES; i++) {
        if (size <= class_sizes[i])
            return slab_alloc(&caches[i]);
    }

    uint32_t frames = alloc_frames(round_up_to_page(size) / PAGE_SIZE);

    return (frames == 0) ? NULL : phys_to_virt(frames);
}

void *kmalloc_zeroed(size_t size)
{
    void *pointer = kmalloc(size);

    if (pointer != NULL)
        memset(pointer, 0, size);
    return pointer;
}

void kfree(void *pointer)
{
    if (pointer == NULL)
        return;

    if (((uint32_t)pointer & ~PAGE_MASK) == 0)
        free_frames(virt_to_phys(pointer));
    else
        slab_free(pointer);
}
