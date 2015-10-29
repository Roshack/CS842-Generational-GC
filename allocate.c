/*
 * Allocation functions
 *
 * Copyright (c) 2014, 2015 Gregor Richards
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _BSD_SOURCE /* for MAP_ANON */
#define _DARWIN_C_SOURCE /* for MAP_ANON on OS X */

/* for standards info */
#if defined(unix) || defined(__unix) || defined(__unix__) || \
    (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#if _POSIX_VERSION
#include <sys/mman.h>
#endif

/* REMOVE THIS FOR SUBMISSION! */
#include <gc.h>
/* --- */

#include "ggggc/gc.h"
#include "ggggc-internals.h"

#ifdef __cplusplus
extern "C" {
#endif

/* figure out which allocator to use */
#if defined(GGGGC_USE_MALLOC)
#define GGGGC_ALLOCATOR_MALLOC 1
#include "allocate-malloc.c"

#elif _POSIX_ADVISORY_INFO >= 200112L
#define GGGGC_ALLOCATOR_POSIX_MEMALIGN 1
#include "allocate-malign.c"

#elif defined(MAP_ANON)
#define GGGGC_ALLOCATOR_MMAP 1
#include "allocate-mmap.c"

#elif defined(_WIN32)
#define GGGGC_ALLOCATOR_VIRTUALALLOC 1
#include "allocate-win-valloc.c"

#else
#warning GGGGC: No allocator available other than malloc!
#define GGGGC_ALLOCATOR_MALLOC 1
#include "allocate-malloc.c"

#endif

/* pools which are freely available */
static struct GGGGC_Pool *freePoolsHead, *freePoolsTail;

/* allocate and initialize a pool */
static struct GGGGC_Pool *newPool(int mustSucceed)
{
    struct GGGGC_Pool *ret;

    ret = NULL;

    /* try to reuse a pool */
    if (freePoolsHead) {
        if (freePoolsHead) {
            ret = freePoolsHead;
            freePoolsHead = freePoolsHead->next;
            if (!freePoolsHead) freePoolsTail = NULL;
        }
    }

    /* otherwise, allocate one */
    if (!ret) ret = (struct GGGGC_Pool *) allocPool(mustSucceed);

    if (!ret) return NULL;

    /* set it up */
    ret->next = NULL;
    ret->free = ret->start;
    ret->end = (ggc_size_t *) ((unsigned char *) ret + GGGGC_POOL_BYTES);

    return ret;
}

/* heuristically expand a generation if it has too many survivors */
void ggggc_expandGeneration(struct GGGGC_Pool *pool)
{
    ggc_size_t space, survivors, poolCt;

    if (!pool) return;

    /* first figure out how much space was used */
    space = 0;
    survivors = 0;
    poolCt = 0;
    while (1) {
        space += pool->end - pool->start;
        survivors += pool->survivors;
        pool->survivors = 0;
        poolCt++;
        if (!pool->next) break;
        pool = pool->next;
    }

    /* now decide if it's too much */
    if (survivors > space/2) {
        /* allocate more */
        ggc_size_t i;
        for (i = 0; i < poolCt; i++) {
            pool->next = newPool(0);
            pool = pool->next;
            if (!pool) break;
        }
    }
}

/* free a generation (used when a thread exits) */
void ggggc_freeGeneration(struct GGGGC_Pool *pool)
{
    if (!pool) return;
    if (freePoolsHead) {
        freePoolsTail->next = pool;
    } else {
        freePoolsHead = pool;
    }
    while (pool->next) pool = pool->next;
    freePoolsTail = pool;
}

/* allocate an object */
void *ggggc_malloc(struct GGGGC_Descriptor *descriptor)
{
    /* FILLME */
    GGC_YIELD();
    return GC_MALLOC(descriptor->size * sizeof(void*));
}

struct GGGGC_Array {
    struct GGGGC_Header header;
    ggc_size_t length;
};

/* allocate a pointer array (size is in words) */
void *ggggc_mallocPointerArray(ggc_size_t sz)
{
    struct GGGGC_Descriptor *descriptor = ggggc_allocateDescriptorPA(sz + 1 + sizeof(struct GGGGC_Header)/sizeof(ggc_size_t));
    struct GGGGC_Array *ret = (struct GGGGC_Array *) ggggc_malloc(descriptor);
    ret->length = sz;
    return ret;
}

/* allocate a data array */
void *ggggc_mallocDataArray(ggc_size_t nmemb, ggc_size_t size)
{
    ggc_size_t sz = ((nmemb*size)+sizeof(ggc_size_t)-1)/sizeof(ggc_size_t);
    struct GGGGC_Descriptor *descriptor = ggggc_allocateDescriptorDA(sz + 1 + sizeof(struct GGGGC_Header)/sizeof(ggc_size_t));
    struct GGGGC_Array *ret = (struct GGGGC_Array *) ggggc_malloc(descriptor);
    ret->length = nmemb;
    return ret;
}

/* allocate a descriptor-descriptor for a descriptor of the given size */
struct GGGGC_Descriptor *ggggc_allocateDescriptorDescriptor(ggc_size_t size)
{
    struct GGGGC_Descriptor tmpDescriptor, *ret;
    ggc_size_t ddSize;

    /* need one description bit for every word in the object */
    ddSize = GGGGC_WORD_SIZEOF(struct GGGGC_Descriptor) + GGGGC_DESCRIPTOR_WORDS_REQ(size);

    /* check if we already have a descriptor */
    if (ggggc_descriptorDescriptors[size])
        return ggggc_descriptorDescriptors[size];

    /* otherwise, need to allocate one. First lock the space */
    if (ggggc_descriptorDescriptors[size]) {
        return ggggc_descriptorDescriptors[size];
    }

    /* now make a temporary descriptor to describe the descriptor descriptor */
    tmpDescriptor.header.descriptor__ptr = NULL;
    tmpDescriptor.size = ddSize;
    tmpDescriptor.pointers[0] = GGGGC_DESCRIPTOR_DESCRIPTION;

    /* allocate the descriptor descriptor */
    ret = (struct GGGGC_Descriptor *) ggggc_malloc(&tmpDescriptor);

    /* make it correct */
    ret->size = size;
    ret->pointers[0] = GGGGC_DESCRIPTOR_DESCRIPTION;

    /* put it in the list */
    ggggc_descriptorDescriptors[size] = ret;
    GGC_PUSH_1(ggggc_descriptorDescriptors[size]);
    GGC_GLOBALIZE();

    /* and give it a proper descriptor */
    ret->header.descriptor__ptr = ggggc_allocateDescriptorDescriptor(ddSize);

    return ret;
}

/* allocate a descriptor for an object of the given size in words with the
 * given pointer layout */
struct GGGGC_Descriptor *ggggc_allocateDescriptor(ggc_size_t size, ggc_size_t pointers)
{
    ggc_size_t pointersA[1];
    pointersA[0] = pointers;
    return ggggc_allocateDescriptorL(size, pointersA);
}

/* descriptor allocator when more than one word is required to describe the
 * pointers */
struct GGGGC_Descriptor *ggggc_allocateDescriptorL(ggc_size_t size, const ggc_size_t *pointers)
{
    struct GGGGC_Descriptor *dd, *ret;
    ggc_size_t dPWords, dSize;

    /* the size of the descriptor */
    if (pointers)
        dPWords = GGGGC_DESCRIPTOR_WORDS_REQ(size);
    else
        dPWords = 1;
    dSize = GGGGC_WORD_SIZEOF(struct GGGGC_Descriptor) + dPWords;

    /* get a descriptor-descriptor for the descriptor we're about to allocate */
    dd = ggggc_allocateDescriptorDescriptor(dSize);

    /* use that to allocate the descriptor */
    ret = (struct GGGGC_Descriptor *) ggggc_malloc(dd);
    ret->size = size;

    /* and set it up */
    if (pointers) {
        memcpy(ret->pointers, pointers, sizeof(ggc_size_t) * dPWords);
        ret->pointers[0] |= 1; /* first word is always the descriptor pointer */
    } else {
        ret->pointers[0] = 0;
    }

    return ret;
}

/* descriptor allocator for pointer arrays */
struct GGGGC_Descriptor *ggggc_allocateDescriptorPA(ggc_size_t size)
{
    ggc_size_t *pointers;
    ggc_size_t dPWords, i;

    /* fill our pointer-words with 1s */
    dPWords = GGGGC_DESCRIPTOR_WORDS_REQ(size);
    pointers = (ggc_size_t *) alloca(sizeof(ggc_size_t) * dPWords);
    for (i = 0; i < dPWords; i++) pointers[i] = (ggc_size_t) -1;

    /* get rid of non-pointers */
    pointers[0] &= ~0x4;

    /* and allocate */
    return ggggc_allocateDescriptorL(size, pointers);
}

/* descriptor allocator for data arrays */
struct GGGGC_Descriptor *ggggc_allocateDescriptorDA(ggc_size_t size)
{
    /* and allocate */
    return ggggc_allocateDescriptorL(size, NULL);
}

/* allocate a descriptor from a descriptor slot */
struct GGGGC_Descriptor *ggggc_allocateDescriptorSlot(struct GGGGC_DescriptorSlot *slot)
{
    if (slot->descriptor) return slot->descriptor;
    if (slot->descriptor) {
        return slot->descriptor;
    }

    slot->descriptor = ggggc_allocateDescriptor(slot->size, slot->pointers);

    /* make the slot descriptor a root */
    GGC_PUSH_1(slot->descriptor);
    GGC_GLOBALIZE();

    return slot->descriptor;
}

/* and a combined malloc/allocslot */
void *ggggc_mallocSlot(struct GGGGC_DescriptorSlot *slot)
{
    return ggggc_malloc(ggggc_allocateDescriptorSlot(slot));
}

#ifdef __cplusplus
}
#endif
