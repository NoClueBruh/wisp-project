#include<stdio.h>
#include"Parser.h"

int main(int argc, char** argv) {
    if(argc != 3) {
        fprintf(stderr, "Usage: ./flang <source_file> <output_file>\n");
        return 1;
    }
 
    FILE* output = fopen(argv[2], "wb");
     
    if(!output) {
        fprintf(stderr, "Cannot open file \"%s\" for writing\n", argv[2]);
        return 1;
    } 
    
    // copying path
    size_t plen = strlen(argv[1]);
    char*  path = mem_req(plen + 1);
    memcpy(path, argv[1], plen + 1);

    compile_output_t compout = parser_compile((heapstr_t) {path, plen}, NULL);

    if(!compout.error) {
        fwrite(compout.bytecode.buffer, sizeof(char), compout.bytecode.len, output);
        free(compout.bytecode.buffer);
        fclose(output);
        return 0;
    }
    fclose(output);
    return 1;
}