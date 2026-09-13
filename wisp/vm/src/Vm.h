#ifndef VM_H
#define VM_H

#include "GC.h"
#include "Shared.h"
#include <stdalign.h>

#ifdef NOT_PACKED
    #define GCC_PACKED  __attribute__((packed))
#else
    #define GCC_PACKED
#endif

enum {
    // actively executing instructions
    VM_RUNNING,
    // not executing instructions
    VM_FROZEN,
    // errored
    VM_ERROR,
    // already freed memory
    VM_FREED,
}; 

// some data about a function
typedef struct {
    uint32_t ptr;
    uint16_t sid; // signature id, if two functions have the same signature id, they have the same input and output
    uint8_t  vars;
} fndata_t;

typedef struct vm_t {
    struct {
        unsigned char* prog; // program 
        unsigned char* pos;  // current instruction
        size_t size;
    };

    #ifdef THIS_IS_VM
    gc_t gc;
    #endif 

    char status;

    void (*onpanic)(struct vm_t* vm, char* msg);

    void* (*getextern)(char* name, int len);
    void** externals;

    type_t* types;
    uint16_t typecount;

    fndata_t* funcs;
    uint16_t func_count;

    // general use arena to store life-long data such as additional type information
    arena_t data;

    inpvec_t callstack;
    inpvec_t stack;
} vm_t; 

typedef struct {
    void* parent;
    uint16_t fnid;
} GCC_PACKED fnval_t;

typedef struct {
    union {
        int integer;
        void* ptr;
        fnval_t* fnval;
    };

    uint16_t typeid;
} GCC_PACKED any_t;

typedef union stack_value_t {
    int integer;
    int boolean;
    int character;
    void* ptr;

    any_t any;
    fnval_t fnval;
} GCC_PACKED stack_value_t;

typedef struct {
    uint32_t length;
    stack_value_t elements[];
} array_t;

typedef struct {
    uint32_t length;
    char characters[];
} arrstr_t; // not sure why I named it like this but sure

typedef struct list_node_t {
    stack_value_t value;
    struct list_node_t* next;
} list_node_t;

typedef struct {
    stack_value_t value;
    uint16_t typeid;
} GCC_PACKED stack_entry_t;
 
typedef struct fnframe_t {
    gcp_header_t* prevalloc;
    
    struct fnframe_t* parent;
    uint16_t fn;
    uint8_t  vars;
    stack_entry_t local[];
} fnframe_t;

typedef struct activeframe_t {
    fnframe_t* frame;
    uint32_t ra;
} activeframe_t;

// loads bytecode into the vm
void vm_load(vm_t* vm, char* bytecode, size_t size);

// run the instruction at vm->pos
void vm_exec(vm_t* vm);

// begin execution
void vm_run(vm_t* vm);

// free all memory associated with the vm
void vm_kill(vm_t* vm); 

// prints a stack_value to stdout
void vm_printv(vm_t* vm, uint16_t typeid, stack_value_t val);

// throws a simple message
void vm_panic(vm_t* vm, char* msg);

// returns whether the type's value is able to fit into an integer
int type_small(uint16_t typeid);

// returns NULL if such meta is not found
metadata_t vm_getmeta(vm_t* vm, char* name, size_t namelen);
#endif