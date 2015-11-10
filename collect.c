/*
 * The collector
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "ggggc/gc.h"
#include "ggggc-internals.h"

#ifdef __cplusplus
extern "C" {
#endif

struct StackLL 
{
    void *data;
    struct StackLL *next;
};

struct StackLL * markStack;

void StackLL_Init()
{
    markStack = (struct StackLL *)malloc(sizeof(struct StackLL));
    markStack->data = NULL;
    markStack->next = NULL;
}

void StackLL_Push(void *x)
{
    struct StackLL *new = (struct StackLL *)malloc(sizeof(struct StackLL));
    new->data = x;
    new->next = markStack;
    markStack = new;
}

void * StackLL_Pop()
{
    void * x = markStack->data;
    struct StackLL *old = markStack;
    if (x) {
        markStack = markStack->next;
        free(old);
    }
    return x;
}

void StackLL_Clean()
{
    while(markStack->next) {
        StackLL_Pop();
    }
    free(markStack);
}

void process_pseudoRoots() {
    struct GGGGC_Pool * poolIter = ggggc_sunnyvaleRetirement;
    while(poolIter) {
        ggc_size_t cardIter = 0;
        while (cardIter < GGGGC_CARDS_PER_POOL) {
            if (poolIter->remember[cardIter]) {
                ggc_size_t * objIter = ((ggc_size_t *) poolIter) + cardIter*GGGGC_WORDS_PER_CARD + poolIter->firstObjs[cardIter];
                ggc_size_t * cardEnd = ((ggc_size_t *) poolIter) + (cardIter+1)*GGGGC_WORDS_PER_CARD;
                int foundPseudoRoot = 0;
                while (objIter < cardEnd) {
                    struct GGGGC_Descriptor * desc = (struct GGGGC_Descriptor *) cleanForwardAddress((void *) objIter);
                    if (desc->pointers[0]&1) {
                        long unsigned int bitIter = 1;
                        int z = 0;
                        while (z < desc->size) {
                            if (desc->pointers[0] & bitIter) {
                                void * loc = (void *) (objIter +z);
                                ggggc_process(loc);
                                struct GGGGC_Header * obj = *((struct GGGGC_Header **) loc);
                                if (isYoung(obj)) {
                                    foundPseudoRoot = 1;
                                }
                            }
                            z++;
                            bitIter = bitIter<<1;
                        }
                    }
                    objIter = objIter + desc->size;
                }
                if (!foundPseudoRoot) {
                    poolIter->remember[cardIter] = 0;
                    poolIter->firstObjs[cardIter] = GGGGC_FIRST_OBJ_DEFAULT;
                }
            }
            cardIter++;
        }
        poolIter = poolIter->next;
    }
}

void ggggc_collectFull(){
    StackLL_Init();
    ggggc_forceFullCollect = 0;

    StackLL_Clean();
    return;
}

/* run a collection */
void ggggc_collect()
{
    // Initialize our work stack.
    StackLL_Init();
    struct GGGGC_PointerStack *stack_iter = ggggc_pointerStack;
    // Set the curpool to the toList so we can allocate to the curpool and update it
    // should we have more than one pool worth of live objects.
    ggggc_curPool = ggggc_toList;
    ggggc_toList = ggggc_fromList;
    ggggc_fromList = ggggc_curPool;
    while(ggggc_curPool) {
        ggggc_curPool->free = ggggc_curPool->start;
        ggggc_curPool = ggggc_curPool->next;
    }
    ggggc_curPool = ggggc_fromList;
    while (stack_iter) {
        ggc_size_t *** ptrptr = (ggc_size_t ***) stack_iter->pointers;
        ggc_size_t ptrIter = 0;
        while (ptrIter < stack_iter->size) {
            ggggc_process((void *) ptrptr[ptrIter]);        
            ptrIter++;               
        }
        stack_iter = stack_iter->next;
    }
    process_pseudoRoots();
    void * workIter = StackLL_Pop();
    while (workIter) {
        scan(workIter);
        workIter = StackLL_Pop();
    }
    StackLL_Clean();
    if(ggggc_forceFullCollect) {
        ggggc_collectFull();
    }
    // Now updated references.
    // Deprecated since implementing a cleaner cheney's algorithm.
    //ggggc_updateRefs();
}



void scan(void *x) {
    struct GGGGC_Header * ref = (struct GGGGC_Header *) x;
    if (!x) {
        return;
    }
    struct GGGGC_Descriptor * desc = cleanForwardAddress(x);
    if (desc->pointers[0]&1) {
        long unsigned int bitIter = 1;
        int z = 0;
        while (z < desc->size) {
            if (desc->pointers[0] & bitIter) {
                void * loc = (void *) ( ((ggc_size_t *) ref) + z );
                ggggc_process(loc);
            }
            z++;
            bitIter = bitIter<<1;
        }
    } else {
        ggggc_process((void *) ref);
    }
}


void ggggc_process(void * x) {
    struct GGGGC_Header * obj = *((struct GGGGC_Header **) x);
    if (obj && isYoung((void *) obj)) {
        *((struct GGGGC_Header **) x) = (struct GGGGC_Header *) forward((void *) obj);
    }
}

long unsigned int alreadyMoved(void * x) {
    // Check if the lowest order bit of the "descriptor ptr" is set. If it is
    // then this object has been moved (and that's not a descriptor ptr but a forward address)
    return (long unsigned int) ((struct GGGGC_Header *) x)->descriptor__ptr & 1L;
}

