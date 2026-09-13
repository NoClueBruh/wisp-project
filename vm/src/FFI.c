/*
    this is here to show how external functions can give the language way more functionality.
    as you might have guessed, it is a simple ffi implementation using libffi for linux.
    this can of course be uncommented and used!
*/

/*
#include <ffi.h> 
#include <dlfcn.h>
#include "Utils.h"

// quick function to pop the top element of the stack
static stack_entry_t stacktop(vm_t* vm) {
    if(vm->stack.len) {
        vm->stack.len--;
        return *(stack_entry_t*) inpvec_at(&vm->stack, vm->stack.len);
    }

    return (stack_entry_t) {.typeid = 0, .value.integer = 0};
}

// example function on how variable arguments can be used by the vm
static stack_entry_t sum(vm_t* vm) {
    stack_entry_t top = stacktop(vm);
    stack_entry_t res = {.typeid = TYPE_INT, .value.integer = 0};

    // when a function has variable arguments, the very top element when the function is called is the number of variable arguments passed.
    // check if the type is TYPE_INT just in case. 
    // technically anyone can change the function signature so it is good to have checks like these
    if(top.typeid != TYPE_INT) {
        return res;
    }

    int n = top.value.integer;  
    for(int i = 0; i < n; i++) {
        top = stacktop(vm);
        if(top.typeid == TYPE_INT) {
            res.value.integer += top.value.integer;
        }
    }
    return res;
}

/////////////////////////////////////////////////////////////////////

static stack_entry_t ffi_loadlib(vm_t* vm) { 
    stack_entry_t input = stacktop(vm);

    arrstr_t* libpath = input.value.ptr;
    if(input.typeid != TYPE_STRING || !libpath) {
        return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = NULL};
    }
   
    void* lib = dlopen(libpath->characters, RTLD_LAZY);

    if(!lib) {
        printf("[ VM - FFI ] library \"%.*s\" failed to open.\n", libpath->length, libpath->characters);
    } 

    return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = lib};
}

static stack_entry_t ffi_unloadlib(vm_t* vm) {
    stack_entry_t input = stacktop(vm);

    if(input.typeid != TYPE_PTR) {
        return (stack_entry_t) {.typeid = TYPE_INT, .value.integer = 1};
    }
    
    return (stack_entry_t) {.typeid = TYPE_INT, .value.integer = dlclose(input.value.ptr)};
}

static stack_entry_t ffi_getsym(vm_t* vm) {
    stack_entry_t lib = stacktop(vm);
    stack_entry_t sym = stacktop(vm);
    
    if(lib.typeid != TYPE_PTR || sym.typeid != TYPE_STRING || !lib.value.ptr || !sym.value.ptr) {
        return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = NULL};
    }

    void* p = dlsym(lib.value.ptr, ((arrstr_t*) sym.value.ptr)->characters); 
    return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = p};
}

static stack_entry_t ffi_callvoid(vm_t* vm) {
    stack_entry_t input = stacktop(vm);

    if(input.typeid != TYPE_PTR || !input.value.ptr) {
        return (stack_entry_t) {.typeid = TYPE_INT, .value.integer = 1};
    } 

    ((void (*)())input.value.ptr)();
    return (stack_entry_t) {.typeid = TYPE_INT, .value.integer = 0};
}

// function that "converts" the language's string to a c-string
static stack_entry_t ffi_cstr(vm_t* vm) {
    stack_entry_t input = stacktop(vm);

    if(input.typeid != TYPE_STRING || !input.value.ptr) { 
        return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = NULL};
    } 

    return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = ((arrstr_t*)input.value.ptr)->characters};
}

// function that copies the c-string as a language string
static stack_entry_t ffi_lstr(vm_t* vm) {
    stack_entry_t input = stacktop(vm);

    if(input.typeid != TYPE_PTR) { 
        return (stack_entry_t) {.typeid = TYPE_PTR, .value.ptr = NULL};
    }    
 
    char* buff = input.value.ptr; 
    uint32_t len = buff ? strlen(buff) : 0; 
 
    arrstr_t* str = gc_alloc(&vm->gc, sizeof(arrstr_t) + len + 1);
    str->length = len;
    str->characters[len] = 0;
    if(len) {
        memcpy(str->characters, buff, len);
    }
    gc_empty_temp(&vm->gc);

    return (stack_entry_t) {.typeid = TYPE_STRING, .value.ptr = str};
}

static inline void ffi_addarg(vm_t* vm, stack_value_t* value, uint16_t typeid, ffi_type** ft, void** fv) {
    type_t* argtype = vm->types + typeid;
 
    switch(argtype->prim) {
    case TYPE_INT:
        *ft = &ffi_type_sint;
        *fv = &value->integer;  
        break;
    case TYPE_CHAR:
        *ft = &ffi_type_schar;
        *fv = &value->character;
        break;
    case TYPE_BOOL:
        *ft = &ffi_type_schar;
        *fv = &value->boolean;
        break;
    case TYPE_PTR: 
        *ft = &ffi_type_pointer;
        *fv = &value->ptr; 
        break;
    case TYPE_ANY: {
        stack_entry_t* entry = value->ptr;
        if(!entry) {
            *ft = &ffi_type_pointer;
            *fv = &value->ptr; 
        }
        else {
            ffi_addarg(vm, &entry->value, entry->typeid, ft, fv);
        }
        break;
    }
    case TYPE_STRING:
        vm_panic(vm, "[ VM - FFI ] convert string to cstr.");
    default:
        vm_panic(vm, "[ VM - FFI ] argument combination not supported.");
    }
}

static stack_entry_t ffi_callargs(vm_t* vm) {
    stack_entry_t rest = stacktop(vm);
    stack_entry_t sym = stacktop(vm);
    stack_entry_t out = stacktop(vm);

    if(sym.typeid != TYPE_PTR || out.typeid != TYPE_INT || rest.typeid != TYPE_INT || !sym.value.ptr) {  
        return (stack_entry_t) {.typeid = TYPE_ANY, .value.ptr = NULL};
    }

    int argc = rest.value.integer;

    stack_value_t rvalue;
    ffi_type* rtype; 
    
    switch(out.value.integer) {
    case TYPE_INT:
        rtype = &ffi_type_sint;
        break;
    case TYPE_CHAR:
    case TYPE_BOOL:
        rtype = &ffi_type_schar;
        break;
    case TYPE_PTR:  
        rtype = &ffi_type_pointer;
        break;
    case -1:
        rtype = &ffi_type_void;
        break;
    default:
        vm_panic(vm, "[ VM - FFI ] unsupported return type.");
    }

    ffi_cif cif; 
    if(argc == 0) {  
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, rtype, NULL);
        ffi_call(&cif, sym.value.ptr, &rvalue, NULL);
    }
    else { 
        // actual ffi stuff 
        {
            ffi_type* ffi_arg_types[argc];
            void* ffi_arg_values[argc];
            stack_entry_t args[argc];

            for(size_t i = 0; i < argc; i++) {
                args[i] = stacktop(vm);
                ffi_addarg(vm, &args[i].value, args[i].typeid, &ffi_arg_types[i], &ffi_arg_values[i]);
            }

            ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argc, rtype, ffi_arg_types);
            ffi_call(&cif, sym.value.ptr, &rvalue, ffi_arg_values);
        } 
    }

    if(rtype == &ffi_type_void) { 
        return (stack_entry_t) {.typeid = TYPE_ANY, .value.ptr = NULL}; 
    }

    return vm_wrapany(vm, (stack_entry_t) {.typeid = out.value.integer, .value = rvalue});
}

// you can do `vm.getextern = ffi_getextern` and this'll work
void* ffi_getextern(char* name, int len) { 
    if(strneq("ffi_loadlib", name, len)) { 
        return ffi_loadlib;
    }
    if(strneq("ffi_unloadlib", name, len)) {
        return ffi_unloadlib;
    }
    if(strneq("ffi_getsym", name, len)) {
        return ffi_getsym;
    }
    if(strneq("ffi_callvoid", name, len)) {
        return ffi_callvoid;
    }
    if(strneq("ffi_callargs", name, len)) {
        return ffi_callargs;
    }
    if(strneq("ffi_cstr", name, len)) {
        return ffi_cstr;
    }
    if(strneq("ffi_lstr", name, len)) {
        return ffi_lstr;
    }
    if(strneq("sum", name, len)) {
        return sum;
    }
    
    return NULL;
}
*/