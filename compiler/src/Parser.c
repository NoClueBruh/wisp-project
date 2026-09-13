#include "Parser.h"
#include "Value.h"
#include "Expr.h"

/// 
int parser_isroot(parser_t* parser) {
    return parser->pid == 0;
}

void* parser_addconst(parser_t* parser) { 
    const_t* c = arena_alloc(&parser->root->consts, sizeof(const_t));
    c->pid = parser->pid;
    return c;
}

void* parser_addtypedef(parser_t* parser) {
    typedef_t* t = arena_alloc(&parser->root->typedefs, sizeof(typedef_t));
    t->pid = parser->pid;
    return t;
}

static void* parser_addtype(parser_t* parser) {
    return arena_alloc(&parser->root->types, sizeof(type_t));
}

void* parser_addfunct(parser_t* parser) {
    return arena_alloc(&parser->root->functs, sizeof(func_t));
}

void* parser_addmeta(parser_t* parser) {
    return arena_alloc(&parser->root->meta, sizeof(metainfo_t));
}

void* parser_addextern(parser_t* parser) {
    external_t* ext = arena_alloc(&parser->root->externals, sizeof(external_t));
    ext->pid = parser->pid;
    return ext;
}

void* parser_alloc(parser_t* parser, size_t size) {
    return arena_alloc(&parser->root->other, size);
}

char* parser_copycut(parser_t* parser, strcut_t cut) {
    char* str = parser_alloc(parser, cut.len + 1);
    str[cut.len] = 0;
    memcpy(str, cut.str, cut.len);
    return str;
}

//////////////////////////////////////////////////
 
int parser_register_type(parser_t* parser, type_t* type, type_t** dest) {
    type_t* dup = type_getdup(parser, type);

    if(dup) {
        *dest = dup;
        return 0;
    }

    type_t* newtype = arena_alloc(&parser->root->types, sizeof(type_t));
    *newtype = *type;
    
    // structs are tuples under the hood, so they dont get a new id, they just inherit the underlying tuple's id
    // enums are ints
    // functions dont need all that info at runtime
    if(type->prim == TYPE_FUNC) {
        newtype->id = parser->root->snum++; 
    }
    else if(type->prim != TYPE_STRUCT && type->prim != TYPE_ENUM) {
        newtype->id = parser->root->typec++;
    }

    *dest = newtype;

    return 1;
}

void parser_skip_statement(parser_t* parser) {
    token_t token;
    do {
        scanner_next(&parser->scan, &token);
    }
    while(token.type != TOKEN_EOF && (token.type != TOKEN_SYMBOL || token.symbol[0] != ';'));
}

