#include "Expr.h"
#include "Value.h" 
#include "Shared.h"

typedef enum {
    NODE_BINARY     = 0, // all pointers
    NODE_UNARY      = 1, // only right/child pointer
    NODE_OPERAND    = 2, // no pointers
} node_size_t;

typedef struct alias_t {
    strcut_t name;
    expr_node_t* ref;
    struct alias_t* next;
} alias_t; 

typedef struct {
    expr_node_t* output;
    expr_node_t* operators;

    alias_t* aliases;

    struct {
        char has_elements : 1; 
        char last_was_op : 1;
        char this_was_op : 1;
        char allow_empty : 1;
    };
 
    func_t* parent;

    parser_t* parser;
    token_t token;
} expr_builder_t;

typedef struct {
    expr_node_t* node;
} element_iterator_t;

////////////////

// each expression node has some data above and below it.
// the data above are type-specific. for example, a EXPR_NODE_INTEGER will have a data32 above it to contain it's value.
// the data below are pointers for the stack, for the left/right subtrees or any children nodes.

typedef union {
    uint32_t integer;
    uint32_t character;
} data32_t;

typedef union {
    uint16_t boolean;
    uint16_t varidx;
    uint16_t tupidx;
    uint16_t operator;
    char character;
} data16_t;

typedef union {
    struct {
        char*    property_name;
        uint8_t  property_index;
    };

    struct {
        type_t*  property_type;
        uint8_t  property_vp;
    };

    struct {
        type_t* cast_type;
        uint8_t cast_instruction;
    };
} datalong_t; 
// long as in "multiple fields larger than 32bits combined"

typedef union {
    type_t* fnc_type;
    type_t* tuple_type;
    type_t* typeof_type;

    expr_node_t* extra_child;
    
    void* constant;
    void* external;
    void* function;
    char* string;
} dataptr_t;

typedef struct {
    void* srcfn;
    uint8_t varidx;
    type_t* vartype;
} datafnvar_t;

typedef struct {
    type_t* type;
    uint32_t membc;
} datalist_t;

typedef struct {
    struct expr_node_t* true_case;
    struct expr_node_t* false_case;
    struct expr_node_t* condition;
} datacond_t;
// used by if-else statements

#define DEFINE_DATA(type, txt) static type* DATA_##txt(expr_node_t* node) { return (type*)node - 1;  }

DEFINE_DATA(data32_t, 32)
DEFINE_DATA(data16_t, 16)
DEFINE_DATA(dataptr_t, PTR)
DEFINE_DATA(datalist_t, LIST)
DEFINE_DATA(datafnvar_t, FN)
DEFINE_DATA(datacond_t, COND)
DEFINE_DATA(datalong_t, LONG)

/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////

// the higher the precedence, the more "binding power" it has
static const operator_t global_operators[] = {
    [ OPERATOR_ADD ] = { "+", 20, OP_ADD, 0 },
    [ OPERATOR_SUB ] = { "-", 20, OP_SUB, 0 },
    [ OPERATOR_MUL ] = { "*", 30, OP_MUL, 0 },
    [ OPERATOR_DIV ] = { "/", 30, OP_DIV, 0 },
    
    [ OPERATOR_GREATER ]        = { ">", 10, OP_GRT, 0 },
    [ OPERATOR_LESS ]           = { "<", 10, OP_SML, 0 },
    [ OPERATOR_GREATER_EQUAL]   = { ">=", 10, OP_GRE, 0 },
    [ OPERATOR_LESS_EQUAL ]     = { "<=", 10, OP_SME, 0 },
    [ OPERATOR_EQUAL]           = { "==", 10, OP_EQL, 0 },
    [ OPERATOR_NOT_EQUAL ]      = { "!=", 10, OP_NQL, 0 },

    [ OPERATOR_AND ]            = { "&&", 5, 0, 0},
    [ OPERATOR_OR  ]            = { "||", 5, 0, 0},

    [ OPERATOR_LIST_INSERT ]    = { "->", 15, OP_LIST_INSERT, 0 },
    [ OPERATOR_ARRAY_CONCAT ]   = { "++", 15, OP_ARRAY_CONCAT, 0 },
    [ OPERATOR_STRING_CONCAT ]  = { "..", 15, OP_STRING_CONCAT, 0 },

    [ OPERATOR_COMMA ] = { ",", 0, 0, 0 },

    // UNARY 
    [ OPERATOR_FIRST ]      = { "$", 0xFF, OP_LIST_GET, 0 },
    [ OPREATOR_TAIL ]       = { "~", 0xFF, OP_LIST_TAIL, 0 },
    [ OPERATOR_NEG ]        = { "-", 0xFF, OP_NEG, 0 },
    [ OPERATOR_NOT ]        = { "!", 0xFF, OP_NOT, 0 },
    [ OPREATOR_NOT_NULL ]   = { "?", 0xFF, OP_NOT_NULL, 0 },
};

static optype_t optype(const operator_t* op) {
    return op - global_operators;
}

static expr_node_t* getnode(parser_t* parser, size_t predata, node_size_t size) {
    char* node = parser_alloc(parser, predata + sizeof(expr_node_t) - size * sizeof(void*));
    return (expr_node_t*) (node + predata);
}

static const operator_t* getop(char symbol[2]) {
    for(size_t i = 0; i < sizeof(global_operators) / sizeof(operator_t); i++) {
        const operator_t* op = global_operators + i;

        if(op->symbol[0] == symbol[0] && op->symbol[1] == symbol[1])
            return op;
    }

    return NULL;
}

/////////////////////////////////

static type_t* typecheck(parser_t* parser, expr_node_t* node);
static int parse(expr_builder_t* builder);

static int pop_operator(expr_builder_t* builder) {
    expr_node_t* top = builder->operators; 
    if(!top) {
        return 1;
    }
     
    builder->operators = top->next;

    optype_t type = DATA_16(top)->operator;
    if(type > __OPERATOR_UNARY) {
        expr_node_t* right;

        if((right = builder->output) == NULL) {
            fprintf(stderr, "%s:%u Expected more operands for unary operator %s\n", builder->parser->filepath.buffer, builder->parser->scan.line, global_operators[type].symbol);
            return 1;
        }

        top->right = right;
        top->next  = right->next;
    } 
    else {
        expr_node_t* left;
        expr_node_t* right;

        if((right = builder->output) == NULL || (left = right->next) == NULL) {
            fprintf(stderr, "%s:%u Expected more operands for binary operator %s\n", builder->parser->filepath.buffer, builder->parser->scan.line, global_operators[type].symbol);
            return 1;
        }

        top->left  = left;
        top->right = right;
        top->next  = left->next;
    }  
    builder->output = top;

    return 0;
}

static expr_builder_t childbuilder(expr_builder_t* p) {
    return (expr_builder_t) {
        .parser = p->parser,
        .parent = p->parent,
        .output = NULL,
        .operators = NULL,
        .aliases = p->aliases,
        .last_was_op = 1,
        .this_was_op = 0,
        .has_elements = 0,
        .allow_empty = 0
    };
}

// helper function to easily walk every element in a comma tree.
static expr_node_t* nextelement(element_iterator_t* iterator) {
    if(!iterator->node || iterator->node->node_type != EXPR_NODE_OPERATOR || DATA_16(iterator->node)->operator != OPERATOR_COMMA) {
        expr_node_t* c = iterator->node;
        iterator->node = NULL;
        return c;
    }

    expr_node_t* c = iterator->node->left;
    iterator->node = iterator->node->right;
    return c;
}

// assuming `root != NULL`
static size_t count_elements(expr_node_t* node) {
    size_t n = 0;
    element_iterator_t it = {.node = node};
    for(expr_node_t* element; element = nextelement(&it); n++);
    return n;
}

