#include "Utils.h" 

int strneq(const char* str1, char* str2, int len) {
    for(int i = 0; i < len; i++) {
        if(str1[i] != str2[i]) {
            return 0;
        }
    }

    return str1[len] == 0;
}

////////////
 
stack_entry_t vm_wrapany(vm_t* vm, stack_entry_t entry) {
    any_t any;
    any.typeid = entry.typeid;

    if(entry.typeid == TYPE_FUNC) {
        fnval_t* v = gc_alloc(&vm->gc, sizeof(fnval_t));
        *v = entry.value.fnval;
    }
    else if(type_small(entry.typeid)) {
        any.integer = entry.value.integer;
    }
    else {
        any.ptr = entry.value.ptr;
    }

    return (stack_entry_t) {.typeid = TYPE_ANY, .value.any = any};
}

stack_entry_t vm_unwrapany(vm_t* vm, any_t any) {
    stack_entry_t result;
    if(any.typeid == TYPE_FUNC) {
        result.value.fnval = *any.fnval;
    }
    else if(type_small(any.typeid)){
        result.value.integer = any.integer;
    }
    else {
        result.value.ptr = any.ptr;
    }
    result.typeid = any.typeid;
    return result;
}

static stack_entry_t vm_getentry(vm_t* vm, metadata_t data) {
    if(data.type != META_CONST) {
        vm_panic(vm, "[ VM ] Cannot get stack entry, wrong metadata type");
    }

    if(data.data >= vm->stack.len) {
        vm_panic(vm, "[ VM ] Cannot get stack entry, bad metadata");
    }

    return *((stack_entry_t*)inpvec_at(&vm->stack, data.data));
}

stack_entry_t vm_getconst(vm_t* vm, char* name) {
    metadata_t dat = vm_getmeta(vm, name, strlen(name));

    if(dat.type != META_CONST) {
        vm_panic(vm, "[ VM ] Such const doesn't exist, maybe it was not @exposed?");
    }

    return vm_getentry(vm, dat);
}

uint16_t vm_gettype(vm_t* vm, char* name) {
    metadata_t dat = vm_getmeta(vm, name, strlen(name));

    if(dat.type != META_TYPE) {
        vm_panic(vm, "[ VM ] Such type doesn't exist, maybe it was not @exposed?");
    }

    return dat.data;
}

stack_entry_t vm_tuple(vm_t* vm, uint16_t typeid, stack_value_t* members) {
    size_t memc = vm->types[typeid].tuple.mcount;

    stack_value_t* p = gc_alloc(&vm->gc, sizeof(stack_value_t) * memc);
    for(size_t i = 0; i < memc; i++) {
        p[i] = members[i];
    }

    return (stack_entry_t) {
        .typeid = typeid,
        .value.ptr = p
    };
}

/////////////

stack_entry_t vm_calltup(vm_t* vm, uint16_t tupid, stack_value_t* values, stack_entry_t func) {
    stack_entry_t input = vm_tuple(vm, tupid, values);
    gc_empty_temp(&vm->gc);

    return vm_call(vm, input, func);
}

/////////////

stack_entry_t vm_call(vm_t* vm, stack_entry_t input, stack_entry_t func) {
    if(vm->types[func.typeid].prim != TYPE_FUNC) {
        vm_panic(vm, "[ VM ] Cannot call such entry");
    }

    uint32_t framec = vm->callstack.len;
    unsigned char* last_pos = vm->pos;

    // temporary instruction to exec
    unsigned char op = OP_FNCALL;
    vm->pos = &op;

    // push value and function
    inpvec_push(&vm->stack, &input);
    inpvec_push(&vm->stack, &func);

    // call OP_FNCALL, jump to the function
    vm_exec(vm);
    
    // execute the entire function until the end
    vm->status = VM_RUNNING;
    while(vm->pos < vm->prog + vm->size) {
        if(vm->status != VM_RUNNING) {
            break;
        }

        // make sure we finished calling "func" and not another function
        if(*vm->pos == OP_RET && vm->callstack.len == framec + 1) {
            vm->status = VM_FROZEN;
            break;
        }

        vm_exec(vm);
    }

    // pop frame
    vm->callstack.len--;
    
    // return to were this function was called
    vm->pos = last_pos;
    
    stack_entry_t output = *((stack_entry_t*)inpvec_at(&vm->stack, vm->stack.len - 1));
    vm->stack.len--;

    return output;
}