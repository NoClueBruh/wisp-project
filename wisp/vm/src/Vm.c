#define THIS_IS_VM
#include "Vm.h" 
#include "Utils.h"

void vm_panic(vm_t* vm, char* msg) {
    if(vm->status == VM_ERROR) {
        return;
    }

    vm->status = VM_ERROR;
    if(vm->onpanic) {
        vm->onpanic(vm, msg);
    }
    else {
        fputs(msg, stderr);
        fputc('\n', stderr);
        exit(1);
    }
}

uint32_t vm_get32(vm_t* vm) {
    if(vm->pos + 4 >= vm->prog + vm->size) {
        vm_panic(vm, "[ VM ] Expected 4 bytes");
        return 0;
    }
 
    uint32_t val = ((uint32_t)vm->pos[0] << 24) | ((uint32_t)vm->pos[1] << 16) | ((uint32_t)vm->pos[2] << 8) | vm->pos[3];
    vm->pos += 4;
    return val;
}

uint32_t vm_get16(vm_t* vm) {
    if(vm->pos + 2 >= vm->prog + vm->size) {
        vm_panic(vm, "[ VM ] Expected 2 bytes");
        return 0;
    }

    uint16_t val = ((uint16_t)vm->pos[0] << 8) | vm->pos[1];
    vm->pos += 2;
    return val; 
} 

uint32_t vm_get8(vm_t* vm) {
    if(vm->pos + 1 >= vm->prog + vm->size) {
        vm_panic(vm, "[ VM ] Expected byte");
        return 0;
    }
  
    return *(vm->pos++);
}

void vm_getbuf(vm_t* vm, void* dest, size_t len) {
    if(vm->pos + len >= vm->prog + vm->size) {
        vm_panic(vm, "[ VM ] Expected buffer");
        return;
    }

    if(dest) {
        memcpy(dest, vm->pos, len);
    }
    vm->pos += len;
}

metadata_t vm_getmeta(vm_t* vm, char* name, size_t namelen) {
    metadata_t meta = (metadata_t) {0};
    if(namelen > 0xFF) {
        return meta;
    }

    char* lastpos = vm->pos;
    vm->pos = vm->prog + 4;

    for(;;) {
        uint8_t len = vm_get8(vm);

        if(len == 0) {
            break;
        }

        void* metaname = vm->pos;
        vm_getbuf(vm, NULL, len);

        uint8_t type  = vm_get8(vm);
        uint16_t data = vm_get16(vm);

        if(len == namelen && strncmp(name, metaname, namelen) == 0) {
            meta.type = type;
            meta.data = data;
            break;
        }
    }

    vm->pos = lastpos;
    return meta;
}

///
static void vm_cleanup(void*); 

void vm_load(vm_t* vm, char* bytecode, size_t size) {
    vm->prog = bytecode;
    vm->pos = vm->prog;
    vm->size = size; 
    vm->status = VM_FROZEN; 
 
    inpvec_new(&vm->callstack, sizeof(activeframe_t), 16);
    inpvec_new(&vm->stack, sizeof(stack_entry_t), 16); 

    gc_init(&vm->gc, vm_cleanup, vm);
    arena_new(&vm->data, 256);

    // skip metadata
    vm->pos = vm->prog + vm_get32(vm);

    // externals
    {
        size_t externals = vm_get16(vm);  
        vm->externals = externals ? mem_req(sizeof(void*) * externals) : NULL;

        for(size_t i = 0; i < externals; i++) {
            uint8_t len = vm_get8(vm); 

            void* name = vm->pos;
            vm_getbuf(vm, NULL, len);

            if(!vm->getextern) {
                vm->externals[i] = NULL;
            }
            else {
                vm->externals[i] = vm->getextern(name, len);
            }
        }
    }

    // read function data
    {
        vm->func_count = vm_get16(vm);
        vm->funcs = vm->func_count ? mem_req(sizeof(fndata_t) * vm->func_count) : NULL;

        for(size_t i = 0; i < vm->func_count; i++) {
            fndata_t* p = vm->funcs + i;
            p->ptr  = vm_get32(vm);
            p->sid  = vm_get16(vm);
            p->vars = vm_get8(vm);
        }
    }

    // read types
    {
        uint16_t count = vm_get16(vm);

        vm->typecount = count;
        vm->types = count ? mem_req(sizeof(type_t) * count) : NULL;

        for(size_t i = 0; i < count; i++) {
            type_t* type = vm->types + i;

            switch(type->prim = vm_get8(vm)) {
            case TYPE_BOOL:
            case TYPE_INT: 
            case TYPE_PTR:
            case TYPE_STRING:
            case TYPE_CHAR:
            case TYPE_ANY:
            case TYPE_FUNC:
                break;
            case TYPE_TUPLE: {
                uint8_t memc = vm_get8(vm);

                type->tuple.mcount = memc;
                type->tuple.memb = arena_alloc(&vm->data, sizeof(uint16_t) * memc);
                for(size_t j = 0; j < memc; j++) {
                    uint16_t id = vm_get16(vm);
                    type->tuple.memb[j] = id;
                }
                break;
            }
            case TYPE_LIST: {
                type->list.memb = vm_get16(vm);
                break;
            }
            case TYPE_ARRAY: {
                type->array.memb = vm_get16(vm);
                break;
            } 
            default:
                printf("prim is %d\n", type->prim);
                vm_panic(vm, "[ VM ] forgot to parse type");
                return;
            }
        }
    }
}