static int add_post_operand(expr_builder_t* builder, expr_node_t** dest) {
    expr_node_t* node = NULL;

    switch(builder->token.type) {
    case TOKEN_SYMBOL:
        if(builder->token.symbol[0] == '(') { 
            expr_builder_t builder2 = childbuilder(builder);
            builder2.has_elements = 1; // might be a multi-arg function
            builder2.allow_empty  = 1;

            if(parse(&builder2)) {
                return 1;
            }

            if(builder2.token.type != TOKEN_SYMBOL || builder2.token.symbol[0] != ')') {
                fprintf(stderr, "%s:%u Expected )\n", builder->parser->filepath.buffer, builder->parser->scan.line);
                return 1;
            }

            node = getnode(builder->parser, 0, NODE_BINARY);
            node->node_type = EXPR_NODE_CALL;
            node->left = builder2.output;
        }
        else if(builder->token.symbol[0] == '.') {
            scanner_next(&builder->parser->scan, &builder->token);
            
            if(builder->token.type == TOKEN_INTEGER) {
                node = getnode(builder->parser, sizeof(data32_t), NODE_UNARY);
                DATA_16(node)->tupidx = builder->token.integer;
                node->node_type = EXPR_NODE_TUPLE_ACCESS;
            }
            else if(builder->token.type == TOKEN_LITERAL) {
                node = getnode(builder->parser, sizeof(datalong_t), NODE_UNARY);
                DATA_LONG(node)->property_name = parser_copycut(builder->parser, builder->token.literal);
                node->node_type = EXPR_NODE_STRUCT_ACCESS;
            }
        }
        else if(builder->token.symbol[0] == '[') {
            expr_builder_t builder2 = childbuilder(builder);
            if(parse(&builder2)) {
                return 1;
            }

            if(builder2.token.type == TOKEN_LITERAL && strcut_equals(builder2.token.literal, "to")) {
                expr_builder_t builder3 = childbuilder(builder);
                if(parse(&builder3)) {
                    return 1;
                }
                
                if(builder3.token.type != TOKEN_SYMBOL || builder3.token.symbol[0] != ']') {
                    fprintf(stderr, "%s:%u Expected ]\n", builder->parser->filepath.buffer, builder->parser->scan.line);
                    return 1;
                }

                node = getnode(builder->parser, sizeof(dataptr_t), NODE_BINARY);
                node->node_type = EXPR_NODE_ARRAY_SLICE;
                node->left = builder2.output;
                DATA_PTR(node)->extra_child = builder3.output;
            }
            else { 
                if(builder2.token.type != TOKEN_SYMBOL || builder2.token.symbol[0] != ']') {
                    fprintf(stderr, "%s:%u Expected ]\n", builder->parser->filepath.buffer, builder->parser->scan.line);
                    return 1;
                }

                node = getnode(builder->parser, 0, NODE_BINARY);
                node->node_type = EXPR_NODE_ARRAY_ACCESS;
                node->left = builder2.output;
            }
        }
        break;
    case TOKEN_LITERAL:
        if(strcut_equals(builder->token.literal, "as")) {
            type_t* cast;

            if(parse_type(builder->parser, &cast)) {
                return 1;
            }

            node = getnode(builder->parser, sizeof(datalong_t), NODE_UNARY);
            DATA_LONG(node)->cast_type = cast;
            DATA_LONG(node)->cast_instruction = 0;
            node->node_type = EXPR_NODE_CAST;
        }
        else if(strcut_equals(builder->token.literal, "typeof")) {
            type_t* cast;

            if(parse_type(builder->parser, &cast)) {
                return 1;
            }

            node = getnode(builder->parser, sizeof(dataptr_t), NODE_UNARY);
            DATA_PTR(node)->typeof_type = cast;
            node->node_type = EXPR_NODE_TYPEOF;
        }
        break;
    }

    *dest = node;
    return 0;
}

static int add_operator(expr_builder_t* builder) {
    builder->this_was_op = 1;

    const operator_t* op;
    if(builder->token.type != TOKEN_SYMBOL || (op = getop(builder->token.symbol)) == NULL) {
        unsigned int line = builder->parser->scan.line;

        if(builder->output) {
            expr_node_t* post = NULL;
            if(add_post_operand(builder, &post)) {
                return 1;
            }

            if(post) {
                builder->this_was_op = 0;
                post->line = line;

                post->child = builder->output;
                post->next  = builder->output->next;
                builder->output = post;
                return 0;   
            }
        }

        fprintf(stderr, "%s:%u Unexpected ", builder->parser->filepath.buffer, builder->parser->scan.line);
        token_print(stderr, &builder->token);
        fputc('\n', stderr);
        return 1;
    }

    while(builder->operators) {
        int precedence = global_operators[DATA_16(builder->operators)->operator].precedence;

        if(op->precedence > precedence || (!op->leftac && op->precedence == precedence))
            break;
        
        if(pop_operator(builder)) {
            return 1;
        }
    }

    expr_node_t* node = getnode(builder->parser, sizeof(data32_t), NODE_BINARY);
    node->node_type = EXPR_NODE_OPERATOR;
    node->line      = builder->parser->scan.line;
    DATA_16(node)->operator  = optype(op);

    node->next = builder->operators;
    builder->operators = node;

    return 0;
}

static void add_unary(expr_builder_t* builder, const operator_t* op) {
    expr_node_t* node = getnode(builder->parser, sizeof(data32_t), NODE_UNARY);
    node->node_type = EXPR_NODE_OPERATOR;
    node->line      = builder->parser->scan.line;
    DATA_16(node)->operator  = optype(op);

    node->next = builder->operators;
    builder->operators = node;

    builder->this_was_op = 1;
}

static int add_parentheses(expr_builder_t* builder, expr_node_t** dest) {
    expr_builder_t builder2 = childbuilder(builder);
    builder2.has_elements = 1;

    if(parse(&builder2)) {
        return 1;
    }

    if(builder2.token.type != TOKEN_SYMBOL || builder2.token.symbol[0] != ')') {
        fprintf(stderr, "%s:%u Expected )\n", builder->parser->filepath.buffer, builder->parser->scan.line);
        return 1;
    }

    if(builder2.output->node_type == EXPR_NODE_OPERATOR && DATA_16(builder2.output)->operator == OPERATOR_COMMA) {
        // tuple mode!
        size_t membc = count_elements(builder2.output);

        if(membc > 0xFF){
            fprintf(stderr, "%s:%u Too many tuple members\n", builder->parser->filepath.buffer, builder->parser->scan.line);
            return 1;
        }

        type_t** types = mem_req(sizeof(type_t*) * membc); 
        type_t** p = types;

        element_iterator_t it = {.node = builder2.output};
        for(expr_node_t* element; element = nextelement(&it); p++) {
            type_t* type = typecheck(builder->parser, element);
            if(!type) {
                free(types);
                return 1;
            }

            *p = type;
        }

        expr_node_t* node = getnode(builder->parser, sizeof(dataptr_t), NODE_UNARY);
        node->node_type = EXPR_NODE_TUPLE;
        node->right = builder2.output;

        type_t tmp = {
            .tuple = {
                .mcount = membc,
                .memb   = types,
            },
            .prim = TYPE_TUPLE,
        };

        type_t* uh;
        if(parser_register_type(builder->parser, &tmp, &uh)) {
            type_t** locked = parser_alloc(builder->parser, sizeof(type_t*) * membc);
            memcpy(locked, types, sizeof(type_t*) * membc);
            uh->tuple.memb = locked;
        }

        DATA_PTR(node)->tuple_type = uh;
        free(types);

        *dest = node;
        return 0;
    } 

    *dest = builder2.output;
    return 0;
}

static int add_list(expr_builder_t* builder, expr_node_t** dest) {
    expr_builder_t builder2 = childbuilder(builder);
    builder2.has_elements = 1;

    if(parse(&builder2)) {
        return 1;
    }
    
    if(builder2.token.type != TOKEN_SYMBOL || builder2.token.symbol[0] != '}') {
        fprintf(stderr, "%s:%u Expected }\n", builder->parser->filepath.buffer, builder->parser->scan.line);
        return 1;
    }
    
    expr_node_t* node = getnode(builder->parser, sizeof(datalist_t), NODE_UNARY);
    node->node_type = EXPR_NODE_LIST;
    node->right = builder2.output;

    // count elements
    size_t membc = count_elements(builder2.output);
    type_t* membtype = NULL;

    element_iterator_t it = {.node = builder2.output};
    for(expr_node_t* element; element = nextelement(&it);) {
        type_t* element_type = typecheck(builder->parser, element);
        if(!element_type) {
            return 1;
        }

        if(!membtype) {
            membtype = element_type;
        }
        else if(!type_equivalent(membtype, element_type)){
            fprintf(stderr, "%s:%u All list elements must be of the same type\n", builder->parser->filepath.buffer, element->line);
            return 1;
        }
    }

    DATA_LIST(node)->membc = membc;

    type_t tmp = {
        .list.memb = membtype,
        .prim = TYPE_LIST,
    };
    parser_register_type(builder->parser, &tmp, &DATA_LIST(node)->type);

    *dest = node;
    return 0;
}

