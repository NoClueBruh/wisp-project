#include"alloc.h"

void* mem_req(size_t size) {
    void* p = malloc(size);
    if(p == NULL) {
        fprintf(stderr, "MALLOC FAILED\n");
        exit(1);
    }
    return p;
}

void* mem_reloc(void* p, size_t new_size) {
    p = realloc(p, new_size);
    if(p == NULL) {
        fprintf(stderr, "REALLOC FAILED\n");
        exit(1);
    }
    return p;
}

// STRING

void string_new(string_t* string, size_t capacity) {
    string->buffer = mem_req(capacity + 1);
    string->capacity = capacity;
    string->length = 0;
    string->buffer[0] = 0;
}

void string_push(string_t* string, char c) {
    if(string->length + 2 > string->capacity) {
        string->capacity *= 2;
        string->buffer = mem_reloc(string->buffer, string->capacity + 1);
    }

    string->buffer[string->length++] = c;
    string->buffer[string->length] = 0;
}

void string_pushstr(string_t* string, char* str, size_t len) {
    int resized = 0;
    while(string->length + 1 + len > string->capacity) {
        string->capacity *= 2;
        resized = 1;
    }

    if(resized) {
        string->buffer = mem_reloc(string->buffer, string->capacity + 1);
    }

    for(size_t i = 0; i < len; i++) {
        string->buffer[string->length + i] = str[i];
    }

    string->length += len;
    string->buffer[string->length] = 0;
}

void string_write32_at(string_t* string, size_t pos, uint32_t i) {
    string->buffer[pos + 0] = i >> 24;
    string->buffer[pos + 1] = 0xFF & (i >> 16);
    string->buffer[pos + 2] = 0xFF & (i >> 8);
    string->buffer[pos + 3] = 0xFF & i;
}

void string_write32(string_t* string, uint32_t i) {
    if(string->length + 5 > string->capacity) {
        string->capacity *= 2;
        string->buffer = mem_reloc(string->buffer, string->capacity + 1);
    }
 
    string_write32_at(string, string->length, i);
    string->length += 4;

    string->buffer[string->length] = 0;
}

void string_write16_at(string_t* string, size_t pos, uint16_t i) {
    string->buffer[pos + 0] = i >> 8;
    string->buffer[pos + 1] = 0xFF & i;
}

void string_write16(string_t* string, uint16_t i) {
    if(string->length + 3 > string->capacity) {
        string->capacity *= 2;
        string->buffer = mem_reloc(string->buffer, string->capacity + 1);
    }
    
    string->buffer[string->length++] = i >> 8;
    string->buffer[string->length++] = 0xFF & i;
    string->buffer[string->length] = 0;
}

// INPLACE-VECTOR

void inpvec_new(inpvec_t* vec, size_t element_size, size_t capacity) {
    vec->cap = capacity;
    vec->els = element_size;
    vec->len = 0;

    vec->elements = mem_req(element_size * capacity);
}

void* inpvec_app(inpvec_t* vec) {
    if(vec->len >= vec->cap) {
        vec->cap *= 2;
        vec->elements = mem_reloc(vec->elements, vec->els * vec->cap);
    }

    return (char*) vec->elements + vec->els * vec->len++;
}

void inpvec_push(inpvec_t* vec, void* element) {
    memcpy(inpvec_app(vec), element, vec->els);
}

void inpvec_shrink(inpvec_t* vec) {
    if(vec->cap > 4 * vec->len) {
        vec->cap /= 2;
        vec->elements = mem_reloc(vec->elements, vec->els * vec->cap);
    }
}

void* inpvec_at(inpvec_t* vec, size_t i) {
    return (char*) vec->elements + vec->els * i;
}

//////

arena_iterator_t arena_iterator(arena_t* arena, size_t membsize) {
    return (arena_iterator_t) {
        .membsize = membsize,
        .node = arena->list,
        .pos = 0,
    };
}

void* arena_next(arena_iterator_t* iterator) {
    if(iterator->node == NULL)
        return NULL;

    if(iterator->pos + iterator->membsize > iterator->node->ptr) {
        iterator->node = iterator->node->next;
        iterator->pos = 0;
        
        if(iterator->node == NULL)
            return NULL;
    }

    void* p = (char*)(iterator->node + 1) + iterator->pos;
    iterator->pos += iterator->membsize;
    return p;
}

//////

static arena_node_t* arena_node_new(size_t width) {
    arena_node_t* node = mem_req(sizeof(arena_t) + width);
    node->next  = NULL;
    node->ptr   = 0;
    node->width = width; 
    return node;
}

void arena_new(arena_t* arena, size_t width) {
    arena_node_t* node = arena_node_new(width);
    arena->list = node;
    arena->last = node;
    arena->length = 0;
    arena->allbytes = 0;
}

void* arena_alloc(arena_t* arena, size_t size) {
    arena_node_t* node = arena->last;

    size_t newsize = node->width;
    while(node->ptr + size > newsize) 
        newsize = newsize + newsize / 2;

    if(newsize != node->width) {
        node->next = arena_node_new(newsize);
        node = node->next;
        arena->last = node;
    } 

    // useful only for arena-vectors
    arena->length++;

    void* addr = (char*)(node + 1) + node->ptr;
    node->ptr += size;
    
    arena->allbytes += size;
    return addr;
}  

void arena_kill(arena_t* arena) {
    arena_node_t* node = arena->list; 
    while(node != NULL) {
        arena_node_t* next = node->next;
        free(node);
        node = next;
    }
}