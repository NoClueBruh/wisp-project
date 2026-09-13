#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#ifdef THIS_IS_VM
    typedef uint16_t typeref_t;
#else
    typedef struct type_t* typeref_t;
#endif 

// metadata is used as a way to pass additional information from the compiler to the runtime.
// an example is the position of a const in the stack.
typedef struct {
    uint8_t type;
    uint16_t data;
} metadata_t; 

typedef enum {
    META_UNDEF = 0,
    META_CONST,
    META_TYPE
} metatype_t;

///////////////////////

// KEEP IN MIND, ORDER MATTERS IN THIS ENUM
typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_PTR, 
    TYPE_STRING,
    TYPE_CHAR,
    TYPE_ANY,
    TYPE_FUNC,
    
    __TYPE_PRIMS_OVER,
 
    TYPE_TUPLE,
    TYPE_STRUCT,
    TYPE_ENUM,
    TYPE_LIST,
    TYPE_ARRAY, 
} primitive_t;

enum {
    OP_TEST,
    OP_PUSH8,
    OP_PUSH16,
    OP_PUSH32,
    OP_PUSH_BOOL, // kinda dumb
    OP_PUSH_CHAR,
    OP_PUSH_NULL,
    
    OP_POP,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,

    OP_EQL,
    OP_NQL,
    OP_GRT,
    OP_GRE,
    OP_SML,
    OP_SME,

    OP_JT, // jump if true
    OP_JF, // jump if false
    OP_HOP, // and/or operator 
    OP_JMP,

    OP_NEG,
    OP_NOT, 

    OP_TUPLE_CREATE,
    OP_TUPLE_GET,

    OP_ARRAY_CREATE,
    OP_ARRAY_GET,
    OP_ARRAY_SLICE,
    OP_ARRAY_CONCAT,

    OP_STRING_CREATE,
    OP_STRING_GET,
    OP_STRING_SLICE,
    OP_STRING_CONCAT,

    OP_LIST_CREATE,
    OP_LIST_TAIL,
    OP_LIST_GET,
    OP_LIST_INSERT,

    OP_VALUE_PROPERTY,
    OP_CAST,
    OP_TYPEOF,

    OP_NOT_NULL,

    OP_FNGET,
    OP_FNLOCAL,
    OP_FNCALL,
    OP_FN_TAILCALL,
    OP_FN_ADDLOCAL,
    OP_FN_REMLOCAL,
    OP_RET,
    
    OP_VSET,
    OP_VGET,
    OP_CALL_EXTERN,

    OP_HALT,
}; 

typedef enum {
    VP_ARRAY_LEN,
    VP_STRING_LEN,
} valproperty_t;

typedef enum {
    CAST_CHAR_STRING = 1,
    CAST_TO_ANY,
    CAST_FROM_ANY,
    CAST_ANY_FUNC,
} castop_t;

#ifndef THIS_IS_VM
typedef struct {
    char* name;
    int value;
} enum_entry_t;
#endif

typedef struct type_t { 
    union {
        struct {
            typeref_t* memb;
            uint8_t mcount; // 256 should be enough bro, if you need more split into pieces jesus
        } tuple;
        
        #ifndef THIS_IS_VM
        struct {
            typeref_t tuptype;
            char** names;
        } structt;

        struct {
            enum_entry_t* entries;
            uint32_t entry_count;
        } enumm;
 
        struct {
            typeref_t* types; // types[argcount] is the return type, the rest are arguments
            uint8_t argcount; 
            uint8_t hasvargs; // if true, types[argcount - 1] determines their type
        } func;
        #endif

        struct {
            typeref_t memb;
        } list;

        struct {
            typeref_t memb;
        } array;
    };

    primitive_t prim : 8;

    #ifndef THIS_IS_VM
    uint16_t id; // for function types, this becomes the signature id instead of the typeid (typeid = TYPE_FUNC for functions)
    #endif
} type_t;  

#endif 