void * cleanForwardAddress(void * x) {
    struct GGGGC_Header * header = (struct GGGGC_Header *) x;
    return (void *) ((long unsigned int) header->descriptor__ptr & 0xFFFFFFFFFFFFFFF8 );
}

void * forward(void * toBeForwarded)
{
    void * from = returnCleanAge(toBeForwarded);
    if (alreadyMoved(from)) {
        return cleanForwardAddress(from);
    }
    struct GGGGC_Header * toRef = NULL;
    struct GGGGC_Header * fromRef = (struct GGGGC_Header *) from;
    struct GGGGC_Descriptor * descriptor =cleanForwardAddress(from);
    if (isOldEnough(from)) {
        if (ggggc_curOldPool->free + descriptor->size < ggggc_curOldPool->end) {
            toRef = (struct GGGGC_Header *) ggggc_curOldPool->free;
            ggggc_curOldPool->free = ggggc_curOldPool->free + descriptor->size;
        } else {
            ggggc_curOldPool = ggggc_curOldPool->next;
            while(ggggc_curOldPool) {
                if (ggggc_curOldPool->free + descriptor->size < ggggc_curOldPool->end) {
                    toRef = (struct GGGGC_Header *) ggggc_curOldPool->free;
                    ggggc_curOldPool->free = ggggc_curOldPool->free + descriptor->size;
                    break;
                }
                ggggc_curOldPool = ggggc_curOldPool->next;
            }
            if (!toRef) {
                // We couldn't find an old pool with enough memory. We now need to do a full collect
                // and be very sad about this fact.
                ggggc_forceFullCollect = 1;
            }
        } 
    }
    if (!toRef) {
        if (ggggc_curPool->free + descriptor->size < ggggc_curPool->end) {
            toRef = (struct GGGGC_Header *) ggggc_curPool->free;
            ggggc_curPool->free = ggggc_curPool->free + descriptor->size;
        } else {
            // Next pool should always be empty because it must be since we're in to space.
            // Which means assuming we're not allocating any objects larger than pool we can just do it
            ggggc_curPool = ggggc_curPool->next;
            toRef = (struct GGGGC_Header *) (ggggc_curPool->free);
            ggggc_curPool->free = ggggc_curPool->free + descriptor->size;
        }
        if (isOldEnough(from)) {
            // If object was old enough and we got here we need to mark it as being half collected
            // since it should have gotten promoted. This means it's third and second lowest order bit
            // are both 1, essentially meaning it survived 3 collections which is 1 more than it needs to
            // to get promoted.
            incrementAge(from);
        }
    }
    memcpy(toRef,fromRef,sizeof(ggc_size_t)*descriptor->size);
    fromRef->descriptor__ptr = (struct GGGGC_Descriptor *) ( ((ggc_size_t) toRef) | 1L);
    if(isOldEnough(from) && !isHalfCollected(from)) {
        // If it's old enough and it's not half collected me promoted it succesfully.
        // need to maybe update cards if necessary.
        
        cleanAge((void *) toRef);
        struct GGGGC_Descriptor * desc = toRef->descriptor__ptr;
        if (desc->pointers[0]&1) {
            long unsigned int bitIter = 1;
            int z = 0;
            while (z < desc->size) {
                if (desc->pointers[0] & bitIter) {
                    ggc_size_t * loc = (ggc_size_t *) ( ((ggc_size_t *) toRef) + z );
                    GGGGC_WC((void *) toRef, (void *) *loc);
                }
                z++;
                bitIter = bitIter<<1;
            }
        } else {
            GGGGC_WC((void *) toRef, (void *) toRef->descriptor__ptr);
        }
    } else if (!isHalfCollected(from)) {
        // Do not increment the age of something that is half collected meaning it's
        // lowest seconmd and third order bits are both 1s because then you'll overflow
        // into actually meaningful bits!
        incrementAge((void *) toRef);
    }
    StackLL_Push(toRef);
    return(toRef);  
}

ggc_size_t isOldEnough(void *x) {
    ggc_size_t header = (ggc_size_t) x;
    return (header & (1L<<2));
}

int isHalfCollected(void *x) {
    struct GGGGC_Header * header = (struct GGGGC_Header *) x;
    ggc_size_t firstCheck = ((ggc_size_t) header->descriptor__ptr) & (1L << 1);
    ggc_size_t secondCheck = ((ggc_size_t) header->descriptor__ptr) & (1L << 2);
    return (firstCheck && secondCheck);
}

void cleanAge(void * x) {
    struct GGGGC_Header * header = (struct GGGGC_Header *) x;
    header->descriptor__ptr = (struct GGGGC_Descriptor *) ( ((ggc_size_t) header->descriptor__ptr) & ~(3L << 1) );
}

void incrementAge(void *x) {
    // If the object has survived 0 GC cycles yet this will set it's first age bit,
    // if it's survived one already the first age bit will carry to the next age bit!
    struct GGGGC_Header * header = (struct GGGGC_Header *) x;
    header->descriptor__ptr = (struct GGGGC_Descriptor *) ( ((ggc_size_t) header->descriptor__ptr) + (1L << 1) );
}

void * returnCleanAge(void *x) {
    return (void *) (((ggc_size_t) x) & 0xFFFFFFFFFFFFFFF9);
}

int ggggc_yield()
{
    //printf("Going to yield\r\n");
    if (ggggc_forceCollect) {
        ggggc_collect();
        ggggc_forceCollect = 0;    
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
