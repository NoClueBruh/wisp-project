#ifndef GC_H
#define GC_H

#define THIS_IS_VM
#include "Alloc.h"
#include "Shared.h"

// gc pointer header
// keep in mind that if has_prev is true, 
// this object allocated has a gcp_header_t pointer as the prev_allocation pointer
typedef struct gcp_header_t {
    uint8_t marked   : 1;
    uint8_t has_prev : 1;
    struct gcp_header_t* next;
} gcp_header_t;

typedef struct {
    gcp_header_t* head;
    gcp_header_t* last;
    
    struct {
        gcp_header_t* first;
        gcp_header_t* last;
    } temp;

    size_t usage;
    size_t capacity;

    void* parent;
    void (*clean_event)(void*);
} gc_t;

void  gc_init(gc_t* gc, void (*cleanup)(void*), void* parent);
void* gc_alloc(gc_t* gc, size_t size);
void  gc_empty_temp(gc_t* gc);

void  gc_mark(void* p);
int   gc_ismarked(void* p);

// marks the object `p` as having a `prev_alloc` pointer.
// this function does NOT automatically set that pointer.
void gc_setprev(void* p);

// returns whether `p` has a `prev_alloc` pointer or not.
int gc_hasprev(void* p);

// `p` must be an object allocated with `gc_alloc` that also has a `prev_alloc` field at the top.
void gc_freeptr(gc_t* gc, void* p);

void  gc_freeall(gc_t* gc);
#endif