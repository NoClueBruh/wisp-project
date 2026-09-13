#define THIS_IS_VM
#include "Vm.h"
#include "Utils.h"

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: ./vm <input_file>\n");
        return 1;
    }

    FILE* input = fopen(argv[1], "rb");
    if(!input) {
        fprintf(stderr, "Couldn't open file \"%s\"\n", argv[1]);
        return 1;
    }

    fseek(input, 0, SEEK_END);
    long size = ftell(input);
    fseek(input, 0, SEEK_SET); 
    char* buffer = mem_req(size);
    fread(buffer, sizeof(char), size, input); 
    fclose(input);

    vm_t vm = {0};
    // vm.getextern = getextern;
    // vm.onpanic = onpanic;
    vm_load(&vm, buffer, size);  
    vm_run(&vm); 
    vm_kill(&vm); 

    free(buffer); 
    return 0;
}