#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#define int long long // Work with 64-bit machines

int poolsz;           // default size of text/data/stack
int line;             // line number
char *src, *old_src;  // pointer to source code string
int token;            // current token

// memory allocation sections
// without considering virtual memory mapping
//
//    *text: text section
//    *old_text: for dumping text section
//    *stack: stack section

int *text, *old_text;  // text section
int *stack;            // stack section
char *data;            // data section

// ----- Virtual Machine ----- //
//    *pc:   program counter
//    *bp:   stack base pointer
//    *sp:   stack pointer
//    gpr:   general purpose register (only 1)
//    cycle: 

int *pc, *bp, *sp, gpr, cycle;
// support CPU instructions (x86)
enum { LEA,  IMM,  JMP,  CALL, JZ,   JNZ,  ENT,  ADJ, LEV, LI,  LC,  SI,  SC,  PUSH, 
       OR,   XOR,  AND,  EQ,   NE,   LT,   GT,   LE,  GE,  SHL, SHR, ADD, SUB, MUL,  DIV, MOD, 
       OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT };


// ----- Lexer ----- //
// tokens
enum { Num = 128, Fun, Sys, Glo, Loc, Id, 
       Char, Else, Enum, If, Int, Return, Sizeof, While, 
       Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak };
// variable identifier table
// struct identifier { 
//     int token;        // token type
//     int hash;         // hash value, to speed up the comparision of table lookup
//     char *name;       // name of the identifier
//     int class;        // the identifier belonging among global, local, or constants
//     int type;         // type of the identifier, `int`, `char` or pointer
//     int value;        // the value that the identifier points to
//     int Bclass;       // Bxxxx:
//     int Btype;        //     When the local variable name is the same as the global
//     int Bvalue; };    //     one, it will be served as an updating of global variable

// since our compiler does not support `struct` , we use `enum` instead
int token_val;      // value of current token (mainly for number)
int *curr_id;       // current parsed ID
int *symbols;       // symbol table
// fields of identifier
enum { Token, Hash, Name, Type, Class, Value, BType, BClass, BValue, IdSize };

// types of variables/functions
enum { CHAR, INT, PTR };
int *idmain;             // the main function