int parse_type(parser_t* parser, type_t** dest) { 
    token_t token;
    scanner_next(&parser->scan, &token);

    // kinda ugly but
    type_t* prims = (type_t*)(parser->root->types.list + 1);

    type_t* int_type  = prims + TYPE_INT;
    type_t* bool_type = prims + TYPE_BOOL;
    type_t* ptr_type  = prims + TYPE_PTR;
    type_t* str_type  = prims + TYPE_STRING;
    type_t* char_type = prims + TYPE_CHAR;
    type_t* any_type  = prims + TYPE_ANY;

    int gotit = 0;
    if(token.type == TOKEN_LITERAL) {
        if(strcut_equals(token.literal, "int")) {
            *dest = int_type;
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "bool")) {
            *dest = bool_type;
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "string")) {
            *dest = str_type;
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "char")) {
            *dest = char_type;
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "any")) {
            *dest = any_type;
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "pointer")) {
            *dest = ptr_type;
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "void")) {
            scanner_next(&parser->scan, &token);
            if(token.type != TOKEN_SYMBOL || token.symbol[0] != '-' || token.symbol[1] != '>') {
                fprintf(stderr, "%s:%u Expected -> after void\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }

            type_t* output;
            if(parse_type(parser, &output)) {
                return 1;
            }

            type_t tmp = {
                .prim   = TYPE_FUNC,
                .func = {
                    .argcount = 0,
                    .hasvargs = 0,
                    .types = &output
                }
            };

            gotit = 1;
            if(parser_register_type(parser, &tmp, dest)) {
                type_t** hm = parser_alloc(parser, sizeof(type_t*));
                *hm = output;

                (*dest)->func.types = hm;
            }
        }
        else if(strcut_equals(token.literal, "list")) {
            type_t* element;
            if(parse_type(parser, &element)) {
                return 1;
            }
            
            type_t tmp = {
                .prim = TYPE_LIST,
                .list.memb = element,
            };

            parser_register_type(parser, &tmp, dest);
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "array")) {
            type_t* element;
            if(parse_type(parser, &element)) {
                return 1;
            }
            
            type_t tmp = {
                .prim = TYPE_ARRAY,
                .array.memb = element,
            };

            parser_register_type(parser, &tmp, dest);
            gotit = 1;
        }
        else if(strcut_equals(token.literal, "enum")) {
            scanner_next(&parser->scan, &token);

            if(token.type != TOKEN_SYMBOL || token.symbol[0] != '(') { 
                fprintf(stderr, "%s:%u Expected '(' after enum\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }

            inpvec_t entries;
            inpvec_new(&entries, sizeof(enum_entry_t), 8);

            int prev = -1;
            for(;;) {
                scanner_next(&parser->scan, &token);

                if(token.type != TOKEN_LITERAL) { 
                    free(entries.elements);
                    fprintf(stderr, "%s:%u Expected enum entry\n", parser->filepath.buffer, parser->scan.line);
                    return 1;
                }

                enum_entry_t* entry = inpvec_app(&entries);
                entry->name = parser_copycut(parser, token.literal);
                entry->value = ++prev;
                
                scanner_next(&parser->scan, &token);
                if(token.type == TOKEN_SYMBOL && token.symbol[0] == '=' && token.symbol[1] == 0) {
                    scanner_next(&parser->scan, &token);

                    int sign = 1;
                    if(token.type == TOKEN_SYMBOL && token.symbol[0] == '-') {
                        sign = -1;
                        scanner_next(&parser->scan, &token);
                    }

                    if(token.type != TOKEN_INTEGER) {
                        free(entries.elements);
                        fprintf(stderr, "%s:%u Expected an integer after =\n", parser->filepath.buffer, parser->scan.line);
                        return 1;
                    }

                    int value = sign * token.integer;
                    entry->value = value;
                    prev = value;
                    
                    scanner_next(&parser->scan, &token);
                }

                if(token.type != TOKEN_SYMBOL) {
                    free(entries.elements);
                    fprintf(stderr, "%s:%u Expected ',' or ')' after enum entry\n", parser->filepath.buffer, parser->scan.line);
                    return 1;
                }

                if(token.symbol[0] == ',') {
                    continue;
                }
                if(token.symbol[0] == ')') {
                    break;
                }
            }

            type_t tmp = {
                .prim = TYPE_ENUM,
                .id   = TYPE_INT,
                .enumm = {
                    .entries = entries.elements,
                    .entry_count = entries.len
                }
            };

            gotit = 1;
            if(parser_register_type(parser, &tmp, dest)) {
                enum_entry_t* lock = parser_alloc(parser, sizeof(enum_entry_t) * entries.len);
                memcpy(lock, entries.elements, sizeof(enum_entry_t) * entries.len);
                (*dest)->enumm.entries = lock;
            }

            free(entries.elements);
        }
        else if(strcut_equals(token.literal, "struct")) {
            scanner_next(&parser->scan, &token);

            type_t* self = NULL;

            if(token.type == TOKEN_LITERAL) {
                self = parser_addtype(parser);
                self->prim = TYPE_STRUCT;
    
                typedef_t* tdef = parser_addtypedef(parser); 
                tdef->ref = self;
                tdef->name = parser_copycut(parser, token.literal);
                
                tdef->flags = (varflags_t) {0};
                tdef->flags.private = 1;

                scanner_next(&parser->scan, &token);
            }

            if(token.type == TOKEN_SYMBOL && token.symbol[0] == '(') {
                inpvec_t memb;
                inpvec_t names;

                inpvec_new(&names, sizeof(char*), 4);
                inpvec_new(&memb, sizeof(type_t*), 4);

                for(;;) {
                    type_t* p;
                    if(parse_type(parser, &p)) {
                        free(memb.elements);
                        free(names.elements);
                        return 1;
                    }
 
                    inpvec_push(&memb, &p);

                    scanner_next(&parser->scan, &token);
                    if(token.type != TOKEN_LITERAL) {
                        free(memb.elements);
                        free(names.elements);
                        fprintf(stderr, "%s:%u Expected property name\n", parser->filepath.buffer, parser->scan.line);
                        return 1;
                    }

                    char* name = parser_copycut(parser, token.literal);
                    inpvec_push(&names, &name); 

                    scanner_next(&parser->scan, &token);
                    if(token.type == TOKEN_SYMBOL) {
                        if(token.symbol[0] == ',')
                            continue;
                        if(token.symbol[0] == ')')
                            break;
                    }

                    free(memb.elements);
                    free(names.elements);
                    fprintf(stderr, "%s:%u Expected , or )\n", parser->filepath.buffer, parser->scan.line);
                    return 1;
                }

                if(memb.len > 0xFF) {
                    free(memb.elements);
                    free(names.elements);
                    fprintf(stderr, "%s:%u Too many tuple elements\n", parser->filepath.buffer, parser->scan.line);
                    return 1;
                }

                gotit = 1;
                type_t* tuptype;

                if(self) {
                    void* lock = parser_alloc(parser, sizeof(char*) * names.len);
                    memcpy(lock, names.elements, sizeof(char*) * names.len);

                    self->id = parser->root->typec;
                    self->structt.names = lock;
                    self->structt.tuptype = NULL;
                }

                // registering tuple type
                {
                    type_t tmp = {
                        .prim = TYPE_TUPLE,
                        .tuple = {
                            .mcount = memb.len,
                            .memb = memb.elements
                        }
                    }; 

                    if(parser_register_type(parser, &tmp, &tuptype)) {
                        void* lock = parser_alloc(parser, sizeof(type_t*) * memb.len);
                        memcpy(lock, memb.elements, sizeof(type_t*) * memb.len);
                        tuptype->tuple.memb = lock;
                    }
                }

                if(self) {
                    self->structt.tuptype = tuptype;
                    *dest = self;
                }
                else {
                    type_t tmp = {
                        .prim = TYPE_STRUCT,
                        .id = tuptype->id,
                        .structt = {
                            .tuptype = tuptype,
                            .names = names.elements
                        }
                    };
                    
                    if(parser_register_type(parser, &tmp, dest)) {
                        void* lock = parser_alloc(parser, sizeof(strcut_t) * names.len);
                        memcpy(lock, names.elements, sizeof(strcut_t) * names.len);
                        (*dest)->structt.names = lock;
                    }
                }
 
                free(memb.elements);
                free(names.elements);
            }
            else if(self) {
                fprintf(stderr, "%s:%u Expected properties of struct\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }
        }
        else {
            typedef_t* def = typedef_find(parser, token.literal);
            if(def) {
                gotit = 1;
                *dest = def->ref;
            }
        }
    }
    else if(token.type == TOKEN_SYMBOL) {
        if(token.symbol[0] == '(') {
            inpvec_t memb;
            inpvec_new(&memb, sizeof(type_t*), 4);

            for(;;) {
                type_t* p;
                if(parse_type(parser, &p)) {
                    free(memb.elements);
                    return 1;
                }

                inpvec_push(&memb, &p);

                scanner_next(&parser->scan, &token);
                if(token.type == TOKEN_SYMBOL) {
                    if(token.symbol[0] == ',')
                        continue;
                    if(token.symbol[0] == ')')
                        break;
                }

                free(memb.elements);
                fprintf(stderr, "%s:%u Expected , or )\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }

            gotit = 1;
            if(memb.len == 1) { 
                *dest = *(type_t**)inpvec_at(&memb, 0);
            }
            else {
                if(memb.len > 0xFF) {
                    free(memb.elements);
                    fprintf(stderr, "%s:%u Too many tuple elements\n", parser->filepath.buffer, parser->scan.line);
                    return 1;
                }

                type_t tmp = {
                    .prim = TYPE_TUPLE,
                    .tuple = {
                        .mcount = memb.len,
                        .memb = memb.elements
                    }
                };

                if(parser_register_type(parser, &tmp, dest)) {
                    void* lock = parser_alloc(parser, sizeof(type_t*) * memb.len);
                    memcpy(lock, memb.elements, sizeof(type_t*) * memb.len);
                    (*dest)->tuple.memb = lock;
                }
            }
            free(memb.elements);
        }
        else if(token.symbol[0] == '[') {
            int hasvargs = 0;

            inpvec_t args;
            inpvec_new(&args, sizeof(type_t*), 4);

            for(;;) {
                scanner_next(&parser->scan, &token);
                if(token.type == TOKEN_SYMBOL && token.symbol[0] == '.' && token.symbol[1] == '.') {
                    // variable args what??!!
                    hasvargs = 1; 
                    type_t* vargtype = NULL; // null means anything (not any)

                    scanner_next(&parser->scan, &token);
                    if(token.type != TOKEN_SYMBOL || token.symbol[0] != ']') {
                        scanner_rewind(&parser->scan);

                        int cooked = 0;
                        if(parse_type(parser, &vargtype)) {
                            cooked = 1; 
                        }

                        scanner_next(&parser->scan, &token);
                        if(cooked || token.type != TOKEN_SYMBOL || token.symbol[0] != ']') {
                            free(args.elements);
                            fprintf(stderr, "%s:%u Expected ']' after '..'\n", parser->filepath.buffer, parser->scan.line);
                            return 1;
                        }
                    }

                    inpvec_push(&args, &vargtype);
                    break;
                }
                scanner_rewind(&parser->scan);

                type_t* p;
                if(parse_type(parser, &p)) {
                    free(args.elements);
                    return 1;
                }

                inpvec_push(&args, &p);

                scanner_next(&parser->scan, &token);
                if(token.type == TOKEN_SYMBOL) {
                    if(token.symbol[0] == ',')
                        continue;
                    if(token.symbol[0] == ']')
                        break;
                }

                free(args.elements);
                fprintf(stderr, "%s:%u Expected , or ]\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }

            scanner_next(&parser->scan, &token);
            if(token.type != TOKEN_SYMBOL || token.symbol[0] != '-' || token.symbol[1] != '>') {
                free(args.elements);
                fprintf(stderr, "%s:%u Expected -> after function arguments\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }

            gotit = 1; 

            type_t* output;
            if(parse_type(parser, &output)) {
                free(args.elements);
                return 1;
            }
            inpvec_push(&args, &output);

            if(args.len > 0xFF) {
                free(args.elements);
                fprintf(stderr, "%s:%u Too many arguments\n", parser->filepath.buffer, parser->scan.line);
                return 1;
            }

            type_t tmp = {
                .prim = TYPE_FUNC,
                .func = {
                    .argcount = args.len - 1,
                    .hasvargs = hasvargs,
                    .types    = args.elements
                }
            };

            if(parser_register_type(parser, &tmp, dest)) {
                void* lock = parser_alloc(parser, sizeof(type_t*) * args.len);
                memcpy(lock, args.elements, sizeof(type_t*) * args.len);
                (*dest)->func.types = lock;
            } 
            free(args.elements);
        }
    }

    if(!gotit) {
        fprintf(stderr, "%s:%u Unknown type starting with ", parser->filepath.buffer, parser->scan.line);
        token_print(stderr, &token);
        fputc('\n', stderr);
        return 1;
    }

    scanner_next(&parser->scan, &token);
    if(token.type == TOKEN_SYMBOL && token.symbol[0] == '-' && token.symbol[1] == '>') {
        type_t* output_type;
        if(parse_type(parser, &output_type)) {
            return 1;
        }

        type_t* input = *dest;

        type_t** hm = mem_req(sizeof(type_t*) * 2);
        hm[0] = input;
        hm[1] = output_type;

        type_t tmp = {
            .prim = TYPE_FUNC,
            .func = {
                .argcount = 1,
                .hasvargs = 0,
                .types = hm
            }
        };

        if(parser_register_type(parser, &tmp, dest)) {
            type_t** lock = parser_alloc(parser, sizeof(type_t*) * 2);
            lock[0] = input;
            lock[1] = output_type;

            (*dest)->func.types = lock;
            free(hm);
        }
    }
    else {
        scanner_rewind(&parser->scan);
    }

    return 0;
}

static int parse_extern(parser_t* parser, varflags_t flags) {
    strcut_t name; 
    token_t  token;
    type_t*  type;

    scanner_next(&parser->scan, &token);
    if(token.type != TOKEN_LITERAL) {
        fprintf(stderr, "%s:%u Expected name of external\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    name = token.literal;
    if(name.len > 0xFF) { 
        fprintf(stderr, "%s:%u Name of extern is too long\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    scanner_next(&parser->scan, &token);
    if(token.type != TOKEN_SYMBOL || token.symbol[0] != ':') {
        fprintf(stderr, "%s:%u Expected : after name of extern\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    if(parse_type(parser, &type)) {
        return 1;
    }

    if(type->prim != TYPE_FUNC) { 
        fprintf(stderr, "%s:%u Externals can only be functions\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    scanner_next(&parser->scan, &token);
    if(token.type != TOKEN_SYMBOL || token.symbol[0] != ';') {
        fprintf(stderr, "%s:%u Expected ; after extern\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    external_t* extr = parser_addextern(parser);
    extr->name = parser_copycut(parser, name); 
    extr->flags = flags;
    extr->type = type;
    extr->id = parser->root->externals.length - 1;
    return 0;
}

static int parse_const(parser_t* parser, varflags_t flags) { 
    strcut_t cname;
    token_t  token;
    expr_t   expr;
    scanner_next(&parser->scan, &token);

    if(token.type != TOKEN_LITERAL) {
        fprintf(stderr, "%s:%u Expected name of constant\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    cname = token.literal;
    if(const_find(parser, cname)) {
        fprintf(stderr, "%s:%u Constant %.*s already defined\n", parser->filepath.buffer, parser->scan.line, cname.len, cname.str);
        parser_skip_statement(parser);
        return 1;
    }
 
    scanner_next(&parser->scan, &token);

    type_t* expected_type = NULL;
    const_t* newconst = NULL;

    if(token.type == TOKEN_SYMBOL && token.symbol[0] == ':') {
        if(parse_type(parser, &expected_type)) {
            parser_skip_statement(parser);
            return 1;
        }

        scanner_next(&parser->scan, &token);
    }
    
    if(token.type != TOKEN_SYMBOL || token.symbol[0] != '=' || token.symbol[1] != 0) {
        scanner_rewind(&parser->scan);
        
        fprintf(stderr, "%s:%u Expected constant initialization\n", parser->filepath.buffer, parser->scan.line);
        parser_skip_statement(parser);
        return 1;
    }

    if(expected_type) {
        newconst = parser_addconst(parser);
        newconst->name = parser_copycut(parser, cname);
        newconst->id = parser->root->consts.length - 1;
        newconst->expr.type = expected_type;
        newconst->flags = flags;
    }

    if(expr_parse(parser, &expr)) {
        parser_skip_statement(parser);
        return 1;
    }

    if(expected_type) {
        if(!type_equivalent(expected_type, expr.type)) {
            fprintf(stderr, "%s:%u Incorrect type for assignment\n", parser->filepath.buffer, parser->scan.line);
            fprintf(stderr, "\t** Expected ");
            type_print(stderr, expected_type);
            fprintf(stderr, " but got ");
            type_print(stderr, expr.type);
            fprintf(stderr, "\n");
            parser_skip_statement(parser);
            return 1;
        }

        expr.type = expected_type;
    }

    if(!newconst) {
        newconst = parser_addconst(parser);
        newconst->name = parser_copycut(parser, cname);
        newconst->id = parser->root->consts.length - 1;
        newconst->flags = flags;
    }

    newconst->expr = expr; 

    if(flags.exposed) {
        if(cname.len > 0xFF) {
            fprintf(stderr, "%s:%u Name too long for an exposed constant\n", parser->filepath.buffer, parser->scan.line);
            return 1;
        }
        
        metainfo_t* meta = parser_addmeta(parser);
        meta->name = newconst->name;
        meta->data.type = META_CONST;
        meta->data.data = newconst->id;
    }

    printf("Added constant successfully (%.*s)\n", cname.len, cname.str);
    return 0;
} 

compile_output_t parser_compile(heapstr_t path, parser_t* parent) {
    printf("Opened file \"%s\" for compilation\n", path.buffer);
    compile_output_t output = { 0 };

    // reading file
    char* src; 
    {
        FILE* input = fopen(path.buffer, "r");
        if(!input) {
            fprintf(stderr, "File \"%s\" not found\n", path.buffer);
            output.error = 1;
            return output;
        }

        fseek(input, 0, SEEK_END);
        long len = ftell(input);
        fseek(input, 0, SEEK_SET);

        src = mem_req(len + 1); 
        if(!fread(src, sizeof(char), len, input)) {
            fprintf(stderr, "Error when reading input file \"%s\"\n", path.buffer);
            free(src);
            output.error = 1;
            return output;
        }

        src[len] = 0;
        fclose(input);
    }

    parser_t parser;
    parser.filepath = path;

    // getting directory
    { 
        parser.diridx = 0;
        for(char* p = path.buffer; *p; p++) {
            if(*p == '\\' || *p == '/') {
                parser.diridx = (uint16_t)(p - path.buffer) + 1; 
            }
        } 
    }

    //printf("opened script \"%s\" inside dir \"%.*s\"\n", path.buffer, parser.diridx, path.buffer);

    int showed_unexpected = 0;
    token_t token; 

    scanner_new(&parser.scan, src); 

    if(!parent) {
        rootparser_t* poop = mem_req(sizeof(rootparser_t));
        // BASE TYPES ADDED ONLY ON THE SOURCE PARSER
        arena_new(&poop->types, sizeof(type_t) * 16);
        arena_new(&poop->consts, sizeof(const_t) * 16);
        arena_new(&poop->functs, sizeof(func_t) * 16);
        arena_new(&poop->typedefs, sizeof(typedef_t) * 8);
        arena_new(&poop->meta, sizeof(metainfo_t) * 8);
        arena_new(&poop->externals, sizeof(external_t) * 8);
        arena_new(&poop->other, 4096);

        type_t* type_int = arena_alloc(&poop->types, sizeof(type_t));
        type_int->prim = TYPE_INT;
        type_int->id = 0;

        type_t* type_bool = arena_alloc(&poop->types, sizeof(type_t));
        type_bool->prim = TYPE_BOOL;
        type_bool->id = 1;

        type_t* type_ptr = arena_alloc(&poop->types, sizeof(type_t));
        type_ptr->prim = TYPE_PTR;
        type_ptr->id = 2;

        type_t* type_string = arena_alloc(&poop->types, sizeof(type_t));
        type_string->prim = TYPE_STRING;
        type_string->id = 3;

        type_t* type_char = arena_alloc(&poop->types, sizeof(type_t));
        type_char->prim = TYPE_CHAR;
        type_char->id = 4;

        type_t* type_any = arena_alloc(&poop->types, sizeof(type_t));
        type_any->prim = TYPE_ANY;
        type_any->id = 5;

        type_t* type_func = arena_alloc(&poop->types, sizeof(type_t));
        type_func->prim = TYPE_FUNC;
        type_func->id = 6;
        type_func->func.types = NULL;
        type_func->func.argcount = 0;

        poop->typec = 7;
        poop->snum = 0;
        poop->pnum = 0;

        parser.root = poop;
    }
    else {
        parser.root = parent->root;
    }
    
    parser.pid = parser.root->pnum++;
    parser.hidden_consts    = parser.root->consts.length;
    parser.hidden_typedefs  = parser.root->typedefs.length;
    parser.hidden_externals = parser.root->externals.length;

    varflags_t flags = {0};
    do {
        scanner_next(&parser.scan, &token);
        
        if(token.type == TOKEN_LITERAL) {
            if(strcut_equals(token.literal, "const")) { 
                int err = parse_const(&parser, flags);
                output.error = output.error || err;
                showed_unexpected = 0;

                flags = (varflags_t){0};
            }
            else if(strcut_equals(token.literal, "external")) {
                int err = parse_extern(&parser, flags);
                output.error = output.error || err;
                showed_unexpected = 0;

                flags = (varflags_t){0};
            }
            else if(strcut_equals(token.literal, "typedef")) {
                type_t* ref = NULL;
                
                int err = parse_type(&parser, &ref);
                if(err) {
                    parser_skip_statement(&parser);
                    output.error = 1;
                    showed_unexpected = 1;
                    continue;
                }

                scanner_next(&parser.scan, &token);
                if(token.type != TOKEN_LITERAL) {
                    fprintf(stderr, "%s:%u Expected typedef name\n", path.buffer, parser.scan.line);
                    output.error = 1;
                    showed_unexpected = 1;
                    parser_skip_statement(&parser);
                    continue;
                }

                typedef_t* newdef = parser_addtypedef(&parser);
                newdef->name = parser_copycut(&parser, token.literal);
                newdef->flags = flags;
                newdef->ref = ref;

                if(flags.exposed) {
                    if(token.literal.len > 0xFF) {
                        fprintf(stderr, "%s:%u Name too long for an exposed type\n", path.buffer, parser.scan.line);
                        output.error = 1;
                        showed_unexpected = 1;
                        parser_skip_statement(&parser);
                    }
                    else {
                        metainfo_t* info = parser_addmeta(&parser);
                        info->name = newdef->name;
                        info->data.data = ref->id;
                        info->data.type = META_TYPE;
                    }
                }

                flags = (varflags_t) {0};

                scanner_next(&parser.scan, &token);
                if(token.type != TOKEN_SYMBOL || token.symbol[0] != ';') {
                    fprintf(stderr, "%s:%u Expected semicolon after typedef\n", path.buffer, parser.scan.line);
                    output.error = 1;
                    showed_unexpected = 1;
                    parser_skip_statement(&parser);
                }
                else {
                    showed_unexpected = 0;
                }
            }
            else if(strcut_equals(token.literal, "include")) {
                scanner_next(&parser.scan, &token);

                if(token.type != TOKEN_STRING || token.string.len == 0) {
                    fprintf(stderr, "%s:%u Expected file path\n", path.buffer, parser.scan.line);
                    output.error = 1;
                    showed_unexpected = 1;
                    continue;
                } 

                // building filepath 
                {
                    size_t dirlen = parser.diridx;
                    size_t inclen = token.string.len;

                    char* filepath = mem_req(dirlen + inclen + 1);
                    memcpy(filepath, parser.filepath.buffer, dirlen);
                    memcpy(filepath + dirlen, token.string.str, inclen);
                    filepath[dirlen + inclen] = 0;

                    // child parser wont generate bytecode anyways, we just need the error status
                    int err = parser_compile((heapstr_t) { filepath, dirlen + inclen }, &parser).error;
                    output.error = output.error || err;
                }

                scanner_next(&parser.scan, &token);
                if(token.type != TOKEN_SYMBOL || token.symbol[0] != ';') {
                    fprintf(stderr, "%s:%u Expected ; after include\n", path.buffer, parser.scan.line);
                    output.error = 1;
                    showed_unexpected = 1;
                } 
            }
            else if(!showed_unexpected) {
                fprintf(stderr, "%s:%u Unknown keyword \"%.*s\" \n", path.buffer, parser.scan.line, token.literal.len, token.literal.str);
                output.error = 1;
                showed_unexpected = 1;
            } 
        }
        else if(token.type == TOKEN_SYMBOL && token.symbol[0] == '@') {
            scanner_next(&parser.scan, &token);

            if(token.type != TOKEN_LITERAL) {
                fprintf(stderr, "%s:%u Expected flag after @\n", path.buffer, parser.scan.line);
                showed_unexpected = 1;
                output.error = 1; 
            }
            else {
                if(strcut_equals(token.literal, "exposed")) {
                    flags.exposed = 1;
                }
                else if(strcut_equals(token.literal, "private")) {
                    flags.private = 1;
                }
                else {
                    fprintf(stderr, "%s:%u Unknown flag \"%*.s\"\n", path.buffer, parser.scan.line, token.literal.len, token.literal.str);
                    showed_unexpected = 1;
                    output.error = 1; 
                }
            }
        }
        else if(token.type != TOKEN_EOF && !showed_unexpected) {
            fprintf(stderr, "%s:%u Unexpected ", path.buffer, parser.scan.line);
            token_print(stderr, &token);
            fputc('\n', stderr);
            showed_unexpected = 1;
            output.error = 1; 
        }
    }
    while(token.type != TOKEN_EOF);

    if(parser_isroot(&parser)) {
        if(!output.error) {
            output.bytecode = parser_genbytecode(parser.root);
        }

        rootparser_t* root = parser.root;
    
        // pretty loose but whatev
        size_t bytesused = 
            root->consts.allbytes +
            root->types.allbytes +
            root->meta.allbytes +
            root->other.allbytes + 
            root->typedefs.allbytes +
            root->externals.allbytes +
            root->functs.allbytes + 
            sizeof(rootparser_t);

        printf("** Compilation used %zu bytes, even more may be allocated.\n", bytesused);

        arena_kill(&root->consts);
        arena_kill(&root->types);
        arena_kill(&root->other);
        arena_kill(&root->meta);
        arena_kill(&root->typedefs);
        arena_kill(&root->externals);
        arena_kill(&root->functs);

        free(root);
    }

    free(src);
    free(parser.filepath.buffer);

    return output;
}