static int add_array(expr_builder_t* builder, expr_node_t** dest) {
    expr_builder_t builder2 = childbuilder(builder);
    builder2.has_elements = 1;

    if(parse(&builder2)) {
        return 1;
    }
    
    if(builder2.token.type != TOKEN_SYMBOL || builder2.token.symbol[0] != ']') {
        fprintf(stderr, "%s:%u Expected ]\n", builder->parser->filepath.buffer, builder->parser->scan.line);
        return 1;
    }
    
    expr_node_t* node = getnode(builder->parser, sizeof(datalist_t), NODE_UNARY);
    node->node_type = EXPR_NODE_ARRAY;
    node->right = builder2.output;

    // count elements
    size_t membc = count_elements(builder2.output);
    type_t* membtype = NULL;

    element_iterator_t it = {.node = builder2.output};
    for(expr_node_t* element; element = nextelement(&it);) {
        type_t* element_type = typecheck(builder->parser, element);
        if(!element_type) {
            return 1;
        }

        if(!membtype) {
            membtype = element_type;
        }
        else if(!type_equivalent(membtype, element_type)){
            fprintf(stderr, "%s:%u All array elements must be of the same type\n", builder->parser->filepath.buffer, element->line);
            return 1;
        }
    }

    DATA_LIST(node)->membc = membc;

    type_t tmp = {
        .array.memb = membtype,
        .prim = TYPE_ARRAY,
    };
    parser_register_type(builder->parser, &tmp, &DATA_LIST(node)->type);

    *dest = node;
    return 0;
}

static int add_branch(expr_builder_t* builder, expr_node_t** dest) {
    expr_node_t* node = getnode(builder->parser, sizeof(datacond_t), NODE_BINARY);
    node->node_type = EXPR_NODE_BRANCH;

    // condition
    {
        expr_builder_t builder2 = childbuilder(builder);
        if(parse(&builder2)) {
            return 1;
        }

        if(builder2.token.type != TOKEN_LITERAL || !strcut_equals(builder2.token.literal, "then")) {
            fprintf(stderr, "%s:%u Expected keyword 'then' after condition\n", builder->parser->filepath.buffer, builder->parser->scan.line);
            return 1;
        }

        DATA_COND(node)->condition = builder2.output;
    }

    // true case
    {
        expr_builder_t builder2 = childbuilder(builder);
        if(parse(&builder2)) {
            return 1;
        }

        if(builder2.token.type != TOKEN_LITERAL || !strcut_equals(builder2.token.literal, "else")) {
            fprintf(stderr, "%s:%u Expected keyword 'else' after true case\n", builder->parser->filepath.buffer, builder->parser->scan.line);
            return 1;
        }

        DATA_COND(node)->true_case = builder2.output;
    }

    // false case
    {
        expr_builder_t builder2 = childbuilder(builder);
        if(parse(&builder2)) {
            return 1;
        }

        DATA_COND(node)->false_case = builder2.output;

        scanner_rewind(&builder->parser->scan);
    }

    *dest = node;
    return 0;
}

static uint8_t push_localvar(func_t* func, local_var_t* var) {
    func->vars++;
    if(func->vars > func->max_vars) {
        func->max_vars = func->vars;
    }

    size_t varidx = 0;
    if(func->localvar) {
        local_var_t* last;
        for(last = func->localvar; last->next; last = last->next) {
            varidx++;
        }

        last->next = var;
    }
    else {
        func->localvar = var;
    }

    return varidx;
}

static local_var_t* pop_localvar(func_t* func) {
    func->vars--;

    local_var_t* last = func->localvar;
    local_var_t* second_last = NULL;

    for(; last && last->next; second_last = last, last = last->next);

    if(second_last) {
        second_last->next = NULL;
    }
    else {
        func->localvar = NULL;
    }
    return last;
}

static int add_function(expr_builder_t* builder, expr_node_t** dest) {
    scanner_next(&builder->parser->scan, &builder->token);

    if(builder->token.type != TOKEN_SYMBOL) {
        fprintf(stderr, "%s:%u Expected '.' or ':' after 'L' for lambda declaration\n", builder->parser->filepath.buffer, builder->parser->scan.line);
        return 1;
    }

    local_var_t* args = NULL;
    size_t argc = 0;

    if(builder->token.symbol[0] == '.') {
        local_var_t* last = NULL; 
        for(;;) {
            argc++;
 
            local_var_t* var = parser_alloc(builder->parser, sizeof(local_var_t));
            var->next = NULL; 

            if(last) {
                last->next = var;
            } 
            else {
                args = var;
            }
            last = var;

            if(parse_type(builder->parser, &var->type)) {
                return 1;
            }

            scanner_next(&builder->parser->scan, &builder->token);
            if(builder->token.type != TOKEN_LITERAL) {
                fprintf(stderr, "%s:%u Expected argument name\n", builder->parser->filepath.buffer, builder->parser->scan.line);
                return 1;
            }

            var->name = builder->token.literal;
            // if you are wondering, the only reason we aren't doing parser_copycut is because we dont need to extend the lifetime.
    

            scanner_next(&builder->parser->scan, &builder->token);
            if(builder->token.type == TOKEN_SYMBOL) {
                if(builder->token.symbol[0] == ',') {
                    continue;
                }
                if(builder->token.symbol[0] == ':') {
                    break;
                }
            }

            fprintf(stderr, "%s:%u Expected ':' or ',' after argument\n", builder->parser->filepath.buffer, builder->parser->scan.line);
            return 1;
        }

        if(argc > 0xFF) {
            fprintf(stderr, "%s:%u Too many local variables (max: 256).\n", builder->parser->filepath.buffer, builder->parser->scan.line);  
            return 1;
        }  
    }
    else if(builder->token.symbol[0] != ':') {
        fprintf(stderr, "%s:%u Expected '.' or ':' after 'L' for lambda declaration\n", builder->parser->filepath.buffer, builder->parser->scan.line);
        return 1;
    }

    func_t* func = parser_addfunct(builder->parser);
    func->id = builder->parser->root->functs.length - 1;
    func->parent = builder->parent;
    func->localvar = args;
    func->max_vars = argc;
    func->vars = argc;
    func->valid = 1;

    expr_builder_t builder2 = childbuilder(builder);
    builder2.parent = func;
 
    if(parse(&builder2)) {
        func->valid = 0;
        return 1;
    }

    scanner_rewind(&builder->parser->scan); 

    func->expr.root = builder2.output;
    func->expr.type = typecheck(builder->parser, builder2.output); 

    if(!func->expr.type) {
        func->valid = 0;
        return 1;
    }

    type_t** hm = mem_req(sizeof(type_t*) * (1 + func->vars));
    size_t idx = 0;
    
    local_var_t* v = func->localvar;
    for(; v; v = v->next) {
        hm[idx++] = v->type; 
    }

    hm[func->vars] = func->expr.type;

    type_t tmp = {
        .prim = TYPE_FUNC,
        .func = {
            .argcount = func->vars,
            .hasvargs = 0,
            .types = hm
        },
    }; 
  
    if(parser_register_type(builder->parser, &tmp, &func->type)) {
        type_t** lock = parser_alloc(builder->parser, sizeof(type_t*) * (1 + func->vars));
        memcpy(lock, hm, sizeof(type_t*) * (1 + func->vars));
        
        // kinda goofy lol
        func->type->func.types = lock;
    } 
    free(hm); 

    expr_node_t* node = getnode(builder->parser, sizeof(dataptr_t), NODE_OPERAND);
    node->node_type = EXPR_NODE_FUNCTION;
    DATA_PTR(node)->function = func;

    *dest = node;
    return 0;
}

