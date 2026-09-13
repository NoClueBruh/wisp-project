#include "Alloc.h"
#include "Scan.h"

////

int strcut_match(strcut_t s1, strcut_t s2) {
    if(s1.len != s2.len)
        return 0;

    for(size_t i = 0; i < s1.len; i++) {
        if(s1.str[i] != s2.str[i])
            return 0;
    }
    return 1;
}

int strcut_equals(strcut_t s1, char* str) {
    size_t i = 0;
    for(; i < s1.len; i++) {
        if(s1.str[i] != str[i])
            return 0;
    }

    return str[i] == 0;
}

int strcut_equalsn(strcut_t s1, char* str, size_t len) {
    if(s1.len != len) {
        return 0;
    }

    size_t i = 0;
    for(; i < s1.len; i++) {
        if(s1.str[i] != str[i])
            return 0;
    }

    return 1;
}

char special_char(char c) {
    switch(c) {
    case '0':
        return 0;
    case 'n':
        return '\n';
    case 't':
        return '\t';
    }
    return c;
}

//////

static void sc_skipspaces(scanner_t* scanner) {
    while(scanner->pchar[0] != 0 && scanner->pchar[0] <= ' ') {
        if(scanner->pchar[0] == '\n') {
            scanner->line++; 
        }

        scanner->pchar++;
    }
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_hexdigit(char c) {
    return (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9');
}

static int is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_symbol(char c) {
    return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
}

static int joinsym(char c0, char c1) {
    return (c0 == c1 && (c0 == '.' || c0 == '+' || c0 == '&' || c0 == '|' || c0 == '=' || c0 == '/')) || (c1 == '=' && (c0 == '>' || c0 == '<' || c0 == '!')) || (c0 == '-' && c1 == '>');
}

static int iswchar(char c) {
    return is_letter(c) || is_digit(c) || c == '_';
}

static char sgetchar(scanner_t* scanner) {
    if(scanner->pchar[0] == 0)
        return 0;

    return *(scanner->pchar++);
}

void scanner_new(scanner_t* scanner, char* input) {
    scanner->line = 1;
    scanner->pchar = input;
    scanner->input = input;
}

void scanner_next(scanner_t* scanner, token_t* token) {
    sc_skipspaces(scanner);
    scanner->lchar = scanner->pchar;

    if(is_letter(scanner->pchar[0])) {
        token->type = TOKEN_LITERAL;
        token->literal = (strcut_t) {.str = scanner->pchar, .len = 0};

        while(iswchar(scanner->pchar[0])) {
            token->literal.len++;
            scanner->pchar++;
        }
    }
    else if(is_digit(scanner->pchar[0])) {
        token->type = TOKEN_INTEGER;
        token->integer = 0;

        while(is_digit(scanner->pchar[0])) {
            token->integer = 10 * token->integer + scanner->pchar[0] - '0';
            scanner->pchar++;
        }
    } 
    else if(scanner->pchar[0] == '\\') {
        char next = scanner->pchar[1];
        
        if(next == 'x') {
            scanner->pchar += 2;
            
            token->type = TOKEN_INTEGER;
            token->integer = 0;

            char c = 0;
            while(is_hexdigit(c = scanner->pchar[0])) { 
                token->integer = 16 * token->integer + (c > '9' ? 10 + c - 'A' : c - '0');
                scanner->pchar++;
            }
        }
        else {
            token->type = TOKEN_SYMBOL;
            token->symbol[0] = '\\';
        }
    }
    else if(scanner->pchar[0] == '"') {
        scanner->pchar++;

        token->type = TOKEN_STRING;
        token->string = (strcut_t) {scanner->pchar, 0};
        
        for(char c; c = *scanner->pchar; scanner->pchar++) { 
            if(c == '\\') {
                scanner->pchar++;
            }
            else if(c == '"') {
                scanner->pchar++;
                break;
            }
        }
        
        token->string.len = scanner->pchar - token->string.str;
        if(token->string.len > 0) {
            token->string.len--;
        }
    }
    else if(scanner->pchar[0] == '\'') {
        token->type = TOKEN_CHAR;

        sgetchar(scanner);
        if(scanner->pchar[0] == 0)
            token->type = TOKEN_EOF;
        else {
            char c = sgetchar(scanner);

            if(c == 0) {
                token->type = TOKEN_EOF;
            }
            else if(c == '\'') {
                token->cchar = 0;
                scanner->pchar--;
            }
            else if(c != '\\') {
                token->cchar = c;
            }
            else {
                char c = sgetchar(scanner);

                if(c == 0) {
                    token->cchar = 0; // yo
                }
                else {
                    token->cchar = special_char(c);
                }
            }

            for(char c; c = sgetchar(scanner), (c != 0 && c != '\''););
        }
    }
    else if(is_symbol(scanner->pchar[0])){
        token->type = TOKEN_SYMBOL;
        token->symbol[0] = *(scanner->pchar++); 
        token->symbol[2] = 0;

        if(joinsym(token->symbol[0], scanner->pchar[0])) {
            token->symbol[1] = *(scanner->pchar++);
            
            if(token->symbol[0] == token->symbol[1] && token->symbol[0] == '/') { 
                // comments!
                for(char c; c = scanner->pchar[0], (c != 0 && c != '\n'); scanner->pchar++);
                scanner_next(scanner, token); 
            }
        }
        else {
            token->symbol[1] = 0; 
        }
    } 
    else if(scanner->pchar[0] == 0){
        token->type = TOKEN_EOF;
    }
    else {
        printf("bro you forgot to take care of c %d\n", scanner->pchar[0]);
        token->type = TOKEN_EOF;
    }
}

void scanner_rewind(scanner_t* scanner) {
    scanner->pchar = scanner->lchar;
} 

void token_print(FILE* fl, token_t* token) {
    switch(token->type) {
    case TOKEN_EOF:
        fputs("EOF", fl);
        break;
    case TOKEN_INTEGER:
        fprintf(fl, "(u32) %d", token->integer);
        break;
    case TOKEN_SYMBOL:
        fprintf(fl, "(symbol) \"%s\"", token->symbol);
        break;
    case TOKEN_LITERAL:
        fprintf(fl, "(literal) \"%.*s\"", token->literal.len, token->literal.str);
        break; 
    case TOKEN_STRING:
        fprintf(fl, "(string) \"%.*s\"", token->string.len, token->string.str);
        break;
    case TOKEN_CHAR:
        fprintf(fl, "(char) '%c'", token->cchar);
        break;
    default:
        fprintf(fl, "(unknown) ???");
        break;
    }
}