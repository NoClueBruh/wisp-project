#include "GC.h"

void gc_init(gc_t* gc, void (*cleanup)(void*), void* parent) {
    gc->clean_event = cleanup; 
    gc->parent = parent;
    gc->usage = 0;
    gc->capacity = 100; // 100 - 1024;

    gc->head = NULL;
    gc->last = NULL;
    gc->temp.first = NULL;
    gc->temp.last = NULL;
}

void gc_freeall(gc_t* gc) {
    size_t freed = 0;
    
    gcp_header_t* curr = gc->head;
    while(curr) {
        gcp_header_t* next = curr->next;
        freed++;
        free(curr);
        curr = next;
    }

    if(freed != 0) {
        printf("[ GC ] freed %zu objects\n", freed);
    }
}

void gc_cleanup(gc_t* gc) {
    gc->clean_event(gc->parent);
    
    size_t freed = 0;

    gcp_header_t* last = NULL;
    gcp_header_t* curr = gc->head;
 
    while(curr) { 
        if(curr->marked) {
            curr->marked = 0;
            last = curr;
            curr = curr->next;
        }
        else {
            gcp_header_t* next = curr->next;
            if(last) {
                last->next = next;
            }
            else {
                gc->head = next;
            }

            if(next && next->has_prev) {
                *((gcp_header_t**) (next + 1)) = last;
            }

            gc->usage--;
            
            freed++;
            free(curr);
            curr = next;
        }
    } 

    if(freed != 0) {
        printf("[ GC ] freed %zu objects\n", freed);
    }
}

void* gc_alloc(gc_t* gc, size_t size) {
    gc->usage++;
    if(gc->usage > gc->capacity) {
        gc_cleanup(gc);
    }

    gcp_header_t* p = mem_req(sizeof(gcp_header_t) + size);
    p->marked = 0;
    p->has_prev = 0;
    p->next = NULL;
 
    if(!gc->temp.first) {
        gc->temp.first = p;
        gc->temp.last = p;
    }
    else {
        gc->temp.last->next = p;
        gc->temp.last = p;
    }

    return p + 1;
}

void gc_freeptr(gc_t* gc, void* p) {
    gc->usage--;

    gcp_header_t* prev = *(gcp_header_t**)p;
    gcp_header_t* data = (gcp_header_t*)p - 1;
    gcp_header_t* next = data->next;

    if(gc->head == data) {
        gc->head = next;
    }
    
    if(gc->last == data) {
        gc->last = prev;
    }

    if(prev) {
        prev->next = next;
    }

    if(next && next->has_prev) {
        *(gcp_header_t**)(next + 1) = prev;
    }

    free(data);
}

void gc_setprev(void* p) {
    ((gcp_header_t*)p - 1)->has_prev = 1;
}

int gc_hasprev(void* p) {
    return ((gcp_header_t*)p - 1)->has_prev;
}

void gc_mark(void* p) {
    ((gcp_header_t*)p - 1)->marked = 1;
}

int gc_ismarked(void* p) {
    return ((gcp_header_t*)p - 1)->marked;
}

void gc_empty_temp(gc_t* gc) {
    if(gc->temp.first) {
        if(gc->head && gc->head->has_prev) {
            *(gcp_header_t**)(gc->head + 1) = gc->temp.last;
        }
        gc->temp.last->next = gc->head;
        gc->head = gc->temp.first;

        gc->temp.first = NULL;
        gc->temp.last = NULL;
    }
}