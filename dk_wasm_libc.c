/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kingizor
 * dkcomp library - malloc and friends for WebAssembly */

#include <stddef.h>

void *memcpy (void *restrict dest, const void *restrict src, size_t n) {
    return __builtin_memcpy(dest, src, n);
}
void *memmove (void *dest, const void *src, size_t n) {
    return __builtin_memmove(dest, src, n);
}
void *memset (void *s, int c, size_t n) {
    return __builtin_memset(s, c, n);
}

void *bsearch (const void *key, const void *base, size_t n, size_t size, typeof(int (const void*, const void*)) *compar) {
    size_t lo = 0, hi = n;
    if (!n) return NULL;
    for (;;) {
        size_t ofs = (hi-lo) / 2;
        void *el = (char*)base + (lo+ofs)*size;
        int cmp  = compar(key, el);
             if (cmp  > 0) lo += ofs;
        else if (cmp <  0) hi -= ofs;
        else if (cmp == 0) return el;
             if (ofs == 0) return NULL;
    }
}

#define MDAT_LIMIT 256

struct MDAT {
    struct MDAT_ENTRY {
        void *start;
        void *end;
    } entry[MDAT_LIMIT];
    int active;
};
#define MDAT_TAILS(X) (sizeof(struct MDAT_ENTRY) * (mdat.active - (X - mdat.entry)))

static struct MDAT mdat = {0};
extern char __heap_base[];

static int sort_mdat (const void *aa, const void *bb) {
    const struct MDAT_ENTRY *a = aa, *b = bb;
    return (a->start < b->start) ? -1
         : (a->start > b->start);
}

void *malloc (size_t size) {
    void *start, *end = (char*)(__builtin_wasm_memory_size(0) << 16);
    int i;

    if (mdat.active == MDAT_LIMIT)
        return NULL;

    if (size & 7) size += 8 - (size & 7);

    /* check if any spaces contain enough reusable space */
    for (i = 0; i < mdat.active-1; i++) {
        struct MDAT_ENTRY *a = &mdat.entry[i];
        struct MDAT_ENTRY *b = &mdat.entry[i+1];
        if (size < (size_t)(b->start - a->end)) {
            __builtin_memmove(b+1, b, MDAT_TAILS(b));
            b->start = a->end;
            b->end   = a->end + size;
            return b->start;
        }
    }

    /* check if heap contains enough space */
    start = (mdat.active) ? mdat.entry[mdat.active-1].end : __heap_base;
    if ((size_t)(end - start) < size)
        return NULL;

    mdat.entry[mdat.active].start = start;
    mdat.entry[mdat.active].end   = start + size;
    mdat.active++;
    return start;
}

void *calloc (size_t n, size_t size) {
    void *data = malloc(n*size);
    if (data == NULL) return NULL;
    __builtin_memset(data, 0, n*size);
    return data;
}

void free (void *ptr) {
    struct MDAT_ENTRY en = { ptr, NULL };
    struct MDAT_ENTRY *target;
    if (ptr    == NULL) return;
    target = bsearch(&en, mdat.entry, mdat.active, sizeof(struct MDAT_ENTRY), sort_mdat);
    if (target == NULL) return;
    __builtin_memmove(target, target+1, MDAT_TAILS(target));
    mdat.active--;
}
