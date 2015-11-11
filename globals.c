#include "ggggc-internals.h"

/* publics */
struct GGGGC_PointerStack *ggggc_pointerStack, *ggggc_pointerStackGlobals;

/* internals */
struct GGGGC_Pool *ggggc_fromList;
struct GGGGC_Pool *ggggc_toList;
struct GGGGC_Pool *ggggc_sunnyvaleRetirement;
struct GGGGC_Pool *ggggc_curPool;
struct GGGGC_Pool *ggggc_curOldPool;
struct GGGGC_Descriptor *ggggc_descriptorDescriptors[GGGGC_WORDS_PER_POOL/GGGGC_BITS_PER_WORD+sizeof(struct GGGGC_Descriptor)];

struct GGGGC_Descriptor *ggggc_freeObjectDesc;

struct GGGGC_FreeObject *ggggc_oldFreeList;

int ggggc_forceCollect;
int ggggc_forceFullCollect;
int ggggc_forceOldExpand;

static struct GGGGC_Pool *newPool(int mustSucceed);