static int add_let(expr_builder_t* builder, expr_node_t** dest) {
    scanner_next(&builder->parser->scan, &builder->token);
    
    uint8_t varidx = 0;
    local_var_t* newvar = NULL; 

    strcut_t name;
    if(builder->token.type != TOKEN_LITERAL) {
        fprintf(stderr, "%s:%u Expected local variable name\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    name = builder->token.literal;
    if(!builder->parent) {
        fprintf(stderr, "%s:%u Tried to define hanging local variable %.*s\n", builder->parser->filepath.buffer, builder->parser->scan.line, name.len, name.str);  
        return 1;
    }

    func_t* parent = builder->parent;
    int toomany = builder->parent->vars == 0xFF;
 
    scanner_next(&builder->parser->scan, &builder->token);

    if(builder->token.type == TOKEN_SYMBOL && builder->token.symbol[0] == ':') {
        newvar = mem_req(sizeof(local_var_t));
        newvar->name = name;
        newvar->next = NULL;
        parse_type(builder->parser, &newvar->type);

        if(!newvar->type) {
            return 1;
        }
 
        varidx = push_localvar(parent, newvar);
        scanner_next(&builder->parser->scan, &builder->token);
    }
    
    if(builder->token.type != TOKEN_SYMBOL || builder->token.symbol[0] != '=' || builder->token.symbol[1] != 0) {
        fprintf(stderr, "%s:%u Expected = after local variable name\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    expr_builder_t builder2 = childbuilder(builder);
    if(parse(&builder2)) {
        return 1;
    }

    if(builder2.token.type != TOKEN_LITERAL || !strcut_equals(builder2.token.literal, "in")) {
        fprintf(stderr, "%s:%u Expected \"in\" after local variable\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    expr_node_t* node = getnode(builder->parser, sizeof(data32_t), NODE_BINARY);
    node->node_type = EXPR_NODE_LETDEF;

    type_t* tc = typecheck(builder2.parser, builder2.output);
    if(!tc) {
        return 1;
    }

    if(!newvar) {
        newvar = mem_req(sizeof(local_var_t));
        newvar->name = name;
        newvar->type = tc;
        newvar->next = NULL;

        if(!newvar->type) {
            return 1;
        }

        // adding localvar 
        varidx = push_localvar(parent, newvar);
    }

    node->left = builder2.output;  
    DATA_16(node)->varidx = varidx;

    expr_builder_t builder3 = childbuilder(builder);
    if(parse(&builder3)) {
        return 1;
    }
 
    pop_localvar(parent);
    free(newvar);

    if(toomany) {
        fprintf(stderr, "%s:%u Too many local variables (max: 256).\n", builder->parser->filepath.buffer, builder->parser->scan.line);  
        return 1;
    } 
    
    scanner_rewind(&builder3.parser->scan);
    node->right = builder3.output;

    *dest = node;
    return 0;
}

static int add_alias(expr_builder_t* builder, expr_node_t** dest) {
    scanner_next(&builder->parser->scan, &builder->token);

    strcut_t name;
    if(builder->token.type != TOKEN_LITERAL) {
        fprintf(stderr, "%s:%u Expected alias name\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    name = builder->token.literal;
    scanner_next(&builder->parser->scan, &builder->token);

    if(builder->token.type != TOKEN_SYMBOL || builder->token.symbol[0] != '=' || builder->token.symbol[1] != 0) {
        fprintf(stderr, "%s:%u Expected = after alias name\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    expr_builder_t builder2 = childbuilder(builder);
    if(parse(&builder2)) {
        return 1;
    }

    if(builder2.token.type != TOKEN_LITERAL || !strcut_equals(builder2.token.literal, "in")) {
        fprintf(stderr, "%s:%u Expected \"in\" after alias\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    alias_t* uh = mem_req(sizeof(alias_t));
    uh->name = name;
    uh->ref  = builder2.output;
    uh->next = builder->aliases;

    builder->aliases = uh;

    expr_builder_t builder3 = childbuilder(builder);
    if(parse(&builder3)) {
        free(uh);
        return 1;
    }

    scanner_rewind(&builder->parser->scan);
    
    builder->aliases = uh->next;
    free(uh);

    *dest = builder3.output;
    return 0;
}

static int add_body(expr_builder_t* builder, expr_node_t** node) {
    scanner_next(&builder->parser->scan, &builder->token);
    if(builder->token.type != TOKEN_SYMBOL || builder->token.symbol[0] != '{') {
        fprintf(stderr, "%s:%u Expected { before body\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    expr_builder_t builder2 = childbuilder(builder);
    if(parse(&builder2)) {
        return 1;
    }

    if(builder2.token.type != TOKEN_SYMBOL || builder2.token.symbol[0] != '}') {
        fprintf(stderr, "%s:%u Expected } after body\n", builder->parser->filepath.buffer, builder->parser->scan.line);   
        return 1;
    }

    *node = builder2.output;
    return 0;
}

static int add_operand(expr_builder_t* builder) {
    builder->this_was_op = 0;

    unsigned int line = builder->parser->scan.line;

    expr_node_t* node = NULL;
    switch(builder->token.type) {
    case TOKEN_SYMBOL:
        // search for unary operators
        for(size_t i = __OPERATOR_UNARY + 1; i < __OPREATOR_END; i++) {
            const operator_t* op = global_operators + i;
            if(builder->token.symbol[0] == op->symbol[0] && builder->token.symbol[1] == op->symbol[1]) {
                add_unary(builder, op);
                return 0;
            }
        }

        if(builder->token.symbol[0] == '{') {
            if(add_list(builder, &node)) {
                return 1;
            }
        }
        else if(builder->token.symbol[0] == '[') {
            if(add_array(builder, &node)) {
                return 1;
            }
        }
        else if(builder->token.symbol[0] == '(') {
            if(add_parentheses(builder, &node)) {
                return 1;
            }
        }

        break;
    case TOKEN_STRING:
        node = getnode(builder->parser, sizeof(dataptr_t), NODE_OPERAND);
        node->node_type = EXPR_NODE_STRING;
        // handle special characters
        {
            // special characters decrease the size of the string, so I allocate the upper limit
            char* p = parser_alloc(builder->parser, builder->token.string.len + 1);
            DATA_PTR(node)->string = p;

            for(size_t i = 0; i < builder->token.string.len; i++) {
                char c = builder->token.string.str[i];

                if(c == '\\') {
                    char n = builder->token.string.str[i + 1];
                    *(p++) = special_char(n);
                    i++;
                }
                else {
                    *(p++) = c;
                }
            }
            *p = 0;
        }
        break;
    case TOKEN_INTEGER:
        node = getnode(builder->parser, sizeof(data32_t), NODE_OPERAND);
        node->node_type = EXPR_NODE_INTEGER;
        DATA_32(node)->integer = builder->token.integer;
        break;
    case TOKEN_CHAR:
        node = getnode(builder->parser, sizeof(data16_t), NODE_OPERAND);
        node->node_type = EXPR_NODE_CHARACTER;
        DATA_16(node)->character = builder->token.cchar;
        break;
    case TOKEN_LITERAL:
        if(strcut_equals(builder->token.literal, "L")) {
            if(add_function(builder, &node)) {
                return 1;
            }
            break;
        }

        if(strcut_equals(builder->token.literal, "null")) {
            node = getnode(builder->parser, 0, NODE_OPERAND);
            node->node_type = EXPR_NODE_NULL;
            break;
        }

        if(strcut_equals(builder->token.literal, "let")) {
            if(add_let(builder, &node)) {
                return 1;
            }
            break;
        }

        if(strcut_equals(builder->token.literal, "alias")) {
            if(add_alias(builder, &node)) {
                return 1;
            }
            break;
        }

        if(strcut_equals(builder->token.literal, "true")) {
            node = getnode(builder->parser, sizeof(data32_t), NODE_OPERAND);
            node->node_type = EXPR_NODE_BOOLEAN;
            DATA_16(node)->boolean = 1;
            break;
        }

        if(strcut_equals(builder->token.literal, "false")) {
            node = getnode(builder->parser, sizeof(data32_t), NODE_OPERAND);
            node->node_type = EXPR_NODE_BOOLEAN;
            DATA_16(node)->boolean = 0;
            break;
        }

        if(strcut_equals(builder->token.literal, "body")) {
            if(add_body(builder, &node)) {
                return 1;
            }
            break;
        } 
        
        if(strcut_equals(builder->token.literal, "if")) {
            if(add_branch(builder, &node)) {
                return 1;
            }
            break;
        }

        uint16_t found = 0;
        // search aliases
        {
            for(alias_t* alias = builder->aliases; alias && !found; alias = alias->next) {
                if(strcut_match(alias->name, builder->token.literal)) {
                    node = getnode(builder->parser, 0, NODE_UNARY);
                    node->node_type = EXPR_NODE_SUB_EXPR;
                    node->child = alias->ref;

                    found = 1;
                }
            }

            if(found) {
                break;
            }
        }

        // search arguments 
        { 
            uint16_t found = 0;
            for(func_t* fn = builder->parent; fn && !found; fn = fn->parent) {
                if(!fn->valid) continue;

                uint8_t varidx = 0;
                local_var_t* v = NULL;

                for(v = fn->localvar; v; v = v->next) {
                    if(strcut_match(v->name, builder->token.literal)) {
                        found = 1;
                        break;
                    }
                    varidx++;
                }

                if(found) {
                    node = getnode(builder->parser, sizeof(datafnvar_t), NODE_OPERAND);
                    node->node_type = EXPR_NODE_FNLOCAL;

                    *DATA_FN(node) = (datafnvar_t) {
                        .srcfn = fn,
                        .varidx = varidx,
                        .vartype = v->type,
                    };
                }
            }

            if(found) {
                break;
            }
        }

        const_t* v = const_find(builder->parser, builder->token.literal);
        if(!v) {
            typedef_t* def = typedef_find(builder->parser, builder->token.literal);
            if(def) {
                if(def->ref->prim != TYPE_ENUM) {
                    fprintf(stderr, "%s:%u \"%s\" cannot be passed as a value\n", builder->parser->filepath.buffer, builder->parser->scan.line, def->name);
                    return 1;
                }

                scanner_next(&builder->parser->scan, &builder->token);
                if(builder->token.type != TOKEN_SYMBOL || builder->token.symbol[0] != '.' || builder->token.symbol[1] != 0) {
                    fprintf(stderr, "%s:%u Expected . after \"%s\"\n", builder->parser->filepath.buffer, builder->parser->scan.line, def->name);
                    return 1;
                } 

                scanner_next(&builder->parser->scan, &builder->token);
                if(builder->token.type != TOKEN_LITERAL) {
                    fprintf(stderr, "%s:%u Expected entry name after .\n", builder->parser->filepath.buffer, builder->parser->scan.line);
                    return 1;
                }

                strcut_t target = builder->token.literal;

                enum_entry_t* entries = def->ref->enumm.entries;
                size_t entry_count = def->ref->enumm.entry_count;
                 
                enum_entry_t* found = NULL;
                for(size_t i = 0; i < entry_count && !found; i++) {
                    if(strcut_equals(target, entries[i].name)) {
                        found = entries + i;
                    }
                }

                if(found) { 
                    node = getnode(builder->parser, sizeof(data32_t), NODE_OPERAND);
                    node->node_type = EXPR_NODE_INTEGER;
                    DATA_32(node)->integer = found->value;
                }
                else {
                    fprintf(stderr, "%s:%u enum \"%s\" does not contain entry \"%.*s\"\n", builder->parser->filepath.buffer, builder->parser->scan.line, def->name, target.len, target.str);
                    return 1;
                }
            }
            else {
                external_t* ext = extern_find(builder->parser, builder->token.literal);

                if(ext) {
                    scanner_next(&builder->parser->scan, &builder->token);
                    if(builder->token.type != TOKEN_SYMBOL || builder->token.symbol[0] != '(') {
                        fprintf(stderr, "%s:%u Expected '(' after \"%s\"\n", builder->parser->filepath.buffer, builder->parser->scan.line, ext->name);
                        return 1;
                    }
 
                    expr_builder_t b2 = childbuilder(builder);
                    b2.has_elements = 1;
                    b2.allow_empty  = 1;

                    if(parse(&b2)) {
                        return 1;
                    }
                    
                    if(b2.token.type != TOKEN_SYMBOL || b2.token.symbol[0] != ')') {
                        fprintf(stderr, "%s:%u Expected ')' after extern arguments\n", builder->parser->filepath.buffer, builder->parser->scan.line);
                        return 1;
                    }

                    node = getnode(builder->parser, sizeof(dataptr_t), NODE_UNARY);
                    node->node_type = EXPR_NODE_EXTERNCALL;
                    node->child = b2.output;
                    DATA_PTR(node)->external = ext;
                }
                else {
                    fprintf(stderr, "%s:%u \"%.*s\" not defined\n", builder->parser->filepath.buffer, builder->parser->scan.line, builder->token.literal.len, builder->token.literal.str);
                    return 1;
                }
            }
        }
        else {
            node = getnode(builder->parser, sizeof(dataptr_t), NODE_OPERAND);
            node->node_type = EXPR_NODE_CONSTANT;
            DATA_PTR(node)->constant = v;
        }
        break;
    }

    if(!node) { 
        fprintf(stderr, "%s:%u Unexpected ", builder->parser->filepath.buffer, builder->parser->scan.line);
        token_print(stderr, &builder->token);
        fputc('\n', stderr);
        return 1;
    }

    node->next = builder->output;
    node->line = line;
    builder->output = node;
    return 0;
}

/////////////////////////////////////////////

static type_t* typecheck_fnargs(parser_t* parser, type_t* fntype, unsigned int line, expr_node_t* args) { 
    int argc = fntype->func.argcount;
    int has_vargs = fntype->func.hasvargs;
    type_t* output_type = fntype->func.types[argc];
 
    if(!args) { 
        if(argc == 1 && has_vargs) {
            return output_type;
        } 

        if(argc) {
            fprintf(stderr, "%s:%u Function accepts arguments, not void\n", parser->filepath.buffer, line);
            return NULL;
        } 

        return output_type;
    }
    
    if(argc == 0) {
        fprintf(stderr, "%s:%u Function does not accept any arguments\n", parser->filepath.buffer, args->line);
        return NULL;
    }  

    element_iterator_t it = {.node = args};
    int i = 0;
 
    for(int i = 0; i < argc - has_vargs; i++) {
        expr_node_t* arg = nextelement(&it); 
        if(!arg) {
            fprintf(stderr, "%s:%u Expected more arguments\n", parser->filepath.buffer, args->line);
            return NULL;
        }
 
        type_t* argument_type = typecheck(parser, arg);
        if(!argument_type) {
            return NULL;
        } 

        if(!type_equivalent(argument_type, fntype->func.types[i])) {
            fprintf(stderr, "%s:%u Wrong type for argument #%d\n", parser->filepath.buffer, arg->line, i);
            return NULL;
        }
    }

    if(has_vargs) {
        type_t* vtype = fntype->func.types[argc - 1];
        int idx = 0;
        for(expr_node_t* varg; varg = nextelement(&it); idx++) {
            type_t* argument_type = typecheck(parser, varg);
            if(!argument_type) {
                return NULL;
            }

            if(vtype && !type_equivalent(argument_type, vtype)) {
                fprintf(stderr, "%s:%u Wrong type for varg #%d\n", parser->filepath.buffer, varg->line, idx);
                return NULL;
            }
        }
    }
    return output_type;
}

static type_t* typecheck(parser_t* parser, expr_node_t* node) {
    type_t* prims = (type_t*)(parser->root->types.list + 1);

    type_t* int_type  = prims + TYPE_INT;
    type_t* bool_type = prims + TYPE_BOOL;
    type_t* ptr_type  = prims + TYPE_PTR;
    type_t* str_type  = prims + TYPE_STRING;
    type_t* char_type = prims + TYPE_CHAR;
    type_t* any_type  = prims + TYPE_ANY;

    switch(node->node_type) {
    case EXPR_NODE_INTEGER:
        return int_type;
    case EXPR_NODE_NULL:
        return any_type;
    case EXPR_NODE_STRING:
        return str_type;
    case EXPR_NODE_CHARACTER:
        return char_type;
    case EXPR_NODE_SUB_EXPR:
        return typecheck(parser, node->child);
    case EXPR_NODE_LETDEF:
        return typecheck(parser, node->right);
    case EXPR_NODE_TUPLE:
        return DATA_PTR(node)->tuple_type;
    case EXPR_NODE_LIST:
    case EXPR_NODE_ARRAY:
        return DATA_LIST(node)->type;
    case EXPR_NODE_BOOLEAN:
        return bool_type;
    case EXPR_NODE_CONSTANT:
        return ((const_t*)DATA_PTR(node)->constant)->expr.type;
    case EXPR_NODE_STRING_ACCESS:
        return char_type; 
    case EXPR_NODE_CALL: {
        type_t* func = typecheck(parser, node->right);
        if(!func) {
            return NULL;
        }
        
        if(func->prim != TYPE_FUNC) {
            fprintf(stderr, "%s:%u Expected function on the lhs\n", parser->filepath.buffer, node->line);
            return NULL;
        } 
        
        return typecheck_fnargs(parser, func, node->line, node->left);
    }
    case EXPR_NODE_EXTERNCALL: { 
        external_t* ext = DATA_PTR(node)->external;
        return typecheck_fnargs(parser, ext->type, node->line, node->child);
    }
    case EXPR_NODE_TYPEOF: {
        type_t* child = typecheck(parser, node->child);
        if(!child) {
            return NULL;
        }

        return bool_type;
    }
    case EXPR_NODE_CAST: {
        type_t* child = typecheck(parser, node->child);
        if(!child) {
            return NULL;
        }
        
        type_t* cast_type = DATA_LONG(node)->cast_type;

        if(type_equals(child, cast_type, 1)) {
            fprintf(stderr, "%s:%u No reason for cast\n", parser->filepath.buffer, node->line);
            return NULL;
        }
       
        if(child->prim == TYPE_ANY) {
            if(cast_type->prim == TYPE_FUNC) {
                DATA_LONG(node)->cast_instruction = CAST_ANY_FUNC;
            }
            else {
                DATA_LONG(node)->cast_instruction = CAST_FROM_ANY;
            }
            return cast_type;
        }

        switch(cast_type->prim) {
        case TYPE_STRING: {
            switch(child->prim) {
            case TYPE_CHAR:
                DATA_LONG(node)->cast_instruction = CAST_CHAR_STRING;
                return cast_type;
            }
            break;
        }
        case TYPE_ANY: 
            DATA_LONG(node)->cast_instruction = CAST_TO_ANY;
            return any_type;
        }

        if(type_interchangable(child, cast_type)) {
            return cast_type;
        }

        fprintf(stderr, "%s:%u Invalid cast\n", parser->filepath.buffer, node->line);
        return NULL;
    }
    case EXPR_NODE_TUPLE_ACCESS: {
        type_t* child = typecheck(parser, node->child);
        if(!child) {
            return NULL;
        }

        if(child->prim != TYPE_TUPLE) {
            fprintf(stderr, "%s:%u Expected tuple for tuple access\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        if(DATA_16(node)->tupidx >= child->tuple.mcount) {
            fprintf(stderr, "%s:%u Tuple doesn't have that many elements\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        return child->tuple.memb[DATA_16(node)->tupidx];
    }
    case EXPR_NODE_VALUE_PROPERTY: {
        return DATA_LONG(node)->property_type;
    }
    case EXPR_NODE_STRUCT_ACCESS: {
        type_t* child = typecheck(parser, node->child);
        if(!child) {
            return NULL;
        }

        char* target = DATA_LONG(node)->property_name;

        switch(child->prim) {
        case TYPE_ARRAY:
            if(strcmp(target, "length") == 0) {
                node->node_type = EXPR_NODE_VALUE_PROPERTY;
                DATA_LONG(node)->property_vp = VP_ARRAY_LEN;
                DATA_LONG(node)->property_type = int_type;
                return int_type;
            }
            break;
        case TYPE_STRING:
            if(strcmp(target, "length") == 0) {
                node->node_type = EXPR_NODE_VALUE_PROPERTY;
                DATA_LONG(node)->property_vp = VP_STRING_LEN;
                DATA_LONG(node)->property_type = int_type;
                return int_type;
            }
            break;
        }

        if(child->prim != TYPE_STRUCT) {
            fprintf(stderr, "%s:%u Expected struct for struct access\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        uint8_t pc = child->structt.tuptype->tuple.mcount;
        for(size_t i = 0; i < pc; i++) {
            if(strcmp(child->structt.names[i], target) == 0) {
                DATA_LONG(node)->property_index = i;
                return child->structt.tuptype->tuple.memb[i];
            }
        }

        fprintf(stderr, "%s:%u Struct does not contain any property named \"%s\"\n", parser->filepath.buffer, node->line, target);
        return NULL;
    }
    case EXPR_NODE_FNLOCAL: {
        datafnvar_t* fnv = DATA_FN(node);
        return fnv->vartype;
    }
    case EXPR_NODE_BODY: {
        type_t* left = typecheck(parser, node->left);
        if(!left) {
            return NULL;
        }

        type_t* right = typecheck(parser, node->right);
        if(!right) {
            return NULL;
        }

        return right;
    }
    case EXPR_NODE_ARRAY_SLICE: {
        type_t* left = typecheck(parser, node->left);
        if(!left) {
            return NULL;
        }

        type_t* right = typecheck(parser, node->right);
        if(!right) {
            return NULL;
        }

        type_t* extra = typecheck(parser, DATA_PTR(node)->extra_child);
        if(!extra) {
            return NULL;
        }

        if(right->prim == TYPE_STRING) {
            node->node_type = EXPR_NODE_STRING_SLICE;
        }
        else if(right->prim != TYPE_ARRAY) {
            fprintf(stderr, "%s:%u Expected string or array on the lhs\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        if(extra->prim != TYPE_INT || left->prim != TYPE_INT) {
            fprintf(stderr, "%s:%u From/to indexes should be integers\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        return right;
    }
    case EXPR_NODE_ARRAY_ACCESS: {
        type_t* left = typecheck(parser, node->left);
        if(!left) {
            return NULL;
        }

        type_t* right = typecheck(parser, node->right);
        if(!right) {
            return NULL;
        }

        // got my sides mixed up and shi
        if(left->prim != TYPE_INT) {
            fprintf(stderr, "%s:%u Array index must be a integer\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        if(right->prim == TYPE_STRING) {
            node->node_type = EXPR_NODE_STRING_ACCESS;
            return char_type;
        }

        if(right->prim != TYPE_ARRAY) {
            fprintf(stderr, "%s:%u Expected array on the lhs\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        return right->array.memb;
    }
    case EXPR_NODE_FUNCTION: {
        return ((func_t*)DATA_PTR(node)->function)->type;
    }
    case EXPR_NODE_BRANCH: {
        datacond_t* cond = DATA_COND(node);

        type_t* cc = typecheck(parser, cond->condition);
        if(!cc) {
            return NULL;
        }

        type_t* tc = typecheck(parser, cond->true_case);
        if(!tc) {
            return NULL;
        }

        type_t* fc = typecheck(parser, cond->false_case);
        if(!fc) {
            return NULL;
        }

        if(cc->prim != TYPE_BOOL) {
            fprintf(stderr, "%s:%u Condition must be a boolean\n", parser->filepath.buffer, node->line);
            return NULL;
        }

        if(!type_equivalent(tc, fc)) {
            fprintf(stderr, "%s:%u All branches must be of the same type\n", parser->filepath.buffer, node->line);
            return NULL;
        }
        return typecheck(parser, cond->false_case);
    }
    case EXPR_NODE_OPERATOR: {
        optype_t type = DATA_16(node)->operator;

        type_t* right = typecheck(parser, node->right);
        type_t* left  = NULL;

        if(!right) { 
            return NULL;
        }

        if(type < __OPERATOR_UNARY) {
            left = typecheck(parser, node->left);
            
            if(!left) { 
                return NULL;
            }
        }

        switch(type) {
        case OPERATOR_ARRAY_CONCAT:
            if(left->prim != TYPE_ARRAY || right->prim != TYPE_ARRAY) {
                fprintf(stderr, "%s:%u Expected arrays to concatenate\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            if(!type_equivalent(left->array.memb, right->array.memb)) {
                fprintf(stderr, "%s:%u Cannot concatenate such arrays\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            return left;
        case OPERATOR_COMMA:
            return right;
        case OPERATOR_LIST_INSERT:
            if(right->prim != TYPE_LIST) {
                fprintf(stderr, "%s:%u Expected list on the rhs to insert to\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            if(!type_equivalent(left, right->list.memb)) {
                fprintf(stderr, "%s:%u Cannot insert element into list, wrong type\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            return right;
        case OPERATOR_AND:
        case OPERATOR_OR:
            if(right->prim != TYPE_BOOL || left->prim != TYPE_BOOL) {
                fprintf(stderr, "%s:%u %s is performed between booleans\n", parser->filepath.buffer, node->line, global_operators[DATA_16(node)->operator].symbol);
                return NULL;
            }

            return bool_type;
        case OPERATOR_EQUAL:
        case OPERATOR_NOT_EQUAL:
            if((right->prim == TYPE_ENUM && left->prim == TYPE_ENUM) ||(right->prim == TYPE_INT && left->prim == TYPE_INT) || (right->prim == TYPE_CHAR && left->prim == TYPE_CHAR) || (right->prim == TYPE_BOOL && left->prim == TYPE_BOOL))
                return bool_type;

            if(type_iscomb(right, left, TYPE_INT, TYPE_ENUM)) {
                return bool_type;
            }
            
            fprintf(stderr, "%s:%u %s is performed between integers/enums/characters/booleans\n", parser->filepath.buffer, node->line, global_operators[DATA_16(node)->operator].symbol);
            return NULL;
        case OPERATOR_GREATER:
        case OPERATOR_GREATER_EQUAL:
        case OPERATOR_LESS:
        case OPERATOR_LESS_EQUAL:
            if(right->prim != TYPE_INT || left->prim != TYPE_INT) {
                fprintf(stderr, "%s:%u %s is performed between integers\n", parser->filepath.buffer, node->line, global_operators[DATA_16(node)->operator].symbol);
                return NULL;
            }

            return bool_type;
        case OPERATOR_STRING_CONCAT:
            if(left->prim != TYPE_STRING || right->prim != TYPE_STRING) {
                fprintf(stderr, "%s:%u Expected strings to concatenate \n", parser->filepath.buffer, node->line);
                return NULL;
            }

            return str_type;
        case OPERATOR_DIV:
        case OPERATOR_SUB:
        case OPERATOR_MUL:
        case OPERATOR_ADD:
            if(right->prim != TYPE_INT || left->prim != TYPE_INT) {
                fprintf(stderr, "%s:%u %s is performed between integers\n", parser->filepath.buffer, node->line, global_operators[DATA_16(node)->operator].symbol);
                return NULL;
            }

            return int_type;
        case OPERATOR_NOT:
            if(right->prim != TYPE_BOOL) {
                fprintf(stderr, "%s:%u You can only invert booleans\n", parser->filepath.buffer, node->line);
                return NULL;
            }
            return bool_type;
        case OPERATOR_NEG:
            if(right->prim != TYPE_INT) {
                fprintf(stderr, "%s:%u You can only negate integers\n", parser->filepath.buffer, node->line);
                return NULL;
            }
            return int_type;
        case OPREATOR_TAIL:
            if(right->prim != TYPE_LIST) {
                fprintf(stderr, "%s:%u Expected list on the rhs\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            return right;
        case OPREATOR_NOT_NULL:
            if(!type_isobj(right)) {
                fprintf(stderr, "%s:%u Expected nullable type on the rhs\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            return bool_type;
        case OPERATOR_FIRST:
            if(right->prim != TYPE_LIST) {
                fprintf(stderr, "%s:%u Expected list on the rhs\n", parser->filepath.buffer, node->line);
                return NULL;
            }

            return right->list.memb;
        } 
    } 
    default:
        fprintf(stderr, "%s: hello you shouldn't be here!! %d\n", parser->filepath.buffer, node->node_type);
        return NULL;
    }
}

static int parse(expr_builder_t* builder) {
    for(;;) {
        scanner_next(&builder->parser->scan, &builder->token);
        if(builder->token.type == TOKEN_SYMBOL) {
            char* symbol = builder->token.symbol;
            if(symbol[0] == ';' || symbol[0] == ')' || symbol[0] == '}' || symbol[0] == ']') {
                break;
            }
        }
        else if(builder->token.type == TOKEN_LITERAL) {
            // valid keywords to die to
            if( strcut_equals(builder->token.literal, "then") || 
                strcut_equals(builder->token.literal, "in") || 
                strcut_equals(builder->token.literal, "else") || 
                strcut_equals(builder->token.literal, "to") 
            ) {
                break;
            }
        }
        else if(builder->token.type == TOKEN_EOF) {
            fprintf(stderr, "%s:%u: Unexpected EOF\n", builder->parser->filepath.buffer, builder->parser->scan.line);
            return 1;
        }

        if(builder->last_was_op ? add_operand(builder) : add_operator(builder)) {
            return 1;
        }

        builder->last_was_op = builder->this_was_op;
    }

    while(builder->operators) {
        if(pop_operator(builder)) {
            return 1;
        }
    }
 
    if(!builder->output) {
        if(builder->allow_empty) {
            return 0;
        }
        fprintf(stderr, "%s:%u: Empty Expression\n", builder->parser->filepath.buffer, builder->parser->scan.line);  
        return 1;
    }

    if(!builder->has_elements && builder->output->node_type == EXPR_NODE_OPERATOR && DATA_16(builder->output)->operator == OPERATOR_COMMA) {
        builder->output->node_type = EXPR_NODE_BODY;
    }

    return 0;
}

////////////////////////////////

void expr_emit_node(expr_node_t* node, int final, string_t* bc, func_t* fn) {
    switch(node->node_type) {
    case EXPR_NODE_CHARACTER:
        string_push(bc, OP_PUSH_CHAR);
        string_push(bc, DATA_16(node)->character);
        break; 
    case EXPR_NODE_INTEGER: {
        uint32_t uh = DATA_32(node)->integer;

        if(uh > UINT16_MAX) {
            string_push(bc, OP_PUSH32);
            string_write32(bc, uh);
        }
        else if(uh > UINT8_MAX) {
            string_push(bc, OP_PUSH16);
            string_write16(bc, uh);
        }
        else {
            string_push(bc, OP_PUSH8);
            string_push(bc, uh);
        }
        break;
    }
    case EXPR_NODE_NULL:
        string_push(bc, OP_PUSH_NULL);
        break;
    case EXPR_NODE_SUB_EXPR:
        expr_emit_node(node->child, final, bc, fn);
        break;
    case EXPR_NODE_TUPLE: {
        expr_emit_node(node->right, 0, bc, fn); // write elements 
        string_push(bc, OP_TUPLE_CREATE);
        string_write16(bc, type_getid(DATA_PTR(node)->tuple_type));
        break;
    }
    case EXPR_NODE_TUPLE_ACCESS: {
        expr_emit_node(node->child, 0, bc, fn);
        string_push(bc, OP_TUPLE_GET);
        string_push(bc, DATA_16(node)->tupidx);
        break;
    }
    case EXPR_NODE_STRUCT_ACCESS: {
        expr_emit_node(node->child, 0, bc, fn);
        string_push(bc, OP_TUPLE_GET);
        string_push(bc, DATA_LONG(node)->property_index);
        break;
    }
    case EXPR_NODE_CAST: {
        expr_emit_node(node->child, 0, bc, fn);
        
        uint8_t cop = DATA_LONG(node)->cast_instruction;
        string_push(bc, OP_CAST);
        string_push(bc, cop);
        string_write16(bc, DATA_LONG(node)->cast_type->id);
        break;
    }
    case EXPR_NODE_TYPEOF: {
        expr_emit_node(node->child, 0, bc, fn);
        string_push(bc, OP_TYPEOF);
        
        type_t* type = DATA_PTR(node)->typeof_type;
        string_write16(bc, type_getid(type));

        if(type->prim == TYPE_FUNC) {
            string_write16(bc, type->id);
        }
        break;
    }
    case EXPR_NODE_BODY: {
        expr_emit_node(node->left, 0, bc, fn);
        string_push(bc, OP_POP);

        node = node->right;
        for(; node && node->node_type == EXPR_NODE_OPERATOR && DATA_16(node)->operator == OPERATOR_COMMA; node = node->right) {
            expr_emit_node(node->left, 0, bc, fn);
            string_push(bc, OP_POP);
        }

        expr_emit_node(node, final, bc, fn);
        break;
    }
    case EXPR_NODE_STRING: {
        string_push(bc, OP_STRING_CREATE);

        // kinda ass but whatever
 
        uint32_t len = 0; 
        size_t lenpos = bc->length;
        string_write32(bc, 0); 

        char* p = DATA_PTR(node)->string;
        for(; *p; len++, p++) {
            string_push(bc, *p);
        }

        string_write32_at(bc, lenpos, len);
        break;
    }
    case EXPR_NODE_LIST: {
        expr_emit_node(node->right, 0, bc, fn); // write elements
        string_push(bc, OP_LIST_CREATE);

        string_write32(bc, DATA_LIST(node)->membc);
        string_write16(bc, DATA_LIST(node)->type->id);
        break;
    }
    case EXPR_NODE_ARRAY: {
        expr_emit_node(node->right, 0, bc, fn);
        string_push(bc, OP_ARRAY_CREATE);

        string_write32(bc, DATA_LIST(node)->membc);
        string_write16(bc, type_getid(DATA_LIST(node)->type));
        break;
    }
    case EXPR_NODE_CALL: {
        if(node->left) {
            expr_emit_node(node->left, 0, bc, fn);
        }

        expr_emit_node(node->right, 0, bc, fn);  
        string_push(bc, final ? OP_FN_TAILCALL : OP_FNCALL);
        
        break;
    }
    case EXPR_NODE_ARRAY_ACCESS: {
        expr_emit_node(node->right, 0, bc, fn);
        expr_emit_node(node->left, 0, bc, fn);
        string_push(bc, OP_ARRAY_GET);
        break;
    }
    case EXPR_NODE_ARRAY_SLICE: {
        expr_emit_node(node->right, 0, bc, fn);
        expr_emit_node(node->left, 0, bc, fn);
        expr_emit_node(DATA_PTR(node)->extra_child, 0, bc, fn);
        string_push(bc, OP_ARRAY_SLICE);
        break;
    }
    case EXPR_NODE_STRING_SLICE: {
        expr_emit_node(node->right, 0, bc, fn);
        expr_emit_node(node->left, 0, bc, fn);
        expr_emit_node(DATA_PTR(node)->extra_child, 0, bc, fn);
        string_push(bc, OP_STRING_SLICE);
        break;
    }
    case EXPR_NODE_STRING_ACCESS: {
        expr_emit_node(node->right, 0, bc, fn);
        expr_emit_node(node->left, 0, bc, fn);
        string_push(bc, OP_STRING_GET);
        break;
    }
    case EXPR_NODE_VALUE_PROPERTY: {
        expr_emit_node(node->child, 0, bc, fn);
        string_push(bc, OP_VALUE_PROPERTY);
        string_push(bc, DATA_LONG(node)->property_vp);
        break;
    }
    case EXPR_NODE_CONSTANT: {
        string_push(bc, OP_VGET);
        string_write16(bc, ((const_t*)DATA_PTR(node)->constant)->id);
        break;
    }
    case EXPR_NODE_LETDEF: {
        expr_emit_node(node->left, 0, bc, fn);
        string_push(bc, OP_FN_ADDLOCAL);
        expr_emit_node(node->right, final, bc, fn);
        string_push(bc, OP_FN_REMLOCAL);
        break;
    }
    case EXPR_NODE_BOOLEAN: {
        string_push(bc, OP_PUSH_BOOL);
        string_push(bc, DATA_16(node)->boolean);
        break;
    }
    case EXPR_NODE_EXTERNCALL: {
        external_t* ext = DATA_PTR(node)->external; 
    
        if(node->child) {
            expr_emit_node(node->child, 0, bc, fn);
        }

        if(ext->type->func.hasvargs) {
            string_push(bc, OP_PUSH32);
            
            int c = ext->type->func.argcount - 1;
            uint32_t vargc = count_elements(node->child) - c;
            string_write32(bc, vargc);
        }

        string_push(bc, OP_CALL_EXTERN);
        string_write16(bc, ext->id);
        break;
    }
    case EXPR_NODE_FNLOCAL: {
        string_push(bc, OP_FNLOCAL);
        datafnvar_t* dfv = DATA_FN(node); 

        int steps = 0;
        for(func_t* f = fn; f && f != dfv->srcfn; f = f->parent, steps++);

        string_write16(bc, steps);
        string_push(bc, dfv->varidx);
        break;
    }
    case EXPR_NODE_FUNCTION: {
        string_push(bc, OP_FNGET);
        string_write16(bc, ((func_t*)DATA_PTR(node)->function)->id);
        break;
    }
    case EXPR_NODE_OPERATOR: {
        optype_t idx = DATA_16(node)->operator;

        if(idx == OPERATOR_AND) {
            expr_emit_node(node->left, 0, bc, fn);

            string_push(bc, OP_HOP);
            string_push(bc, 0);
            
            size_t wpos = bc->length;
            string_write32(bc, 0);

            expr_emit_node(node->right, 0, bc, fn); 
            string_write32_at(bc, wpos, bc->length);
        }
        else if(idx == OPERATOR_OR) {
            expr_emit_node(node->left, 0, bc, fn);

            string_push(bc, OP_HOP);
            string_push(bc, 1);
            
            size_t wpos = bc->length;
            string_write32(bc, 0);

            expr_emit_node(node->right, 0, bc, fn); 
            string_write32_at(bc, wpos, bc->length);
        }
        else {   
            expr_emit_node(node->right, 0, bc, fn);
            if(idx < __OPERATOR_UNARY) {
                expr_emit_node(node->left, 0, bc, fn);
            }
        }

        if(global_operators[idx].opcode) {
            string_push(bc, global_operators[idx].opcode);
        }
        break;
    }
    case EXPR_NODE_BRANCH: {
        datacond_t* cond = DATA_COND(node);
        expr_emit_node(cond->condition, 0, bc, fn);
        
        string_push(bc, OP_JT);
        size_t j0 = bc->length;
        string_write32(bc, 0);

        expr_emit_node(cond->false_case, final, bc, fn);
        string_push(bc, OP_JMP);
        size_t j1 = bc->length;
        string_write32(bc, 0);

        string_write32_at(bc, j0, bc->length);

        expr_emit_node(cond->true_case, final, bc, fn);
        string_write32_at(bc, j1, bc->length);
        break;
    }
    
    }
}

int expr_parse(parser_t* parser, expr_t* expr) {
    expr_builder_t builder = {
        .parser = parser,
        .operators = NULL,
        .output = NULL,
        .parent = NULL,
        .last_was_op = 1,
        .this_was_op = 0,
        .has_elements = 0
    };

    if(parse(&builder)) {
        scanner_rewind(&parser->scan);
        parser_skip_statement(parser);
        return 1;
    }

    if(builder.token.type != TOKEN_SYMBOL || builder.token.symbol[0] != ';') {
        fprintf(stderr, "%s:%u Expected ;\n", builder.parser->filepath.buffer, builder.parser->scan.line);
        return 1;
    }

    expr->root = builder.output;
    expr->type = typecheck(parser, builder.output);

    if(!expr->type) {
        return 1;
    }

    return 0;
}