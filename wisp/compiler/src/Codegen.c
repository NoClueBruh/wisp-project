#include "Parser.h"
#include "Value.h"
#include "Expr.h"
 
static int emit_type(type_t* type, string_t* bc) {
    if(type->prim == TYPE_STRUCT || type->prim == TYPE_ENUM)
        return 0;

    // we keep the only "primitve" function type
    if(type->prim == TYPE_FUNC) {
        if(type->func.types) {
            return 0;
        }
    }

    string_push(bc, type->prim);
    switch(type->prim) {
    case TYPE_INT:
    case TYPE_BOOL:
    case TYPE_PTR:
    case TYPE_STRING:
    case TYPE_CHAR:
    case TYPE_ANY: 
    case TYPE_FUNC:
        break;
    case TYPE_TUPLE:
        string_push(bc, type->tuple.mcount);

        for(size_t i = 0; i < type->tuple.mcount; i++) {
            type_t* member = type->tuple.memb[i];
            string_write16(bc, member->id);
        }
        break;
    case TYPE_LIST:
        string_write16(bc, type->list.memb->id);
        break;
    case TYPE_ARRAY:
        string_write16(bc, type->array.memb->id);
        break; 
    default:
        fprintf(stderr, "You forgot to add type emit\n");
        exit(1);
    }

    return 1;
}

heapbuf_t parser_genbytecode(rootparser_t* root) { 
    printf("** Generating bytecode..\n");
    
    string_t bytecode;
    string_new(&bytecode, 4096);

    uint32_t fndata = 0;
    string_write32(&bytecode, 0);

    // metadata
    {
        arena_iterator_t it = arena_iterator(&root->meta, sizeof(metainfo_t));

        for(metainfo_t* current; (current = arena_next(&it)); ) {
            size_t wpos = bytecode.length;
            size_t len  = 0;

            string_push(&bytecode, 0);
            for(char c; c = current->name[len]; len++) {
                string_push(&bytecode, c);
            }
            bytecode.buffer[wpos] = len;
            
            string_push(&bytecode, current->data.type);
            string_write16(&bytecode, current->data.data);
        }

        string_push(&bytecode, 0); 
        string_write32_at(&bytecode, 0, bytecode.length);
    }

    // externals 
    {
        arena_iterator_t it = arena_iterator(&root->externals, sizeof(external_t));

        string_write16(&bytecode, root->externals.length);
        for(external_t* current; (current = arena_next(&it)); ) {
            size_t wpos = bytecode.length;
            size_t len  = 0;
            
            string_push(&bytecode, 0);
            for(char c; c = current->name[len]; len++) {
                string_push(&bytecode, c);
            }
            bytecode.buffer[wpos] = len;
        }
        fndata = bytecode.length;
    }

    // function fwd pointers
    {   
        arena_iterator_t it = arena_iterator(&root->functs, sizeof(func_t));

        // if you change this, make sure to adjust the function section as well
        string_write16(&bytecode, root->functs.length);
        for(size_t i = 0; i < root->functs.length; i++) {
            func_t* current = arena_next(&it);

            string_write32(&bytecode, 0);
            string_write16(&bytecode, current->type->id);
            string_push(&bytecode, current->max_vars);
        }
    } 

    // types
    {
        arena_iterator_t it = arena_iterator(&root->types, sizeof(type_t));

        uint16_t count = 0;
        uint32_t cpos  = bytecode.length;
        string_write16(&bytecode, 0);

        for(type_t* current; (current = arena_next(&it)); ) {
            count += emit_type(current, &bytecode);
        } 

        string_write16_at(&bytecode, cpos, count);
    }

    // code 
    {
        arena_iterator_t it = arena_iterator(&root->consts, sizeof(const_t));

        for(const_t* current; (current = arena_next(&it));) {
            expr_emit_node(current->expr.root, 0, &bytecode, NULL);
            string_push(&bytecode, OP_TEST);
        }
    }

    string_push(&bytecode, OP_HALT);
  
    // functions 
    {
        arena_iterator_t it = arena_iterator(&root->functs, sizeof(func_t));

        uint32_t offset = fndata + 2;
        for(func_t* current; (current = arena_next(&it)); ) {
            string_write32_at(&bytecode, offset, bytecode.length);
            offset += 7;

            for(local_var_t* v = current->localvar; v; v = v->next) {
                string_push(&bytecode, OP_FN_ADDLOCAL);
            }
            
            expr_emit_node(current->expr.root, 1, &bytecode, current); 
            string_push(&bytecode, OP_RET);
        }
    }  
    
    return (heapbuf_t) {
        .buffer = bytecode.buffer,
        .len    = bytecode.length,
    };
}