// a type is small if it's value can fit in an integer (for example int, bool, char etc)
// all other types fit in a pointer (as they are pointers anyway) except functions.
int type_small(uint16_t typeid) {
    switch(typeid) {
    case TYPE_INT:
    case TYPE_BOOL:
    case TYPE_CHAR:
        return 1;
    }
    return 0;
}

void vm_printv(vm_t* vm, uint16_t typeid, stack_value_t val) {
    type_t* type = vm->types + typeid;
 
    switch(type->prim) {
    case TYPE_INT:
        printf("%d", val.integer);
        break;
    case TYPE_BOOL:
        printf("%s", val.boolean ? "true" : "false");
        break;
    case TYPE_PTR:
        printf("%p", val.ptr);
        break;
    case TYPE_CHAR:
        printf("'%c'", val.character);
        break;
    case TYPE_ANY: {
        printf("(any) ");
        if(type_small(val.any.typeid)) {
            vm_printv(vm, val.any.typeid, (stack_value_t) {.integer = val.any.integer});
        }
        else if(val.any.typeid == TYPE_FUNC) {
            vm_printv(vm, val.any.typeid, (stack_value_t) {.fnval = *val.any.fnval});
        }
        else { 
            vm_printv(vm, val.any.typeid, (stack_value_t) {.ptr = val.any.ptr});
        }
        break;
    }
    case TYPE_TUPLE:
        if(val.ptr == NULL) {
            printf("nil");
            break;
        }

        putchar('(');
        for(size_t i = 0; i < type->tuple.mcount; i++) {
            if(i != 0) {
                putchar(',');
            }
            vm_printv(vm, type->tuple.memb[i], ((stack_value_t*)val.ptr)[i]);
        }
        putchar(')');
        break;
    case TYPE_LIST: {
        for(list_node_t* node = val.ptr; node; node = node->next) {
            putchar('(');
            vm_printv(vm, vm->types[typeid].list.memb, node->value);
            printf(") -> ");
        }
        break;
    }
    case TYPE_STRING: {
        if(!val.ptr) {
            printf("(string) nil");
            break;
        } 

        arrstr_t* str = val.ptr;
        printf("\"%s\"", str->characters);
        break;
    }
    case TYPE_ARRAY: {
        array_t* array = val.ptr;
        if(!array) {
            printf("(array) nil");
            break;
        }

        putchar('[');
        for(size_t i = 0; i < array->length; i++) {
            if(i > 0) {
                putchar(',');
            }

            vm_printv(vm, vm->types[typeid].array.memb, ((stack_value_t*)array->elements)[i]);
        }
        putchar(']');
        break;
    }
    case TYPE_FUNC: {
        printf("FUNC(id=%u, sig=%d)", val.fnval.fnid, vm->funcs[val.fnval.fnid].sid);
        break;
    }
    default:
        vm_panic(vm, "[ VM ] Cannot print value");
    }
}

void vm_secure_stack(vm_t* vm, size_t count) {
    if(count > vm->stack.len) {
        vm_panic(vm, "[ VM ] Stack underflow");
    } 
}

void vm_secure_callstack(vm_t* vm, size_t count) {
    if(count > vm->callstack.len) {
        vm_panic(vm, "[ VM ] Callstack underflow");
    }
}