void next() {
    char *last_pos;
    int hash;

    while (token = *src) {
        ++src;

        // parse token here
        if (token=='\n') {
            // NEWLINE
            ++line;

        } else if (token=='#') {
            // MACRO
            // skip macro, which it's not supported in our compiler
            while (*src!=0 && *src!='\n') src++;

        } else if ((token>='a' && token<='z') || (token>='A' && token<='Z') || (token=='_')) {
            // IDENTIFIERS and SYMBOL TABLE
            // parse identifier
            last_pos = src - 1;
            hash = token;

            while ((*src>='a' && *src<='z') || (*src>='A' && *src<='Z') || (*src>='0' && *src<='9') || (*src=='_')) {
                hash = hash * 147 + *src;
                src++;
            }

            // look for existing identifier, linear search
            curr_id = symbols;
            while (curr_id[Token]) {
                if (curr_id[Hash]==hash && !memcmp((char *)curr_id[Name], last_pos, src - last_pos)) {
                    // found one, return
                    token = curr_id[Token];
                    return ;
                }
                curr_id = curr_id + IdSize;
            }

            // store new ID
            curr_id[Name] = (int)last_pos;
            curr_id[Hash] = hash;
            token = curr_id[Token] = Id;
            return ;

        } else if (token>='0' && token<='9') {
            // NUMBERS
            // support three kinds of number representations: decimal, hexadecimal, octal
            token_val = token - '0';
            if (token_val>0) {
                // dec: never starts with '0'
                while (*src>='0' && *src<='9') {
                    token_val = token_val * 10 + *src++ - '0';
                }
            } else {
                // hex: 0xFF
                // oct: 0o77
                if (*src=='x' || *src=='X') {
                    token = *++src;
                    while ((token>='0' && token<='9') || (token>='a' && token<='f') || (token>='A' && token<='F')) {
                        token_val = token_val * 16 + (token & 15) + (token>='A' ? 9 : 0);
                        token = *++src;
                    }
                } else {
                    while (*src>='0' && *src<='7') {
                        token_val = token_val * 8 + *src++ - '0';
                    }
                }
            }

            token = Num;
            return ;

        } else if (token=='"' || token=='\'') {
            // STRING LITERALS
            last_pos = data;
            while (*src!=0 && *src!=token) {
                token_val = *src++;
                if (token_val=='\\') {
                    token_val = *src++;
                    // the only supported escape character for now is '\n'
                    if (token_val=='n') token_val = '\n';
                }
                
                if (token=='"') {
                    *data++ = token_val;
                }
            }

            src++;
            // if it is a single character, return token: Num
            if (token=='"') {
                token_val = (int)last_pos;
            } else {
                token = Num;
            }

            return ;

        } else if (token=='/') {
            // COMMENTS
            // only C++ style comments is supported, e.g. // comment
            // while C style comments is not supported, e.g. /* comment */
            if (*src=='/') {
                // skip comments
                while (*src!=0 && *src!='\n') ++src;
            } else {
                // divide operator
                token = Div;
                return ;
            }

        } else if (token=='=') {
            // parse '==' and '='
            if (*src=='=') {
                src++;
                token = Eq;
            } else {
                token = Assign;
            }
            return;

        } else if (token=='+') {
            // parse '+' and '++'
            if (*src=='+') {
                src++;
                token = Inc;
            } else {
                token = Add;
            }
            return ;

        } else if (token=='-') {
            // parse '-' and '--'
            if (*src=='-') {
                src++;
                token = Dec;
            } else {
                token = Sub;
            }
            return ;

        } else if (token=='!') {
            // parse '!='
            if (*src=='=') {
                src++;
                token = Ne;
            }
            return ;

        } else if (token=='<') {
            // parse '<=', '<<' or '<'
            if (*src=='=') {
                src++;
                token = Le;
            } else if (*src=='<') {
                src++;
                token = Shl;
            } else {
                token = Lt;
            }
            return ;

        } else if (token=='>') {
            // parse '>=', '>>', or '>'
            if (*src=='=') {
                src++;
                token = Ge;
            } else if (*src=='>') {
                src++;
                token = Shr;
            } else {
                token = Gt;
            }
            return;

        } else if (token=='|') {
            // parse '|' or '||'
            if (*src=='|') {
                src++;
                token = Lor;
            } else {
                token = Or;
            }
            return ;

        } else if (token=='&') {
            // parse '&' and '&&'
            if (*src=='&') {
                src++;
                token = Lan;
            } else {
                token = And;
            }
            return ;

        } else if (token=='^') {
            token = Xor;
            return ;

        } else if (token=='%') {
            token = Mod;
            return ;

        } else if (token=='*') {
            token = Mul;
            return ;

        } else if (token=='[') {
            token = Brak;
            return ;

        } else if (token=='?') {
            token = Cond;
            return ;

        } else if (token=='~' || token==';' || token=='{' || token=='}' || token=='(' || token==')' || token==']' || token==',' || token==':') {
            // directly return the character as token
            return ;
        }
    }
    return ;
}

void expression(int level) {
    // do nothing
}

void program() {
    next();
    while (token > 0) {
        printf("token is: %c\n", token);
        next();
    }
}

