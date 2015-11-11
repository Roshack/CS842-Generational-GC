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
    if (markStack->next) {
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
                    if (desc == ggggc_freeObjectDesc) {

                        objIter = objIter + ((struct GGGGC_FreeObject *) objIter)->size;
                        continue;
                    }
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

void ggggc_unmarkObject(void * x) {
    struct GGGGC_Header * obj = (struct GGGGC_Header *) returnCleanAge(x);
    obj->descriptor__ptr = (void *) (((ggc_size_t) obj->descriptor__ptr) & 0xFFFFFFFFFFFE);
}

void ggggc_markObject(void * x) {
    struct GGGGC_Header * obj = (struct GGGGC_Header *) returnCleanAge(x);
    obj->descriptor__ptr = (void *) (((ggc_size_t) obj->descriptor__ptr) | 1L);
}

void ggggc_mark() {
    void * x = StackLL_Pop();
    while (x) {
        //printf("reading location %lx", lui x);  
        struct GGGGC_Header * obj = returnCleanAge((void *) *((struct GGGGC_Header **) x));
        //printf(" which points to %lx\r\n", lui obj);
        if (obj && !alreadyMoved(obj)) {
            struct GGGGC_Header * uncleanObj =  *((struct GGGGC_Header **) x);
            struct GGGGC_Descriptor * desc = cleanForwardAddress(obj);
            ggggc_markObject(obj);
            //printf("readout 1 descriptor is %lx\r\n", lui desc);
            if (desc->pointers[0]&1) {
                long unsigned int bitIter = 1;
                int z = 0;
                //printf("readout2\r\n");
                while (z < desc->size) {
                    if (desc->pointers[0] & bitIter) {
                        void * loc = (void *) ( ((ggc_size_t *) obj) + z );
                        StackLL_Push(loc);
                    }
                    z++;
                    bitIter = bitIter<<1;
                }
            } else {
                ggggc_process((void *) obj);
            }
        }
        x = StackLL_Pop();
    }

}

void ggggc_markOld() {
    StackLL_Init();
    struct GGGGC_PointerStack *stack_iter = ggggc_pointerStack;
    while (stack_iter) {
        ggc_size_t *** ptrptr = (ggc_size_t ***) stack_iter->pointers;
        ggc_size_t ptrIter = 0;
        while (ptrIter < stack_iter->size) {
            StackLL_Push((void *) ptrptr[ptrIter]);  
            ggggc_mark();      
            ptrIter++;               
        }
        stack_iter = stack_iter->next;
    }
    StackLL_Clean();

}

void ggggc_oldSweep() {
    struct GGGGC_Pool *poolIter = ggggc_sunnyvaleRetirement;
    while (poolIter) {
        ggc_size_t *objIter = poolIter->start;
        while (objIter < poolIter->free) {
            ggggc_unmarkObject((void *) objIter);
            struct GGGGC_Header * obj = (struct GGGGC_Header *) objIter;
            objIter = objIter + obj->descriptor__ptr->size;
        }
        poolIter=poolIter->next;
    }
    poolIter = ggggc_fromList;
    while (poolIter) {
        ggc_size_t *objIter = poolIter->start;
        while (objIter < poolIter->free) {
            ggggc_unmarkObject((void *) objIter);
            struct GGGGC_Header * obj = (struct GGGGC_Header *) objIter;
            objIter = objIter + obj->descriptor__ptr->size;
        }
        poolIter=poolIter->next;
    }
}

void ggggc_collectFull(){/*
    printf("doing a full collect!\r\n");
    ggggc_markOld();
    printf("Finished mark old\r\n");
    ggggc_oldSweep();
    printf("Finished sweep\r\n");*/
    return;
}

/* run a collection */
void ggggc_collect()
{
    // Initialize our work stack.
    //printf("beginning normal collection\r\n");
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
    //printf("before pseudoroots\r\n");
    process_pseudoRoots();
    //printf("after pseudoroots\r\n");
    void * workIter = StackLL_Pop();
    while (workIter) {
        scan(workIter);
        workIter = StackLL_Pop();
    }
    StackLL_Clean();
    if(ggggc_forceFullCollect) {
        ggggc_collectFull();
    }
    //printf("finishing normal collcet\r\n");
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


ggc_size_t ageSizeT(void * x) {
    return ((ggc_size_t) x) & 6L;
}

void ggggc_process(void * x) {
    struct GGGGC_Header * obj = returnCleanAge((void *) *((struct GGGGC_Header **) x));
    struct GGGGC_Header * uncleanObj =  *((struct GGGGC_Header **) x);
    if (obj && isYoung((void *) obj)) {
        *((struct GGGGC_Header **) x) = (struct GGGGC_Header *) forward((void *) uncleanObj);
        *((struct GGGGC_Header **) x) = (struct GGGGC_Header *) (((ggc_size_t) *((struct GGGGC_Header **) x)) | ageSizeT(uncleanObj));
    }
}

long unsigned int alreadyMoved(void * x) {
    // Check if the lowest order bit of the "descriptor ptr" is set. If it is
    // then this object has been moved (and that's not a descriptor ptr but a forward address)
    struct GGGGC_Header * obj = (struct GGGGC_Header *) returnCleanAge(x);
    return (long unsigned int) ((struct GGGGC_Header *) obj)->descriptor__ptr & 1L;
}

void * cleanForwardAddress(void * x) {
    struct GGGGC_Header * header = (struct GGGGC_Header *) returnCleanAge(x);
    return (void *) ((long unsigned int) header->descriptor__ptr & 0xFFFFFFFFFFFFFFF8 );
}

void * cleanForwardBit(void * x) {
    struct GGGGC_Header * header = (struct GGGGC_Header *) returnCleanAge(x);
    return (void *) ((long unsigned int) header->descriptor__ptr & 0xFFFFFFFFFFFFFFFE );
}

void * forward(void * from)
{
    if (alreadyMoved(from)) {
        return cleanForwardBit(from);
    }
    struct GGGGC_Header * toRef = NULL;
    struct GGGGC_Header * fromRef = (struct GGGGC_Header *) returnCleanAge(from);
    struct GGGGC_Descriptor * descriptor =cleanForwardAddress(from);
    //printf("object is %lx and descriptor is %lx\r\n", lui from, lui descriptor);
    if (isOldEnough(from)) {
        // First try to find space in the free list.
        struct GGGGC_FreeObject * freeIter = ggggc_oldFreeList;
        struct GGGGC_FreeObject * lastFree = NULL;
        while (freeIter) {
            if (freeIter->size > descriptor->size) {
                // Exactly 0 is great, we can allocate. But we need to update
                // the freelist to the next then!
                if (freeIter->size - descriptor->size == 0) {
                    printf("Allocating to free object %lx with 0 remainder, freeIter next is %lx\r\n", lui freeIter, lui freeIter->next);
                    toRef = (struct GGGGC_Header *) freeIter;
                    if (lastFree) {
                        lastFree->next = freeIter->next;
                    } else {
                        ggggc_oldFreeList = freeIter->next;
                    }
                    break;
                }
                // Can't allocate where we would have less than 3 words left since
                // we need to be able to put a new free object in the remaining space
                // and free objects are 3 words!
                if (freeIter->size - descriptor->size > 2) {
                    toRef = (struct GGGGC_Header *) freeIter;
                    // Need to make a new free object for the new free space
                    struct GGGGC_FreeObject * newFree = (struct GGGGC_FreeObject *) (((ggc_size_t *)freeIter)+ descriptor->size);
                    newFree->size = (freeIter->size - descriptor->size);
                    newFree->next = freeIter->next;
                    newFree->descriptor__ptr = ggggc_freeObjectDesc;
                    if (lastFree) {
                        lastFree->next = newFree;
                    } else {
                        ggggc_oldFreeList = newFree;
                    }
                    printf("Allocating to freeobject %lx of size %lu and new free is %lx\r\n", lui freeIter, descriptor->size, lui newFree);
                    break;
                }
            }
            lastFree = freeIter;
            freeIter = freeIter->next;
            
        }
        if (!toRef) {
            //printf("object is %lx and descriptor is %lx\r\n", lui from, lui descriptor);
            //printf("GGGGC_curoldpool is %lx\r\n", lui ggggc_curOldPool);
            if (ggggc_curOldPool->free + descriptor->size < ggggc_curOldPool->end) {
                toRef = (struct GGGGC_Header *) ggggc_curOldPool->free;
                ggggc_curOldPool->free = ggggc_curOldPool->free + descriptor->size;
            } else {
                struct GGGGC_Pool *poolIter = ggggc_curOldPool->next;
                while(poolIter) {
                    if (poolIter->free + descriptor->size < poolIter->end) {
                        toRef = (struct GGGGC_Header *) poolIter->free;
                        poolIter->free = poolIter->free + descriptor->size;
                        break;
                    }
                    poolIter = poolIter->next;
                }
                if (!toRef) {
                    // We couldn't find an old pool with enough memory. We now need to do a full collect
                    // and be very sad about this fact.
                    ggggc_forceFullCollect = 1;
                }
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
    StackLL_Push((void *) toRef);
    return((void *) toRef);  
}

ggc_size_t isOldEnough(void *x) {
    ggc_size_t desc = (ggc_size_t) ((struct GGGGC_Header *) x)->descriptor__ptr;
    return (desc & (1L<<2));
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
    struct GGGGC_Header * header = (struct GGGGC_Header *) returnCleanAge(x);
    if (isHalfCollected(x)) {
        return;
    }
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

