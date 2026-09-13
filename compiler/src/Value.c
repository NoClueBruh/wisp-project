#include "Parser.h"
#include "Value.h"

const_t* const_find(parser_t* parser, strcut_t name) {
    arena_iterator_t it = arena_iterator(&parser->root->consts, sizeof(const_t));
    
    const_t* current;
    uint16_t idx = 0;
    while(current = arena_next(&it)) {
        if(idx >= parser->hidden_consts && (!current->flags.private || current->pid == parser->pid) && strcut_equals(name, current->name))
            return current;

        idx++;
    }

    return NULL;
}

external_t* extern_find(parser_t* parser, strcut_t name) {
    arena_iterator_t it = arena_iterator(&parser->root->externals, sizeof(external_t));
    
    external_t* current = NULL;
    uint16_t idx = 0;
    while(current = arena_next(&it)) {
        if(idx >= parser->hidden_externals && (!current->flags.private || current->pid == parser->pid) && strcut_equals(name, current->name))
            return current;

        idx++;
    }

    return NULL;
}


type_t* type_getdup(parser_t* parser, type_t* type) {
    arena_iterator_t iterator = arena_iterator(&parser->root->types, sizeof(type_t));
    type_t* current = NULL;

    while(current = arena_next(&iterator)) {
        if(type_equals(current, type, 1))
            return current;
    }

    return NULL;
} 

typedef_t* typedef_find(parser_t* parser, strcut_t name) {
    arena_iterator_t iterator = arena_iterator(&parser->root->typedefs, sizeof(typedef_t));
 
    uint16_t idx = 0;
    for(typedef_t* def; def = arena_next(&iterator); ) { 
        if(idx >= parser->hidden_typedefs && (!def->flags.private || parser->pid == def->pid) && strcut_equals(name, def->name)) {
            return def;
        }
        idx++;
    }

    return NULL;
}

uint16_t type_getid(type_t* type) {
    // for functions, type->id is the signature id
    if(type->prim == TYPE_FUNC) {
        return TYPE_FUNC;
    }

    return type->id;
}

void type_print(FILE* fd, type_t* type) {
    switch(type->prim) {
    case TYPE_INT:
        fprintf(fd, "INT");
        break;
    case TYPE_PTR:
        fprintf(fd, "POINTER");
        break;
    case TYPE_BOOL:
        fprintf(fd, "BOOL");
        break;
    case TYPE_STRING:
        fprintf(fd, "STRING");
        break;
    case TYPE_CHAR:
        fprintf(fd, "CHAR");
        break;
    case TYPE_ANY:
        fprintf(fd, "ANY");
        break;
    case TYPE_TUPLE:
        fprintf(fd, "(");
        for(size_t i = 0; i < type->tuple.mcount; i++) {
            if(type->tuple.memb[i] == type){
                fprintf(fd, "self");
            }
            else {
                type_print(fd, type->tuple.memb[i]);
            }

            if(i + 1 != type->tuple.mcount)
                fprintf(fd, ", ");
        }
        fprintf(fd, ")");
        break;
    case TYPE_LIST:
        fprintf(fd, "LIST(");
        type_print(fd, type->list.memb);
        fprintf(fd, ")");
        break;
    case TYPE_ARRAY:
        fprintf(fd, "ARRAY(");
        type_print(fd, type->array.memb);
        fprintf(fd, ")");
        break;
    case TYPE_FUNC:
        if(type->func.argcount == 0) {
            fprintf(fd, "F. VOID");  
        }
        else {
            fprintf(fd, "F. ");  
            for(size_t i = 0; i < type->func.argcount; i++) {
                if(i) {
                    fprintf(fd, ", ");
                }
                type_print(fd, type->func.types[i]);
            }
        }
        fprintf(fd, ": "); 
        type_print(fd, type->func.types[type->func.argcount]);
        break;
    case TYPE_STRUCT: 
        fprintf(fd, "STRUCT (id=%d)", type->id);
        break;
    case TYPE_ENUM:
        fprintf(fd, "ENUM (id=%d)", type->id);
        break;
    default:
        fprintf(stderr, "You forgot to define print for a type!!\n");
        exit(1);
    }
}