void vm_exec(vm_t* vm) {
    char inst = *(vm->pos++); 
    
    switch(inst) {
    case OP_PUSH_BOOL:
        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {.value.boolean = vm_get8(vm), .typeid = TYPE_BOOL};
        break;
    case OP_PUSH8:
        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {.value.integer = vm_get8(vm), .typeid = TYPE_INT};
        break;  
    case OP_PUSH_CHAR:
        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {.value.integer = vm_get8(vm), .typeid = TYPE_CHAR};
        break;  
    case OP_PUSH16:
        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {.value.integer = vm_get16(vm), .typeid = TYPE_INT};
        break;  
    case OP_PUSH32:
        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {.value.integer = vm_get32(vm), .typeid = TYPE_INT};
        break;  
    case OP_PUSH_NULL:
        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {.value.any = {.ptr = NULL, .typeid = TYPE_PTR}, .typeid = TYPE_ANY};
        break;
    case OP_ADD:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.integer = A->value.integer + B->value.integer;
        }
        vm->stack.len--;
        break;
    case OP_SUB:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.integer = A->value.integer - B->value.integer;
        }
        vm->stack.len--;
        break;
    case OP_MUL:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.integer = A->value.integer * B->value.integer;
        }
        vm->stack.len--;
        break;
    case OP_DIV:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.integer = A->value.integer / B->value.integer;
        }
        vm->stack.len--;
        break;
    case OP_SML:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.boolean = A->value.integer < B->value.integer;
            B->typeid = TYPE_BOOL;
        }
        vm->stack.len--;
        break;
    case OP_SME:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.boolean = A->value.integer <= B->value.integer;
            B->typeid = TYPE_BOOL;
        }
        vm->stack.len--;
        break;
    case OP_GRT:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.boolean = A->value.integer > B->value.integer;
            B->typeid = TYPE_BOOL;
        }
        vm->stack.len--;
        break;
    case OP_GRE:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.boolean = A->value.integer >= B->value.integer;
            B->typeid = TYPE_BOOL;
        }
        vm->stack.len--;
        break;
    case OP_EQL:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.boolean = A->value.integer == B->value.integer;
            B->typeid = TYPE_BOOL;
        }
        vm->stack.len--;
        break;
    case OP_NQL:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);
            B->value.boolean = A->value.integer != B->value.integer;
            B->typeid = TYPE_BOOL;
        }
        vm->stack.len--;
        break;
    case OP_TEST:
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
            
            printf("Stack top has value ");
            vm_printv(vm, top->typeid, top->value);
            putchar('\n');
        }
        break;
    case OP_POP:
        vm_secure_stack(vm, 1);
        vm->stack.len--;
        break;
    case OP_VGET: 
        {
            uint16_t v = vm_get16(vm);
            vm_secure_stack(vm, 1 + v);

            stack_entry_t* entry = inpvec_at(&vm->stack, v);
            inpvec_push(&vm->stack, entry);
        }
        break; 
    case OP_NEG:
        vm_secure_stack(vm, 1);
        { 
            stack_entry_t* V = inpvec_at(&vm->stack, vm->stack.len - 1);
            V->value.integer = -V->value.integer;
        }
        break;
    case OP_NOT:
        vm_secure_stack(vm, 1);
        { 
            stack_entry_t* V = inpvec_at(&vm->stack, vm->stack.len - 1);
            V->value.boolean = !V->value.boolean;
        }
        break;
    case OP_TUPLE_CREATE: {
        uint16_t typeid = vm_get16(vm);
        
        type_t* type = vm->types + typeid;
        uint8_t membc = type->tuple.mcount;

        vm_secure_stack(vm, membc);

        stack_value_t* p = gc_alloc(&vm->gc, sizeof(stack_value_t) * membc);
        {
            for(size_t i = 0; i < membc; i++) {
                stack_entry_t* yo = inpvec_at(&vm->stack, vm->stack.len - 1 - i);
                p[i] = yo->value;
            }

            vm->stack.len -= membc;

            stack_entry_t tup = {
                .typeid = typeid,
                .value.ptr = p
            };
            inpvec_push(&vm->stack, &tup);
        }
        gc_empty_temp(&vm->gc);

        break;
    }
    case OP_TUPLE_GET: {
        uint8_t idx = vm_get8(vm);
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
            
            if(!top->value.ptr) {
                vm_panic(vm, "[ VM ] Cannot get element of null");
                return;
            }
            
            top->typeid = vm->types[top->typeid].tuple.memb[idx];
            top->value  = ((stack_value_t*)top->value.ptr)[idx];
        }
        break;
    }
    case OP_STRING_CREATE: {
        uint32_t len = vm_get32(vm);
 
        arrstr_t* str = gc_alloc(&vm->gc, sizeof(arrstr_t) + len + 1);
        str->length = len;

        vm_getbuf(vm, str->characters, len);
        str->characters[len] = 0;

        *(stack_entry_t*)inpvec_app(&vm->stack) = (stack_entry_t) {
            .typeid = TYPE_STRING,
            .value.ptr = str
        };
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_STRING_CONCAT: {
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* left = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* right = inpvec_at(&vm->stack, vm->stack.len - 2);
            
            arrstr_t* string_left  = left->value.ptr;
            arrstr_t* string_right = right->value.ptr; 

            if(!string_right) {
                right->value.ptr = string_left;
                right->typeid = TYPE_STRING; 
            }
            else if(string_left) {
                arrstr_t* str = gc_alloc(&vm->gc, sizeof(arrstr_t) + string_left->length + 1 + string_right->length);
                str->length = string_left->length + string_right->length;
                str->characters[str->length] = 0;

                memcpy(str->characters, string_left->characters, string_left->length);
                memcpy(str->characters + string_left->length, string_right->characters, string_right->length);

                right->typeid = TYPE_STRING;
                right->value.ptr = str;
 
            }
        }
        vm->stack.len--;
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_STRING_SLICE: {
        vm_secure_stack(vm, 3);
        {
            stack_entry_t* from     = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* to       = inpvec_at(&vm->stack, vm->stack.len - 2);
            stack_entry_t* string   = inpvec_at(&vm->stack, vm->stack.len - 3);

            arrstr_t* str = string->value.ptr;
            if(str && to->value.integer >= 0 && to->value.integer <= str->length && from->value.integer >= 0 && from->value.integer <= str->length && to->value.integer < from->value.integer) {
                size_t size = from->value.integer - to->value.integer;

                arrstr_t* newstr = gc_alloc(&vm->gc, sizeof(arrstr_t) + size + 1);
                newstr->length = size;
                newstr->characters[size] = 0;  
                memcpy(newstr->characters, str->characters + to->value.integer, size);

                string->value.ptr = newstr; 
            }
            else {
                string->value.ptr = NULL;
            }
        }
        vm->stack.len -= 2;
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_STRING_GET: {
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* ind = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* arr = inpvec_at(&vm->stack, vm->stack.len - 2);
            
            arrstr_t* str = arr->value.ptr;

            if(ind->value.integer < 0) {
                vm_panic(vm, "[ VM ] Array index cannot be negative");
                return;
            }

            if(ind->value.integer >= str->length) {
                vm_panic(vm, "[ VM ] Array index out of bounds");
                return;
            }

            arr->typeid = TYPE_CHAR;
            arr->value.integer = str->characters[ind->value.integer];
        }
        vm->stack.len--;
        break;
    }
    case OP_VALUE_PROPERTY: {
        uint8_t vp = vm_get8(vm);
        
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);

            switch(vp) {
            case VP_ARRAY_LEN:
                top->typeid = TYPE_INT;
                top->value.integer = top->value.ptr ? ((array_t*)top->value.ptr)->length : 0;
                break;
            case VP_STRING_LEN:
                top->typeid = TYPE_INT;
                top->value.integer = top->value.ptr ? ((arrstr_t*)top->value.ptr)->length : 0;
                break;
            default:
                vm_panic(vm, "[ VM ] Unknown property thing");
                return;
            }
        }
        break;
    }
    case OP_ARRAY_GET: {
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* ind = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* arr = inpvec_at(&vm->stack, vm->stack.len - 2);
            
            array_t* array = arr->value.ptr;

            if(ind->value.integer < 0) {
                vm_panic(vm, "[ VM ] Array index cannot be negative");
                return;
            }

            if(ind->value.integer >= array->length) {
                vm_panic(vm, "[ VM ] Array index out of bounds");
                return;
            }

            arr->typeid = vm->types[arr->typeid].array.memb;
            arr->value  = ((stack_value_t*)array->elements)[ind->value.integer];
        }
        vm->stack.len--;
        break;
    }
    case OP_ARRAY_SLICE: {
        vm_secure_stack(vm, 3);
        {
            stack_entry_t* from     = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* to       = inpvec_at(&vm->stack, vm->stack.len - 2);
            stack_entry_t* array    = inpvec_at(&vm->stack, vm->stack.len - 3);
 
            array_t* arr = array->value.ptr;
            if(arr && to->value.integer >= 0 && to->value.integer <= arr->length && from->value.integer >= 0 && from->value.integer <= arr->length && to->value.integer < from->value.integer) {
                size_t size = from->value.integer - to->value.integer;

                array_t* newarr = gc_alloc(&vm->gc, sizeof(array_t) + size * sizeof(stack_value_t));
                newarr->length = size;
                memcpy(newarr->elements, arr->elements + to->value.integer, size* sizeof(stack_value_t));

                array->value.ptr = newarr;
            }
            else {
                array->value.ptr = NULL;
            }
        }
        vm->stack.len -= 2;
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_ARRAY_CONCAT: { 
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* left = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* right = inpvec_at(&vm->stack, vm->stack.len - 2);
            
            array_t* array_left  = left->value.ptr;
            array_t* array_right = right->value.ptr;

            if(!array_right) {
                right->value.ptr = array_left;
                right->typeid = left->typeid;
            }
            else if(array_left){
                array_t* array = gc_alloc(&vm->gc, sizeof(array_t) + sizeof(stack_value_t) * (array_left->length + array_right->length));
                array->length = array_left->length + array_right->length;

                memcpy(array->elements, array_left->elements, sizeof(stack_value_t) * array_left->length);
                memcpy(array->elements + array_left->length, array_right->elements, sizeof(stack_value_t) * array_right->length);

                right->typeid = left->typeid;
                right->value.ptr = array;
            }
        }
        vm->stack.len--;
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_ARRAY_CREATE: {
        uint32_t membc = vm_get32(vm);
        uint16_t typeid = vm_get16(vm);

        vm_secure_stack(vm, membc);
        {
            array_t* array = gc_alloc(&vm->gc, sizeof(array_t) + sizeof(stack_value_t) * membc);
            array->length = membc;
            
            for(size_t i = 0; i < membc; i++) {
                stack_entry_t* data = inpvec_at(&vm->stack, vm->stack.len - i - 1);
                array->elements[i] = data->value;
            }

            vm->stack.len -= membc;

            stack_entry_t arr = {
                .typeid = typeid,
                .value.ptr = array
            };
            inpvec_push(&vm->stack, &arr);
        }

        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_LIST_CREATE: {
        uint32_t membc = vm_get32(vm);
        uint16_t typeid = vm_get16(vm);

        vm_secure_stack(vm, membc);
        {
            list_node_t* head = NULL;

            for(size_t i = 0; i < membc; i++) {
                list_node_t* node = gc_alloc(&vm->gc, sizeof(list_node_t));

                stack_entry_t* data = inpvec_at(&vm->stack, vm->stack.len - membc + i);
                node->value = data->value;
                node->next = head;
                head = node;
            }
            vm->stack.len -= membc;

            stack_entry_t list = {
                .typeid = typeid,
                .value.ptr = head
            };
            inpvec_push(&vm->stack, &list);
        }
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_LIST_GET:
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
            list_node_t* node = top->value.ptr;
            
            if(!node) {
                vm_panic(vm, "[ VM ] Cannot get first element of an empty list");
                return;
            }

            top->value = node->value;
            top->typeid = vm->types[top->typeid].list.memb;
        }
        break;
    case OP_LIST_TAIL:
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
            list_node_t* node = top->value.ptr;

            if(!node) {
                vm_panic(vm, "[ VM ] Cannot get tail of an empty list");
                return;
            }

            top->value.ptr = node->next;
        }
        break;
    case OP_LIST_INSERT:
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* A = inpvec_at(&vm->stack, vm->stack.len - 1);
            stack_entry_t* B = inpvec_at(&vm->stack, vm->stack.len - 2);

            list_node_t* node = gc_alloc(&vm->gc, sizeof(list_node_t));
            node->value = A->value;
            node->next  = B->value.ptr;

            B->value.ptr = node;
        }
        gc_empty_temp(&vm->gc);
        vm->stack.len--;
        break;
    case OP_TYPEOF: {
        vm_secure_stack(vm, 1);
        uint16_t typeid = vm_get16(vm);

        stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);

        if(typeid == TYPE_FUNC) {
            uint16_t sig = vm_get16(vm);
 
            if(top->typeid == TYPE_ANY) {
                any_t any = top->value.any;
                top->value.boolean = any.typeid == TYPE_FUNC && vm->funcs[any.fnval->fnid].sid == sig;
            }
            else {
                top->value.boolean = (top->typeid == TYPE_FUNC) && vm->funcs[top->value.fnval.fnid].sid == sig;
            }
        }
        else if(top->typeid == TYPE_ANY) {
            any_t any = top->value.any;
            top->value.boolean = any.typeid == typeid;
        }
        else {
            top->value.boolean = top->typeid == typeid;
        }

        top->typeid = TYPE_BOOL;
        break;
    }
    case OP_CAST: {
        vm_secure_stack(vm, 1);

        uint8_t castinst = vm_get8(vm);
        uint16_t typeid = vm_get16(vm);

        stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);

        switch(castinst) {
        case CAST_CHAR_STRING:  {
            arrstr_t* str = gc_alloc(&vm->gc, sizeof(arrstr_t) + 2);
            str->length = 1;
            str->characters[0] = top->value.character;
            str->characters[1] = 0;

            top->value.ptr = str;
            top->typeid = TYPE_STRING;
            break;
        }
        case CAST_TO_ANY:{    
            if(top->typeid == TYPE_FUNC) {
                fnval_t* val = gc_alloc(&vm->gc, sizeof(fnval_t));
                *val = top->value.fnval;

                top->value.any.fnval = val;
            }
            else if(type_small(top->typeid)) {
                top->value.any.integer = top->value.integer;
            }
            else {
                top->value.any.ptr = top->value.ptr;
            }

            top->value.any.typeid = top->typeid;
            top->typeid = TYPE_ANY;
            break;
        }
        case CAST_ANY_FUNC: {
            any_t any = top->value.any;

            if(any.typeid != TYPE_FUNC) {
                vm_panic(vm, "[ VM ] Invalid cast of any to func, any does not contain function");
                return;
            }

            if(vm->funcs[any.fnval->fnid].sid != typeid) {
                printf("%d vs %d\n", vm->funcs[any.fnval->fnid].sid, typeid);
                vm_panic(vm, "[ VM ] Invalid cast of any to func, wrong function signature");
                return;
            }
            
            top->typeid = TYPE_FUNC;
            top->value.fnval = *any.fnval;
            break;
        }
        case CAST_FROM_ANY: {
            any_t any = top->value.any;
            
            // allow a (any) ptr type to become a object
            // mainly here for NULL
            if(any.typeid == TYPE_PTR && !type_small(typeid)) {
                top->value.ptr = any.ptr;
            }
            else { 
                if(any.typeid != typeid) { 
                    vm_panic(vm, "[ VM ] Invalid cast of any to value, wrong type");
                    return;
                }

                if(type_small(any.typeid)) {
                    top->value.integer = any.integer;
                }
                else {
                    top->value.ptr = any.ptr;
                }
            }
            top->typeid = typeid;
            break;
        }
        default: {
            top->typeid = typeid;
            break;
        }
        }

        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_NOT_NULL:
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
            if(top->typeid == TYPE_ANY) {
                top->value.boolean = top->value.any.ptr != NULL;
            }
            else {
                top->value.boolean = top->value.ptr != NULL;
            }
            top->typeid = TYPE_BOOL;
        }
        break;
    case OP_JT: {
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
            
            uint32_t jmp = vm_get32(vm);
            if(top->value.boolean) {
                vm->pos = vm->prog + jmp;
            }
        }
        vm->stack.len--;
        break;
    }
    case OP_JF: {
        vm_secure_stack(vm, 1);
        {
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
        
            uint32_t jmp = vm_get32(vm);
            if(!top->value.boolean) {
                vm->pos = vm->prog + jmp;
            }
        }
        vm->stack.len--;
        break;
    }
    case OP_HOP: {
        vm_secure_stack(vm, 1);
        {
            int t = vm_get8(vm);
            stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
        
            uint32_t jmp = vm_get32(vm);
            if((top->value.boolean && 1) == t) {
                vm->pos = vm->prog + jmp;
            }
            else {
                vm->stack.len--;
            }
        }
        break;
    }
    case OP_JMP: {
        uint32_t jmp = vm_get32(vm);
        vm->pos = vm->prog + jmp;
        break;
    }
    case OP_FNGET: {
        uint16_t id = vm_get16(vm);

        fnframe_t* parent = NULL;
        if(vm->callstack.len) {
            parent = ((activeframe_t*)inpvec_at(&vm->callstack, vm->callstack.len - 1))->frame;
        }

        *((stack_entry_t*)inpvec_app(&vm->stack)) = (stack_entry_t) {
            .value.fnval = {
                .fnid = id,
                .parent = parent
            }, 
            .typeid = TYPE_FUNC
        };  
        break;
    }
    case OP_FN_TAILCALL: {
        vm_secure_stack(vm, 1);
        vm_secure_callstack(vm, 1);
        {
            stack_entry_t* L = inpvec_at(&vm->stack, vm->stack.len - 1); 
            vm->stack.len--;     
            
            uint16_t fnid = L->value.fnval.fnid;

            activeframe_t* top = inpvec_at(&vm->callstack, vm->callstack.len - 1); 
            int recycle = top->frame->fn == fnid;
            
            if(!recycle) { 
                for(fnframe_t* frame = L->value.fnval.parent; !recycle && frame; frame = frame->parent) {
                    if(top->frame == frame) {
                        recycle = 1;
                    }
                }
            }

            fnframe_t* fuh = NULL;
            if(recycle){ 
                uint8_t oldvc = vm->funcs[top->frame->fn].vars;
                uint8_t newvc = vm->funcs[fnid].vars;

                if(newvc != oldvc) {
                    gc_freeptr(&vm->gc, top->frame);
                    
                    fuh = gc_alloc(&vm->gc, sizeof(fnframe_t) + newvc * sizeof(stack_entry_t));
                    fuh->prevalloc = NULL;
                    gc_setprev(fuh);
                }
                else {
                    fuh = top->frame;
                }
            }
            else {
                fuh = gc_alloc(&vm->gc, sizeof(fnframe_t) + vm->funcs[fnid].vars * sizeof(stack_entry_t));
                fuh->prevalloc = NULL;
                gc_setprev(fuh); 
            }
  
            fuh->fn = fnid;
            fuh->parent = L->value.fnval.parent;
            fuh->vars = 0; 

            top->frame = fuh;
            vm->pos = vm->prog + vm->funcs[fnid].ptr; 
        } 
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_FNCALL: {
        vm_secure_stack(vm, 2);
        {
            stack_entry_t* L = inpvec_at(&vm->stack, vm->stack.len - 1);
            
            uint16_t fnid = L->value.fnval.fnid;
  
            fnframe_t* fuh = gc_alloc(&vm->gc, sizeof(fnframe_t) + vm->funcs[fnid].vars * sizeof(stack_entry_t));
            gc_setprev(fuh);

            fuh->prevalloc = NULL;
            fuh->fn = fnid;
            fuh->parent = L->value.fnval.parent;
            fuh->vars = 0; 

            *((activeframe_t*)inpvec_app(&vm->callstack)) = (activeframe_t) {
                .frame = fuh,
                .ra = vm->pos - vm->prog,
            };
            
            vm->pos = vm->prog + vm->funcs[fnid].ptr;
        }     
        vm->stack.len--;
        gc_empty_temp(&vm->gc);
        break;
    }
    case OP_FN_ADDLOCAL: {
        vm_secure_stack(vm, 1);
        vm_secure_callstack(vm, 1);

        activeframe_t* f = inpvec_at(&vm->callstack, vm->callstack.len - 1);
        stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
        
        f->frame->local[f->frame->vars++] = *top;
        vm->stack.len--;
        
        break;
    }
    case OP_FN_REMLOCAL: {
        vm_secure_callstack(vm, 1);
        activeframe_t* f = inpvec_at(&vm->callstack, vm->callstack.len - 1);
        f->frame->vars--;
        break;
    }
    case OP_FNLOCAL: {
        uint16_t steps = vm_get16(vm);
        uint8_t varidx = vm_get8(vm);

        vm_secure_callstack(vm, 1);
        
        activeframe_t* f = inpvec_at(&vm->callstack, vm->callstack.len - 1);
        
        fnframe_t* uh = f->frame;
        for(; uh->parent && steps; uh = uh->parent, --steps);
 
        inpvec_push(&vm->stack, uh->local + varidx);
        break;
    }
    case OP_RET: {
        vm_secure_callstack(vm, 1);
        vm_secure_stack(vm, 1);
        
        activeframe_t* f = inpvec_at(&vm->callstack, vm->callstack.len - 1);
        vm->pos = vm->prog + f->ra;
        vm->callstack.len--;

        stack_entry_t* top = inpvec_at(&vm->stack, vm->stack.len - 1);
        // the logic here:
        // if the function returns a value that does not need the function's closure, we free the closure (taking load off of the gc)
        // such values are int, bool, char, string, list(int), list(bool) ....
        // not all permutations are written here, only some common cases
        if(top->typeid < TYPE_ANY) { 
            gc_freeptr(&vm->gc, f->frame);
        }
        else if(top->typeid == TYPE_ANY) {
            any_t any = top->value.any;

            if(any.typeid < TYPE_ANY) {
                gc_freeptr(&vm->gc, f->frame);
            }
        }
        else if(vm->types[top->typeid].prim == TYPE_LIST) {
            if(vm->types[top->typeid].list.memb < TYPE_ANY) {
                gc_freeptr(&vm->gc, f->frame);
            }
        }
        else if(vm->types[top->typeid].prim == TYPE_ARRAY) {
            if(vm->types[top->typeid].array.memb < TYPE_ANY) {
                gc_freeptr(&vm->gc, f->frame);
            }
        }
        
        break;
    }
    case OP_CALL_EXTERN: {
        vm_secure_stack(vm, 1);

        uint16_t eid = vm_get16(vm);

        if(vm->externals[eid] == NULL) {
            vm_panic(vm, "[ VM ] tried to call missing external");
            return;
        }

        stack_entry_t (*fn)(vm_t*) = vm->externals[eid];
    
        stack_entry_t damn = fn(vm);
        inpvec_push(&vm->stack, &damn);
        break;
    }
    default: 
        vm_panic(vm, "[ VM ] forgot to define operation");
        return;
    }
}

