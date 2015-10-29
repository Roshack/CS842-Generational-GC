#include <stdio.h>
#include <stdlib.h>

#include "ggggc/gc.h"
// make projecttest

GGC_TYPE(GraphNode)
    GGC_MPTR(GraphNode, First);
    GGC_MPTR(GraphNode, Second);
    GGC_MPTR(GraphNode, Third);
    GGC_MPTR(GraphNode, Fourth);
    GGC_MDATA(int,check);
GGC_END_TYPE(GraphNode,
    GGC_PTR(GraphNode, First)
    GGC_PTR(GraphNode, Second)
    GGC_PTR(GraphNode, Third)
    GGC_PTR(GraphNode, Fourth)
    )

GraphNode NewGraphNode(GraphNode First, GraphNode Second, GraphNode Third, GraphNode Fourth,int check)
{
    GraphNode new = NULL;
    GGC_PUSH_5(First,Second,Third,Fourth,new);
    new = GGC_NEW(GraphNode);
    
    GGC_WP(new,First,First);
    GGC_WP(new,Second,Second);
    GGC_WP(new,Third,Third);
    GGC_WP(new,Fourth,Fourth);
    GGC_WD(new,check,check);

    return new;
}

void useMemory(int x) {
    int i = 0;
    while (i < x) {
        i++;
        GraphNode temp = NewGraphNode(NULL,NULL,NULL,NULL,-1);
        GGC_PUSH_1(temp);
    }
}

int main(int argc, char* argv[])
{
    GraphNode A,B,C,D,E,F,G;
    A = B = C = D = E = F = G = NULL;
    GGC_PUSH_7(A,B,C,D,E,F,G);
    A = NewGraphNode(NULL,NULL,NULL,NULL,0);
    B = NewGraphNode(NULL,NULL,NULL,NULL,0);
    C = NewGraphNode(NULL,NULL,NULL,NULL,0);
    D = NewGraphNode(NULL,NULL,NULL,NULL,0);
    E = NewGraphNode(NULL,NULL,NULL,NULL,0);
    F = NewGraphNode(NULL,NULL,NULL,NULL,0);
    G = NewGraphNode(NULL,NULL,NULL,NULL,0);
    // Nothing points back to A except G and A is the only one pointing to G so if we remove A
    // it shouldn't still be in our memory and if we remove G after it also shouldn't be.
    long unsigned int holder = (long unsigned int) &A;
    GGC_WP(A,First,B);
    GGC_WP(A,Second,C);
    GGC_WP(A,Third,D);
    GGC_WP(A,Fourth,G);
    GGC_WP(B,First,B);
    GGC_WP(B,Second,C);
    GGC_WP(B,Third,D);
    GGC_WP(B,Fourth,E);
    GGC_WP(C,First,D);
    GGC_WP(C,Second,E);
    GGC_WP(C,Third,F);
    GGC_WP(C,Fourth,C);
    GGC_WP(D,First,B);
    GGC_WP(D,Second,C);
    GGC_WP(D,Third,F);
    GGC_WP(D,Fourth,E);
    GGC_WP(E,First,E);
    GGC_WP(E,Second,E);
    GGC_WP(E,Third,E);
    GGC_WP(E,Fourth,B);
    GGC_WP(F,First,B);
    GGC_WP(F,Second,F);
    GGC_WP(F,Third,C);
    GGC_WP(F,Fourth,D);
    printf("A check is %d should be 0\r\n", GGC_RD(A,check));
    A = NULL;
    G = NULL;
    useMemory(3000000);
    // Should have collected at least a few times which means memory should have been over written 
    printf("A check is %d should be random data as that space has been allocated, recollected and reallocated likely. If it is zero every run something is wrong.\r\n", GGC_RD(((GraphNode) holder),check));
    
}
