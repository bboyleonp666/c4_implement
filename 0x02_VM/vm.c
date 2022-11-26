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
/*
    *text: text section
    *old_text: for dumping text section
    *stack: stack section
*/
int *text, *old_text, *stack;
//data section
char *data;

/*
    *pc: program counter
    *bp: stack base pointer
    *sp: stack pointer
    gpr: general purpose register (only 1)
    cycle: 
*/
int *pc, *bp, *sp, gpr, cycle;

// support CPU instructions (x86)
enum { LEA,  IMM,  JMP,  CALL, JZ,   JNZ,  ENT,  ADJ, LEV, LI,  LC,  SI,  SC,  PUSH, 
       OR,   XOR,  AND,  EQ,   NE,   LT,   GT,   LE,  GE,  SHL, SHR, ADD, SUB, MUL,  DIV, MOD, 
       OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT };

void next() {
    token = *src++;
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
    poolsz = 256 * 1024;
    line = 1;

    if ((fd = open(*argv, 0)) < 0) {
        printf("Could not open(%s)\n", *argv);
        return -1;
    }

    if (!(src = old_src = malloc(poolsz))) {
        printf("Could not malloc(%d) for source area\n", poolsz);
        return -1;
    }

    if ((i = read(fd, src, poolsz - 1)) <= 0) {
        printf("read() returned %d\n", i);
        return -1;
    }

    src[i] = 0; // 0 as '\0' representing a EOF character
    close(fd);

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

    memset(text, 0, poolsz);
    memset(data, 0, poolsz);
    memset(stack, 0, poolsz);

    bp = sp = (int *)((int) stack + poolsz);
    gpr = 0;

    i = 0;
    text[i++] = IMM;
    text[i++] = 10;
    text[i++] = PUSH;
    text[i++] = IMM;
    text[i++] = 20;
    text[i++] = ADD;
    text[i++] = PUSH;
    text[i++] = EXIT;
    pc = text;

    program();
    return eval();
}