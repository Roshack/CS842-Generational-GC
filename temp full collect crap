void ggggc_unmarkObject(void * x) {
    struct GGGGC_Header * obj = (struct GGGGC_Header *) returnCleanAge(x);
    obj->descriptor__ptr = (void *) (((ggc_size_t) obj->descriptor__ptr) & 0xFFFFFFFFFFFFFFFE);
}

void ggggc_markObject(void * x) {
    struct GGGGC_Header * obj = (struct GGGGC_Header *) returnCleanAge(x);
    obj->descriptor__ptr = (void *) (((ggc_size_t) obj->descriptor__ptr) | 1L);
}

void ggggc_markHelper() {
    // Takes in a pointer to a pointer much like process...
    void * x = StackLL_Pop();
    while (x) {
        // using alreadyMoved since it is identical to what isMarked would be.
        //printf("marking object %lx\r\n", lui x);
        struct GGGGC_Header * obj = returnCleanAge((void *) *((struct GGGGC_Header **) x));
        if(!obj) {
            x = StackLL_Pop();
            continue;
        }
        if(alreadyMoved(obj)) {
            x = StackLL_Pop();
            continue;
        }
        struct GGGGC_Descriptor *desc = (struct GGGGC_Descriptor *) cleanForwardAddress(obj);
        ggggc_markObject(obj);
        if (desc->pointers[0]&1) {
            long unsigned int bitIter = 1;
            int z = 0;
            while (z < desc->size) {
                //printf("B) z is %d and size is %ld\r\n", z, desc->size);
                if (desc->pointers[0] & bitIter) {
                    void * loc = (void *) ( ((ggc_size_t *) obj) + z );
                    StackLL_Push(loc);
                }
                z++;
                bitIter = bitIter<<1;
            }
        } else {
            StackLL_Push((void *) desc);
        }
        x = StackLL_Pop();
    }
}

void ggggc_markOld() {
    struct GGGGC_PointerStack *stack_iter = ggggc_pointerStack;
    while (stack_iter) {
        ggc_size_t *** ptrptr = (ggc_size_t ***) stack_iter->pointers;
        ggc_size_t ptrIter = 0;
        while (ptrIter < stack_iter->size) {
            //printf("Pushing %lx\r\n", lui *((struct GGGGC_Header **) ptrptr[ptrIter]));
            StackLL_Push((void *) ptrptr[ptrIter]);
            ggggc_markHelper();
            ptrIter++;
        }
        //printf("stackiter is %lx\r\n", lui stack_iter);
        stack_iter = stack_iter->next;
    }
}