void vm_run(vm_t* vm) {
    vm->status = VM_RUNNING;

    while(vm->pos < vm->prog + vm->size) {
        if(vm->status != VM_RUNNING) {
            return;
        }

        if(OP_HALT == *vm->pos) {        
            vm->status = VM_FROZEN;
            return;
        }

        vm_exec(vm);
    }

    vm_panic(vm, "[ VM ] No bytecode left");
}

void vm_kill(vm_t* vm) {
    if(vm->status != VM_FREED) {
        gc_freeall(&vm->gc);
        arena_kill(&vm->data);

        free(vm->externals);
        free(vm->funcs);
        free(vm->types);
        free(vm->callstack.elements);
        free(vm->stack.elements);
        vm->status = VM_FREED;
    }
}


////////////////////////////////////////////

static void vm_markval(vm_t* vm, uint16_t typeid, stack_value_t value);

static void vm_markframe(vm_t* vm, fnframe_t* frame) {
    for(fnframe_t* p = frame; p && !gc_ismarked(p); p = p->parent) {
        gc_mark(p);
         
        size_t vars = p->vars;
        for(size_t i = 0; i < vars; i++) {
            stack_entry_t* entry = p->local + i;
            vm_markval(vm, entry->typeid, entry->value);
        }
    }
}