int eval() {
    int op, *tmp;
    while (1) {
        op = *pc++;                                                            // Get next operation

        if      (op==IMM)  { gpr = *pc++; }                                    // load IMMediate
        else if (op==LC)   { gpr = *(char *)gpr; }                             // Load Character
        else if (op==LI)   { gpr = *(int *)gpr; }                              // Load Integer
        else if (op==SC)   { *(char *)*sp++ = gpr; }                           // Save Character
        else if (op==SI)   { *(int *)*sp++ = gpr; }                            // Save Integer
        else if (op==PUSH) { *--sp = gpr; }                                    // PUSH value onto the stack

        // jump (branch)
        else if (op==JMP)  { pc = (int *)*pc; }                                // JuMP to the address
        else if (op==JZ)   { pc = gpr ? pc + 1 : (int *)*pc; }                 // Jump if (gpr==0)
        else if (op==JNZ)  { pc = gpr ? (int *)*pc : pc + 1; }                 // Jump if Not (gpr==0)

        // function call
        else if (op==CALL) { *--sp = (int)(pc + 1); pc = (int *)*pc; }         // CALL subroutine
        // else if (op==RET)  { pc = (int *)*sp++; }                           // RET is not provided for simplicity
        else if (op==ENT)  { *--sp = (int)bp; bp = sp; sp = sp - *pc++; }      // ENTer, to make new stack frame
        else if (op==ADJ)  { sp = sp + *pc++; }                                // pop all args from frame
        else if (op==LEV)  { sp = bp; bp = (int *)*sp++; pc = (int *)*sp++; }  // LEaVe subroutine, which is 'pop and return'
        else if (op==LEA)  { gpr = (int)(bp + *pc++); }                        // Load Effective Address, load the args

        // Arithmetic operations
        else if (op==OR)   { gpr = *sp++ |  gpr; }
        else if (op==XOR)  { gpr = *sp++ ^  gpr; }
        else if (op==AND)  { gpr = *sp++ &  gpr; }
        else if (op==EQ)   { gpr = *sp++ == gpr; }
        else if (op==NE)   { gpr = *sp++ != gpr; }
        else if (op==LT)   { gpr = *sp++ <  gpr; }
        else if (op==LE)   { gpr = *sp++ <= gpr; }
        else if (op==GT)   { gpr = *sp++ >  gpr; }
        else if (op==GE)   { gpr = *sp++ >= gpr; }
        else if (op==SHL)  { gpr = *sp++ << gpr; }
        else if (op==SHR)  { gpr = *sp++ >> gpr; }
        else if (op==ADD)  { gpr = *sp++ +  gpr; }
        else if (op==SUB)  { gpr = *sp++ -  gpr; }
        else if (op==MUL)  { gpr = *sp++ *  gpr; }
        else if (op==DIV)  { gpr = *sp++ /  gpr; }
        else if (op==MOD)  { gpr = *sp++ %  gpr; }

        // Built-in Instructions
        else if (op==EXIT) { printf("exit(%d)", *sp); return *sp; }
        else if (op==OPEN) { gpr = open((char *)sp[1], sp[0]); }
        else if (op==CLOS) { gpr = close(*sp); }
        else if (op==READ) { gpr = read(sp[2], (char *)sp[1], *sp); }
        else if (op==PRTF) { tmp = sp + pc[1]; gpr = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); }
        else if (op==MALC) { gpr = (int)malloc(*sp); }
        else if (op==MSET) { gpr = (int)memset((char *)sp[2], sp[1], *sp); }
        else if (op==MCMP) { gpr = memcmp((char *)sp[2], (char *)sp[1], *sp); }
        else {
            printf("Unknown instruction: %d\n", op);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    int i, fd;

    --argc;
    ++argv;
    poolsz = 256 * 1024;  // arbitrary size
    line = 1;

    if ((fd = open(*argv, 0)) < 0) {
        printf("Could not open(%s)\n", *argv);
        return -1;
    }

    // allocate memory for virtual machine
    if (!(text = old_text = malloc(poolsz))) {
        printf("Could not malloc(%d) for text area\n", poolsz);
        return -1;
    }

    if (!(data = malloc(poolsz))) {
        printf("Could not malloc(%d) for data area\n", poolsz);
        return -1;
    }

    if (!(stack = malloc(poolsz))) {
        printf("Could not malloc(%d) for stack area\n", poolsz);
        return -1;
    }

    if (!(symbols = malloc(poolsz))) {
        printf ("Could not malloc(%d) for symbol table\n", poolsz);
        return -1;
    }

    memset(text, 0, poolsz);
    memset(data, 0, poolsz);
    memset(stack, 0, poolsz);
    memset(symbols, 0, poolsz);

    bp = sp = (int *)((int)stack + poolsz);
    gpr = 0;

    // keywords with special meaning
    // which cannot be considered as a normal identifier
    //   => one must add these special keywords to
    //      symbol table before calling `next()`
    src = "char else enum if int return sizeof while "
          "open read close printf malloc memset memcmp exit void main";

    // add keywords to symbol table
    i = Char;
    while (i<=While) {
        // Char, Else, Enum, If, Int, Return, Sizeof, While, 
        next();
        curr_id[Token] = i++;
    }

    // add library to symbol table
    i = OPEN;
    while (i<=EXIT) {
        // OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT
        next();
        curr_id[Class] = Sys;
        curr_id[Type]  = INT;
        curr_id[Value] = i++;
    }

    next(); curr_id[Token] = Char;  // handle void type
    next(); idmain = curr_id;       // keep track of main

    if (!(src = old_src = malloc(poolsz))) {
        printf("Could not malloc(%d) for source area\n", poolsz);
        return -1;
    }

    // read the source file
    if ((i = read(fd, src, poolsz - 1)) <= 0) {
        printf("read() returned %d\n", i);
        return -1;
    }

    src[i] = 0; // 0 as '\0' representing a EOF character
    close(fd);

    program();
    return eval();
}