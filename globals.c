#include "ggggc-internals.h"

/* publics */
struct GGGGC_PointerStack *ggggc_pointerStack, *ggggc_pointerStackGlobals;

/* internals */
struct GGGGC_Pool *ggggc_poolList;
struct GGGGC_Pool *ggggc_curPool;
struct GGGGC_Descriptor *ggggc_descriptorDescriptors[GGGGC_WORDS_PER_POOL/GGGGC_BITS_PER_WORD+sizeof(struct GGGGC_Descriptor)];
