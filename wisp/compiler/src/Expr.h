#ifndef EXPR_H
#define EXPR_H

#include "Parser.h" 

typedef struct type_t type_t; 
typedef struct func_t func_t;

enum {
    EXPR_NODE_INTEGER,
    EXPR_NODE_BOOLEAN,
    EXPR_NODE_NULL,
    EXPR_NODE_STRING,
    EXPR_NODE_CHARACTER,

    EXPR_NODE_CONSTANT,
    EXPR_NODE_EXTERNCALL,

    EXPR_NODE_BRANCH,

    EXPR_NODE_CALL,

    EXPR_NODE_TUPLE_ACCESS,
    EXPR_NODE_STRUCT_ACCESS,

    EXPR_NODE_ARRAY_ACCESS,
    EXPR_NODE_STRING_ACCESS,

    EXPR_NODE_ARRAY_SLICE,
    EXPR_NODE_STRING_SLICE,

    EXPR_NODE_VALUE_PROPERTY,

    EXPR_NODE_OPERATOR,
    EXPR_NODE_SUB_EXPR,

    EXPR_NODE_LETDEF,
    EXPR_NODE_CAST,
    EXPR_NODE_TYPEOF,

    EXPR_NODE_FUNCTION,
    EXPR_NODE_FNLOCAL,

    EXPR_NODE_BODY,

    EXPR_NODE_TUPLE,
    EXPR_NODE_LIST,
    EXPR_NODE_ARRAY,

    EXPR_NODE_VARG
};

typedef enum {
    OPERATOR_ADD,
    OPERATOR_MUL,
    OPERATOR_SUB,
    OPERATOR_DIV,

    OPERATOR_GREATER,
    OPERATOR_GREATER_EQUAL,
    OPERATOR_LESS,
    OPERATOR_LESS_EQUAL,
    OPERATOR_EQUAL,
    OPERATOR_NOT_EQUAL,

    OPERATOR_AND,
    OPERATOR_OR,

    OPERATOR_CALL,

    OPERATOR_LIST_INSERT,
    OPERATOR_ARRAY_CONCAT,
    OPERATOR_STRING_CONCAT,

    OPERATOR_COMMA,

    __OPERATOR_UNARY,

        OPREATOR_TAIL,
        OPERATOR_FIRST,

        OPREATOR_NOT_NULL,
        OPERATOR_NEG,
        OPERATOR_NOT,

    __OPREATOR_END,
} optype_t;

typedef struct {
    char symbol[3];
    uint8_t precedence;
    char opcode;
    char leftac;
} operator_t;

//////////////////////////////  

typedef struct expr_node_t {    
    uint16_t node_type;
    unsigned int line;

    struct expr_node_t* next;
    union {
        struct expr_node_t* right;
        struct expr_node_t* child;
    };
    struct expr_node_t* left;
} expr_node_t;

//////////////////////////////

typedef struct {
    type_t* type;
    expr_node_t* root;
} expr_t; 

int expr_parse(parser_t* parser, expr_t* expr);  
void expr_emit_node(expr_node_t* node, int final, string_t* bc, func_t* func);
#endif