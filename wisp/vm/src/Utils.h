#ifndef UTILS_H
#define UTILS_H

#define THIS_IS_VM
#include "Vm.h"

// compares a null-terminated string with a not null terminated string..
int strneq(const char* str1, char* str2, int len);

stack_entry_t vm_call(vm_t* vm, stack_entry_t input, stack_entry_t func);

// finds the const by name and returns it's value
stack_entry_t vm_getconst(vm_t* vm, char* name);

// finds the type by name and returns it's id
uint16_t vm_gettype(vm_t* vm, char* name);

// util to construct a tuple (members are cloned)
stack_entry_t vm_tuple(vm_t* vm, uint16_t typeid, stack_value_t* members);

// wraps a value to an any, may case allocation, run `gc_empty_temp` when done
stack_entry_t vm_wrapany(vm_t* vm, stack_entry_t entry);

// unwraps an any to a stackentry
stack_entry_t vm_unwrapany(vm_t* vm, any_t any);

// util to call a function accepting a tuple
stack_entry_t vm_calltup(vm_t* vm, uint16_t tupid, stack_value_t* values, stack_entry_t func);
#endif