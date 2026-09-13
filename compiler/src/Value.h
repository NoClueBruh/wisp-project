#ifndef VALUE_H
#define VALUE_H

#include "Expr.h"
#include "Shared.h"

// typedef, like in C
typedef struct {
    type_t* ref;
    char* name;

    varflags_t flags;
    uint16_t pid;
} typedef_t;

// const is a value container that corresponds to a stack index at runtime.
// it is named "const" due to the fact that it has a constant type.
typedef struct {
    char* name;

    expr_t expr;
    varflags_t flags; 
    uint16_t pid;
    uint16_t id;
} const_t;

// local variable of a function.
typedef struct local_var_t {
    strcut_t name;
    type_t* type;
    struct local_var_t* next;
} local_var_t;

// lambda function
typedef struct func_t {
    expr_t expr;

    struct func_t* parent;
    local_var_t* localvar;
    type_t* type;

    uint16_t id;
    uint8_t vars;
    uint8_t max_vars;
    char valid : 1;
} func_t;

// returns whether two types `t1` and `t2` can be interchanged without casting.
int type_interchangable(type_t* t1, type_t* t2);

// returns whether two types `t1` and `t2` are equivalent (equal or interchangable).
int type_equivalent(type_t* t1, type_t* t2);

// returns whether two types `t1` and `t2` are exactly equal or equivalent (depending on the `exactly` flag).
int type_equals(type_t* t1, type_t* t2, int exactly);

// returns whether the type `t1` is an object type.
int type_isobj(type_t* t1);

// yes
int type_iscomb(type_t* t1, type_t* t2, primitive_t p1, primitive_t p2);

// prints `type` to `fd` 
void type_print(FILE* fd, type_t* type);

#ifndef PARSER_H
    typedef struct parser_t parser_t;
#endif

// finds a const by name respecting it's flags and position relative to `parser`.
const_t* const_find(parser_t* parser, strcut_t name);

// returns the type exactly equal to `type`, used to avoid duplicates in the type table.
type_t* type_getdup(parser_t* parser, type_t* type);

// finds an external by name respecting it's flags and position relative to `parser`.
external_t* extern_find(parser_t* parser, strcut_t name);

// finds a typedef by name respecting it's flags and position relative to `parser`
typedef_t* typedef_find(parser_t* parser, strcut_t name);

// returns the typeid
uint16_t type_getid(type_t* type);

//
int parse_type(parser_t* parser, type_t** dest);

// registers `type` into the type table.
// returns whether such type already exists and writes the address of the registered type into `*dest`
int parser_register_type(parser_t* parser, type_t* type, type_t** dest);
#endif