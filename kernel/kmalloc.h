#ifndef KMALLOC_H
#define KMALLOC_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* ---- Physical Page Manager ---- */
void     pmm_init(void);
void    *pmm_alloc_page(void);
void    *pmm_alloc_pages(size_t count);
void     pmm_free_page(void *addr);
void     pmm_free_pages(void *addr, size_t count);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

/* ---- Kernel Heap (kmalloc/kfree) ---- */
void  kmalloc_init(void);
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size);
void *kmalloc_zero(size_t size);
void  kfree(void *ptr);
size_t kheap_used(void);
size_t kheap_total(void);

/* ---- Slab Allocator (small fixed-size objects) ---- */
void  slab_init(void);
void *slab_alloc(size_t size);
void  slab_free(void *ptr);

/* ---- File Cache (for BrainFS) ---- */
void  fcache_init(void);
void *fcache_get(uint32_t cluster);      /* Get or create cached cluster */
void  fcache_invalidate(uint32_t cluster); /* Drop a cached cluster */
void  fcache_flush(void);               /* Write back all dirty cache */
void  fcache_invalidate_all(void);       /* Drop everything */
uint32_t fcache_count(void);

#endif