static void vm_markval(vm_t* vm, uint16_t typeid, stack_value_t value) {
    type_t* type = vm->types + typeid;

    switch(type->prim) {
    case TYPE_BOOL:
    case TYPE_INT:
    case TYPE_PTR:
    case TYPE_CHAR:
        break;
    case TYPE_FUNC:
        vm_markframe(vm, value.fnval.parent);
        break;
    case TYPE_ANY: {
        if(value.any.typeid == TYPE_FUNC) { 
            gc_mark(value.any.fnval);
            vm_markframe(vm, value.any.fnval->parent);
        }
        else if(!type_small(value.any.typeid)){
            vm_markval(vm, value.any.typeid, (stack_value_t) {.ptr = value.any.ptr});
        }
        break;
    }
    case TYPE_TUPLE: {
        if(!value.ptr) {
            break;
        }
        
        stack_value_t* members = value.ptr;
        if(!gc_ismarked(members)) {
            gc_mark(members);

            for(size_t i = 0; i < type->tuple.mcount; i++) {
                vm_markval(vm, type->tuple.memb[i], members[i]);
            }
        }
        break;
    }
    case TYPE_LIST: {
        for(list_node_t* node = value.ptr; node; node = node->next) {
            if(gc_ismarked(node)) {
                break;
            }
 
            gc_mark(node);
            vm_markval(vm, type->list.memb, node->value);
        }
        break;
    }
    case TYPE_STRING: {
        arrstr_t* str = value.ptr;
        if(str && !gc_ismarked(str)) {
            gc_mark(str);
        }
        break;
    }
    case TYPE_ARRAY: {
        array_t* array = value.ptr;
        if(array && !gc_ismarked(array)) {
            gc_mark(array);
            for(size_t i = 0; i < array->length; i++) {
                vm_markval(vm, vm->types[typeid].array.memb, ((stack_value_t*)array->elements)[i]);
            }   
        }
        break;
    }
    default:
        vm_panic(vm, "[ VM ] idunno how to mark this type");
        return;
    }
}

static void vm_cleanup(void* p) {
    vm_t* vm = p;
    for(size_t i = 0; i < vm->stack.len; i++) {
        stack_entry_t* entry = inpvec_at(&vm->stack, i);
        vm_markval(vm, entry->typeid, entry->value);
    }

    for(size_t i = 0; i < vm->callstack.len; i++) {
        activeframe_t* fr = inpvec_at(&vm->callstack, i);
        vm_markframe(vm, fr->frame);
    }
}