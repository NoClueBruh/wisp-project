#ifndef SCAN_H
#define SCAN_H

#include <stdlib.h>
#include <stdint.h>

// splice of a not null-terminated string (not owned).
typedef struct {
    char* str;
    unsigned int len;
} strcut_t;
 
typedef struct {
    char* input;
    char* pchar;
    char* lchar;
    unsigned int line;

    string_t tmp;
} scanner_t;

typedef enum {
    TOKEN_LITERAL,
    TOKEN_SYMBOL, 
    TOKEN_INTEGER,
    TOKEN_STRING,
    TOKEN_CHAR,
    TOKEN_EOF,
} token_type_t;

typedef struct {
    union {
        uint32_t integer;
        strcut_t literal;
        strcut_t string; // does NOT handle special characters to keep the scanner heap-free. handle it yourself using `special_char`
        
        char symbol[3]; 
        char cchar; // special characters are handled, unlike strings.
    };

    token_type_t type : 8;
} token_t;

// returns whether `s1` is equal to `s2`.
int strcut_match(strcut_t s1, strcut_t s2);

// returns whether `s1` is equal to the null-terminated `str`.
int strcut_equals(strcut_t s1, char* str);

// returns whether `s1` is equal to `str`, whose length is `len`.
int strcut_equalsn(strcut_t s1, char* str, size_t len);

// initializes a new scanner instance.
// keep in mind that `input` must be available while polling and using tokens.
void scanner_new(scanner_t* scanner, char* input);

// polls the next token.
// note: the string token does not handle special characters automatically so as to keep the scanner malloc-free.
// the char token does.
void scanner_next(scanner_t* scanner, token_t* token);

// can be used to go back one token (one-time use after calling `scanner_next`). 
void scanner_rewind(scanner_t* scanner);

// returns the charcode for the special character `\${c}`.
char special_char(char c);

// prints a simple representation of a token to `fd`.
void token_print(FILE* fd, token_t* token);
#endif