int type_isobj(type_t* t1) {
    switch(t1->prim) {
    case TYPE_TUPLE:
    case TYPE_STRUCT:
    case TYPE_ARRAY:
    case TYPE_STRING:
    case TYPE_ANY:
    case TYPE_LIST:
        return 1;
    }
    return 0;
}

int type_iscomb(type_t* t1, type_t* t2, primitive_t p1, primitive_t p2) {
    return (t1->prim == p1 && t2->prim == p2) || (t2->prim == p1 && t1->prim == p2);
}
int type_equivalent(type_t* t1, type_t* t2) {
    return type_equals(t1, t2, 0) || type_interchangable(t1, t2);
}

int type_interchangable(type_t* t1, type_t* t2) {
    if(!t1 || !t2) {
        return 0;
    }
    
    if(type_iscomb(t1, t2, TYPE_CHAR, TYPE_INT)) {
        return 1;
    }
    
    if(type_iscomb(t1, t2, TYPE_INT, TYPE_ENUM)) {
        return 1;
    }

    if(t1->prim == TYPE_STRUCT) {
        return type_equivalent(t1->structt.tuptype, t2);
    }
    
    if(t2->prim == TYPE_STRUCT) {
        return type_equivalent(t2->structt.tuptype, t1);
    } 

    return 0;
}

int type_equals(type_t* t1, type_t* t2, int exactly) {
    if(t1 == t2) {
        return 1;
    }

    if(!t1 || !t2) {
        return 0;
    }

    if(t1->prim != t2->prim) {
        return 0;
    }

    switch(t1->prim) {
    case TYPE_BOOL:
    case TYPE_INT:
    case TYPE_PTR:
    case TYPE_STRING:
    case TYPE_CHAR:
    case TYPE_ANY:
        return 1;
    case TYPE_ENUM:
        if(t1->enumm.entry_count != t2->enumm.entry_count) {
            return 0;
        }

        for(size_t i = 0; i < t1->enumm.entry_count; i++) {
            if(t1->enumm.entries[i].value != t2->enumm.entries[i].value || strcmp(t1->enumm.entries[i].name, t2->enumm.entries[i].name)) {
                return 0;
            }
        }
        return 1;
    case TYPE_TUPLE:
        if(t1->tuple.mcount != t2->tuple.mcount)
            return 0;
        
        if(exactly) {
            for(size_t i = 0; i < t1->tuple.mcount; i++) {
                if(!type_equals(t1->tuple.memb[i], t2->tuple.memb[i], 1)) {
                    return 0;
                }
            }
        }
        else {
            for(size_t i = 0; i < t1->tuple.mcount; i++) {
                if(!type_equivalent(t1->tuple.memb[i], t2->tuple.memb[i])) {
                    return 0;
                }
            }
        }

        return 1;
    case TYPE_LIST:
        return exactly ? type_equals(t1->list.memb, t2->list.memb, 1) : type_equivalent(t1->list.memb, t2->list.memb);
    case TYPE_ARRAY:
        return exactly ? type_equals(t1->array.memb, t2->array.memb, 1) : type_equivalent(t1->array.memb, t2->array.memb);
    case TYPE_STRUCT: {
        if(exactly) {
            if(!type_equals(t1->structt.tuptype, t2->structt.tuptype, 1)) {
                return 0;
            }
        }
        else {
            if(!type_equivalent(t1->structt.tuptype, t2->structt.tuptype)) {
                return 0;
            }
        }

        for(size_t i = 0; i < t1->structt.tuptype->tuple.mcount; i++) {
            if(strcmp(t1->structt.names[i], t2->structt.names[i])) {
                return 0;
            }
        }

        return 1;
    }
    case TYPE_FUNC:
        if(t1->func.argcount != t2->func.argcount) {
            return 0;
        }

        if(!t1->func.types || !t2->func.types) {
            return 0;
        }

        /*
        if(exactly) {
            for(size_t i = 0; i <= t1->func.argcount; i++) { 
                if(!type_equals(t1->func.types[i], t2->func.types[i], 1)) {
                    return 0;
                } 
            }
        }
        */

        for(size_t i = 0; i <= t1->func.argcount; i++) { 
            if(!type_equivalent(t1->func.types[i], t2->func.types[i])) {
                return 0;
            } 
        }
        return 1;
    default: 
        fprintf(stderr, "You forgot to define equality for a type!!\n");
        exit(1);
    }
}