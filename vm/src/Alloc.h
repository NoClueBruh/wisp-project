#ifndef ALLOC_H
#define ALLOC_H 

#include<stdint.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>

// `malloc` but the process dies if the memory requested isn't available.
void* mem_req(size_t size);

// `realloc` but the process dies if the memory requested isn't available.
void* mem_reloc(void* p, size_t new_size);

typedef struct {
    size_t len; // element count
    size_t cap; // capacity
    size_t els; // element size
    void* elements;
} inpvec_t; 

void    inpvec_new (inpvec_t* vec, size_t element_size, size_t capacity);
void    inpvec_push(inpvec_t* vec, void* element);
void    inpvec_shrink(inpvec_t* vec);
void*   inpvec_app (inpvec_t* vec);
void*   inpvec_at  (inpvec_t* vec, size_t i);
 
typedef struct arena_node_t {
    size_t width;
    size_t ptr;
    struct arena_node_t* next;
} arena_node_t;

typedef struct {
    arena_node_t* list;
    arena_node_t* last;
    size_t length;      // number of times arena_alloc was called (useful for arena-vectors)
    size_t allbytes;    // all bytes currently in use by the arena
} arena_t; 

typedef struct {
    arena_node_t* node;
    size_t pos;
    size_t membsize;
} arena_iterator_t;

void    arena_new   (arena_t* arena, size_t width);
void*   arena_alloc (arena_t* arena, size_t size);
void*   arena_next  (arena_iterator_t* iterator);
void    arena_kill  (arena_t* arena);

// an iterator used to walk arena-vectors (fixed step each iteration)
arena_iterator_t arena_iterator(arena_t* arena, size_t membsize);

typedef struct {
    size_t capacity;
    size_t length;
    char* buffer;
} string_t;

// heapbuf OWNS buffer
typedef struct {
    char* buffer;
    size_t len;
} heapbuf_t;

// heapstr OWNS the null-terminated string which is located in the heap
typedef heapbuf_t heapstr_t;

void string_new(string_t* string, size_t capacity);
void string_push(string_t* string, char c);
void string_pushstr(string_t* string, char* str, size_t len);

// pushes a 32bit integer in its byte format (useful for bytecode)
void string_write32(string_t* string, uint32_t i);

// pushes a 16bit integer in its byte format (useful for bytecode)
void string_write16(string_t* string, uint16_t i);

// writes a 32bit integer in its byte format at <pos> assuming it is a valid index inside the string's buffer
void string_write32_at(string_t* string, size_t pos, uint32_t i);

// writes a 16bit integer in its byte format at <pos> assuming it is a valid index inside the string's buffer
void string_write16_at(string_t* string, size_t pos, uint16_t i);
#endif