void ggggc_sweepOld() {
    struct GGGGC_Pool *poolIter = ggggc_sunnyvaleRetirement;
    struct GGGGC_FreeObject *lastFree = NULL;
    struct GGGGC_FreeObject *current = NULL;
    ggggc_oldFreeList = NULL;    
    while (poolIter) {
        ggc_size_t * objIter = poolIter->start;
        while (objIter < poolIter->free && objIter) {
            //printf("sweeping %lx\r\n", lui objIter);
            struct GGGGC_Descriptor *desc = cleanForwardAddress((void *) objIter);
            ggc_size_t size = desc->size;
            if (alreadyMoved(objIter)) {
                // If an object is alive it might have innergenerational pointers we
                // need to keep track of!
                if (desc->pointers[0]&1) {
                    long unsigned int bitIter = 1;
                    int z = 0;
                    while (z < desc->size) {
                        if (desc->pointers[0] & bitIter) {
                            ggc_size_t * loc = (ggc_size_t *) ( ((ggc_size_t *) objIter) + z );
                            GGGGC_WC((void *) objIter, (void *) *loc);
                        }
                        z++;
                        bitIter = bitIter<<1;
                    }
                } else {
                    GGGGC_WC((void *) objIter, (void *) desc);
                }
                ggggc_unmarkObject(objIter);
                if (lastFree) {
                    printf("saving freeobject %lx of size %lu\r\n", lui lastFree, lastFree->size);
                    if (!ggggc_oldFreeList) {
                        lastFree->next = NULL;
                        ggggc_oldFreeList = lastFree;
                        current = lastFree;
                    } else {
                        current->next = lastFree;
                        lastFree->next = NULL;
                        current = lastFree;
                    }
                    lastFree = NULL;
                }
            } else {
                // If it's free we need to update our free info...
                if (lastFree) {
                    // If last free exists that means the last thing we found
                    // was ALSO free object.. coalesce by adding the size 
                    // of the object to the size of last free! That's all you need to do!
                    // I think... ^_^
                    //printf("Coalescing object at %lx of size %lu into object %lx\r\n", lui objIter, size, lui lastFree);
                    lastFree->size += size;
                } else {
                    printf("Freeing object at %lx of size %lu\r\n", lui objIter, size);
                    printf("object %lx descriptor is %lx and that descriptor is %lx\r\n",lui objIter, lui desc, lui ggggc_freeObjectDesc);
                    // If last free is null the last object we found was a live object
                    // so last free needs to be made anew with this new free object!
                    lastFree = (struct GGGGC_FreeObject *) objIter;
                    lastFree->size = size;
                    lastFree->descriptor__ptr = ggggc_freeObjectDesc;
                }
            }
            objIter = objIter + size;
        }
        if (lastFree) {
            // If we get here and lastfree is a thing that means the last object in this pool was af ree object
            // (and possibly many before it as well), so taht means we need to save this free object since we
            // haven't saved it yet!
            printf("saving freeobject %lx of size %lu\r\n", lui lastFree, lastFree->size);
            if (!ggggc_oldFreeList) {
                lastFree->next = NULL;
                ggggc_oldFreeList = lastFree;
                current = lastFree;
            }else {
                current->next = lastFree;
                lastFree->next = NULL;
                current = lastFree;
            }
            lastFree = NULL;
        }
        poolIter = poolIter->next;
    }
}

void ggggc_collectOld() {
    // Collect our old.
    StackLL_Init();
    ggggc_markOld();
    printf("finished mark old\r\n");
    ggggc_sweepOld();
    printf("finished sweep old\r\n");
    StackLL_Clean();
}

void ggggc_collectYoung() {
    // Young collection called in collectfull. Don't just call our regular collect
    // because we don't want to do aging since this is happening immediately after
    // we just aged them in our initial collect call. This of course means when
    // ggggc_collectFull is called from outside the collector it won't age things
    // but in real use it wouldn't be, just some test cases do do that but that's okay.
    // The other reason this needs to be different than our normal collect is so as not
    // to create an infinite loop with trying to promote objects when the old partition
    // is full, so if the old partition is still full when we're in THIS collection of young
    // then we know we actaully do need to just expand the old partition!.
    struct GGGGC_Pool *poolIter =  ggggc_fromList;
    while (poolIter) {
        ggc_size_t * objIter = poolIter->start;
        while (objIter < poolIter->free && objIter) {
            //printf("unmarking %lx\r\n", lui objIter);
            ggggc_unmarkObject(objIter);
            struct GGGGC_Descriptor *desc = cleanForwardAddress((void *) objIter);
            if(desc) {
                ggc_size_t size = desc->size;
                objIter = objIter + size;
                if(!size) {
                    //printf("Descriptor pointer %lx has size 0 wtf\r\n", lui desc);
                    break;
                }
            } else {
                //printf("descriptor was 0 for %lx wtf\r\n", lui objIter);
                break;
            }
        }
        poolIter=poolIter->next;
    }
    return;
}

void ggggc_collectFull(){
    printf("doing a full collect!\r\n");
    // When we do a full collect we can also find out about what cards still need to actually be remembered.
    // so we will wipe that info and set it in as we go.
    struct GGGGC_Pool *poolIter = ggggc_sunnyvaleRetirement;
    while (poolIter) {
        ggc_size_t i = 0;
        for (i = 0; i < GGGGC_CARDS_PER_POOL; i++) {
            poolIter->remember[i] = 0;
            poolIter->firstObjs[i] = GGGGC_FIRST_OBJ_DEFAULT;
        }
        poolIter = poolIter->next;
    }
    ggggc_forceFullCollect = 0;
    ggggc_collectOld();
    printf("finished collect old\r\n");
    ggggc_collectYoung();
    printf("finished collect young\r\n");
    printf("Finishing full collect\r\n");
    return;
}

