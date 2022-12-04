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
// ordered by their precedences from low to high
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

// variables for expression requirements
int basetype;    // the type of declaration
int expr_type;   // the type of an expression
int index_of_bp; // index of bp pointer on stack

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

void match(int tk) {
    if (token==tk) {
        next();
    } else {
        printf("%d: Expected token: %d\n", line, tk);
        exit(-1);
    }
}

void expression(int level) {
    
    // UNARY OPERATORS
    int *id;
    int tmp;
    int *addr;
    if (!token) {
        printf("%d: Unexpected token EOF of expression\n", line);
        exit(-1);
    }

    if (token==Num) {
        // constant
        match(Num);

        // emit code
        *++text = IMM;
        *++text = token_val;
        expr_type = INT;

    } else if (token=='"') {
        // continuous string
        *++text = IMM;
        *++text = token_val;
        match('"');

        while (token=='"') {
            match('"');
        }

        // append EOF, namely '\0', to a string
        // since all the data are default to 0, we can just move data 1 position forward
        data = (char *)( ((int)data + sizeof(int)) & (-sizeof(int)) );
        expr_type = PTR;

    } else if (token==Sizeof) {
        // sizeof() is actually an unary operator
        // only `sizeof(int)`, `sizeof(char)` and `sizeof(*...) are supported here
        match(Sizeof);
        match('(');
        expr_type = INT;

        if (token==Int) {
            match(Int);
        } else if (token==Char) {
            match(Char);
            expr_type = CHAR;
        }

        while (token==Mul) {
            match(Mul);
            expr_type = expr_type + PTR;
        }

        match(')');

        *++text = IMM;
        *++text = (expr_type==CHAR) ? sizeof(char) : sizeof(int);
        expr_type = INT;

    } else if (token==Id) {
        // there are several type when occurs to Id
        // but since this is unit, it can only be
        // 1. function call
        // 2. enum variable
        // 3. global/local variable
        match(Id);

        id = curr_id;

        if (token=='(') {
            // here we use infix representation
            // however, in C standard, the arithmetic operations are usually in postfix
            match('(');

            tmp = 0;                 // number of arguments
            while (token!=')') {
                expression(Assign);
                *++text = PUSH;
                tmp++;

                if (token==',') {
                    match(',');
                }
            }
            match(')');

            // currently we support `printf()`, `read()`, `malloc()` and other built in functions
            // the rest functions are compiled into `CALL <addr>`
            if (id[Class]==Sys) {
                // system functions
                *++text = id[Value];

            } else if (id[Class]==Fun) {
                // function call
                *++text = CALL;
                *++text = id[Value];

            } else {
                printf("%d: Bad function call\n", line);
                exit(-1);
            }

            // clean the stack for arguments
            if (tmp>0) {
                *++text = ADJ;
                *++text = tmp;
            }
            expr_type = id[Type];

        } else if (id[Class]==Num) {
            // enum variable
            // treated as constants
            *++text = IMM;
            *++text = id[Value];
            expr_type = INT;

        } else {
            // variables
            // load the values of variables using `bp + offset`
            // load the address of global variables using `IMM`
            if (id[Class]==Loc) {
                *++text = LEA;
                *++text = index_of_bp - id[Value];
            } else if (id[Class]==Glo) {
                *++text = IMM;
                *++text = id[Value];
            } else {
                printf("%d: Undefined variable\n", line);
                exit(-1);
            }

            // emit code, default behavior is to load the value of the address 
            // which is stored in `gpr`
            // load the value of variables using `LI/LC` according to their type
            expr_type = id[Type];
            *++text = (expr_type==Char) ? LC : LI;
        }

    } else if (token=='(') {
        // cast or parenthesis
        match('(');

        if (token==Int || token==Char) {
            tmp = (token==Char) ? CHAR : INT;  // cast type
            match(token);
            while (token==Mul) {
                match(Mul);
                tmp = tmp + PTR;
            }
            match(')');

            expression(Inc);                   // cast has precedence as Inc(++)
            expr_type = tmp;

        } else {
            // normal parenthesis
            expression(Assign);
            match(')');
        }

    } else if (token==Mul) {
        // dereference *<addr>
        match(Mul);
        expression(Inc);                       // dereference has the same precedence as Inc(++)

        if (expr_type>=PTR) {
            expr_type = expr_type - PTR;
        } else {
            printf("%d: Bad dereference\n", line);
            exit(-1);
        }

        *++text = (expr_type==CHAR) ? LC : LI;
    } else if (token==And) {
        // get the address of
        match(And);
        expression(Inc);

        if (*text==LC || *text==LI) {
            text--;
        } else {
            printf("$d: Bad address of\n", line);
            exit(-1);
        }

        expr_type = expr_type + PTR;
    } else if (token=-'!') {
        // not operation
        match('!');
        expression(Inc);

        // emit code, <expr>==0
        *++text = PUSH;
        *++text = IMM;
        *++text = 0;
        *++text = EQ;

        expr_type = INT;

    } else if (token=='~') {
        // bitwise not
        // (32-bit) ~a = a ^ 0xFFFF
        // (64-bit) ~a = a ^ 0xFFFFFFFF
        match('~');
        expression(Inc);

        // emit code, <expr> XOR -1
        *++text = PUSH;
        *++text = IMM;
        *++text = -1;
        *++text = XOR;

        expr_type = INT;

    } else if (token==Add) {
        // positive number
        match(Add);
        expression(Inc);
        expr_type = INT;

    } else if (token==Sub) {
        // negative number
        // use `0 - x` to represent `-x`
        match(Sub);

        if (token==Num) {
            *++text = IMM;
            *++text = -token_val;
            match(Num);

        } else {
            *++text = IMM;
            *++text = -1;
            *++text = PUSH;
            expression(Inc);
            *++text = MUL;
        }

        expr_type = INT;

    } else if (token==Inc || token==Dec) {
        // increment and decrement
        // In C, `++p` has higher precedence than `p++`
        // and for `++p`, we have to access `p` twice
        // 1. for load the value
        // 2. for storing the incremented value
        tmp = token;
        match(token);
        expression(Inc);

        if (*text==LC) {
            *text = PUSH;           // to duplicate the address
            *++text = LC;
        } else if (*text==LI) {
            *text = PUSH;           // to duplicate the address
            *++text = LI;
        } else {
            printf("%d: Bad lvalue of pre-increment\n", line);
            exit(-1);
        }
        *++text = PUSH;
        *++text = IMM;

        // deal with the cases when `p` is a pointer
        *++text = (expr_type>PTR) ? sizeof(int) : sizeof(char);
        *++text = (tmp==Inc) ? ADD : SUB;
        *++text = (expr_type==CHAR) ? SC : SI;

    }


    // BINARY OPERATORS
    while (token>=level) {
        // parse token for binary operator and postfix operator
        // handle according to current operator's precedence
        tmp = expr_type;
        
        if (token==Assign) {
            // `Assign` has the lowest precedence among binary operators
            // Consider expression `a = (expression)`, and its instructions as:
            // ----- origin -----
            // IMM <addr>
            // LC/LI
            //
            // After the expression on the right side of `=` is evaluated, the
            // result will be stored in `gpr`
            // in order to assign the value in `gpr` to the variable `a`
            // we have to rewrite the instructions into:
            // ----- rewrite -----
            // IMM <addr>
            // PUSH
            // SC/SI
            match(Assign);

            if (*text==LC || *text==LI) {
                *text = PUSH;                // save the lvalue's pointer
            } else {
                printf("%d: Bad lvalue in assignment\n", line);
                exit(-1);
            }
            expression(Assign);

            expr_type = tmp;
            *++text = (expr_type==CHAR) ? SC : SI;

        } else if (token==Cond) {
            // expr ? a : b;
            match(Cond);

            *++text = JZ;
            addr = ++text;
            expression(Assign);
            if (token==':') {
                match(':');
            } else {
                printf("%d: Missing colon in condition\n", line);
                exit(-1);
            }
            *addr = (int)(text + 3);
            *++text = JMP;
            addr = ++text;
            expression(Cond);
            *addr = (int)(text + 1);

        } else if (token==Lor) {
            // logical or:
            //   <expr1> || <expr2>
            //
            // ----- instructions -----
            //            <expr1>
            //            JNZ section2
            //            <expr2>
            // section2:
            //            ...
            match(Lor);

            *++text = JNZ;
            addr = ++text;
            expression(Lan);
            *addr = (int)(text + 1);
            expr_type = INT;

        } else if (token==Lan) {
            // logical and
            //   <expr1> && <expr2>
            //
            // ----- instructions -----
            //            <expr1>
            //            JZ section2
            //            <expr2>
            // section2:
            //            ...
            match(Lan);

            *++text = JZ;
            addr = ++text;
            expression(Or);
            *addr = (int)(text + 1);
            expr_type = INT;

    
        // MATHEMATICAL OPERATIONS
        // including |, ^, &, ==, !=, <=, >=, <, >, <<, >>, +, -, *, /, %
        // the instruction format is demonstrated below
        // bitwise or
        //   <expr1> | <expr2>
        //
        // ----- instructions -----
        // <expr1>
        // PUSH
        // <expr2>
        // OR

        } else if (token==Or) {
            // bitwise or
            match(Or);

            *++text = PUSH;
            expression(Xor);
            *++text = OR;
            expr_type = INT;

        } else if (token==Xor) {
            // bitwise xor
            match(Xor);

            *++text = PUSH;
            expression(And);
            *++text = XOR;
            expr_type = INT;

        } else if (token==And) {
            // bitwise and
            match(And);

            *++text = PUSH;
            expression(Eq);
            *++text = AND;
            expr_type = INT;

        } else if (token==Eq) {
            // equal
            match(Eq);

            *++text = PUSH;
            expression(Ne);
            *++text = EQ;
            expr_type = INT;

        } else if (token==Ne) {
            // not equal
            match(Ne);

            *++text = PUSH;
            expression(Lt);
            *++text = NE;
            expr_type = INT;

        } else if (token==Lt) {
            // less than
            match(Lt);

            *++text = PUSH;
            expression(Shl);
            *++text = LT;
            expr_type = INT;

        } else if (token==Gt) {
            // greater than
            match(Gt);

            *++text = PUSH;
            expression(Shl);
            *++text = GT;
            expr_type = INT;

        } else if (token==Le) {
            // less than or equal to
            match(Le);

            *++text = PUSH;
            expression(Shl);
            *++text = LE;
            expr_type = INT;

        } else if (token==Ge) {
            // greater than or equal to
            match(Ge);

            *++text = PUSH;
            expression(Shl);
            *++text = GE;
            expr_type = INT;

        } else if (token==Shl) {
            // shift left
            match(Shl);

            *++text = PUSH;
            expression(Add);
            *++text = SHL;
            expr_type = INT;

        } else if (token==Shr) {
            // shift right
            match(Shr);

            *++text = PUSH;
            expression(Add);
            *++text = SHR;
            expr_type = INT;

        // there are still some more important cases need to be handled
        // when it comes to addition and subtraction for pointers
        // it's equivalent to a pointer shiftment
        //
        // For example :
        // <expr1> + <expr2>
        //
        // --- normal ---    --- pointer ---
        //  <expr1>           <expr1>
        //  PUSH              PUSH
        //  <expr2>           <expr2>     |
        //  ADD               PUSH        | <expr2> * <unit>
        //                    IMM <unit>  |
        //                    MUL         |
        //                    ADD
        //
        // this means if `<expr1>` is a pointer, we have to multiply
        // `<expr2>` with the number of bytes that `<expr1>`'s type
        // occupies

        } else if (token==Add) {
            // addition
            match(Add);

            *++text = PUSH;
            expression(Mul);
            expr_type = tmp;

            if (expr_type>PTR) {
                // pointer type, `char *` is not included
                *++text = PUSH;
                *++text = IMM;
                *++text = sizeof(int);
                *++text = MUL;
            }
            *++text = ADD;

        } else if (token==Sub) {
            // subtraction
            match(Sub);

            *++text = PUSH;
            expression(Mul);
            if (tmp>PTR && tmp==expr_type) {
                // pointer subtraction
                *++text = SUB;
                *++text = PUSH;
                *++text = IMM;
                *++text = sizeof(int);
                *++text = DIV;
                expr_type = INT;

            } else if (tmp>PTR) {
                // pointer movement
                *++text = PUSH;
                *++text = IMM;
                *++text = sizeof(int);
                *++text = MUL;
                *++text = SUB;
                expr_type = tmp;

            } else {
                // numerical substraction
                *++text = SUB;
                expr_type = tmp;
            }

        } else if (token==Mul) {
            // multiplication
            match(Mul);

            *++text = PUSH;
            expression(Inc);
            *++text = MUL;
            expr_type = tmp;

        } else if (token==Div) {
            // division
            match(Div);

            *++text = PUSH;
            expression(Inc);
            *++text = DIV;
            expr_type = tmp;

        } else if (token==Mod) {
            // module
            match(Mod);

            *++text = PUSH;
            expression(Inc);
            *++text = MOD;
            expr_type = tmp;

        } else if (token==Inc || token==Dec) {
            // postfix inc(++) and dec(--)
            // we will increase the value to the variable and decrease it
            // on `gpr` to get its original value
            //
            // Comparision
            // ----- prefix -----
            // *++text = PUSH;
            // *++text = IMM;
            // *++text = (expr_type>PTR) ? sizeof(int) : sizeof(char);
            // *++text = (tmp==Inc) ? ADD : SUB;
            // *++text = (expr_type==CHAR) ? SC : SI;
            //
            // ----- postfix -----
            // *++text = PUSH;
            // *++text = IMM:
            // *++text = (expr_type>PTR) ? sizeof(int) : sizeof(char);
            // *++text = (token==Inc) ? ADD : SUB;
            // *++text = (expr_type==CHAR) ? SC : SI;
            // *++text = PUSH;                                           //
            // *++text = IMM;                                            // Inversion of inc/dec
            // *++text = (expr_type>PTR) ? sizeof(int) : sizeof(char);   //
            // *++text = (token=Inc) ? SUB : ADD;                        //

            if (*text==LI) {
                *text = PUSH;
                *++text = LI;
            } else if (*text==LC) {
                *text = PUSH;
                *++text = LC;
            } else {
                printf("%d: Bad value in increment\n", line);
                exit(-1);
            }

            *++text = PUSH;
            *++text = IMM;
            *++text = (expr_type>PTR) ? sizeof(int) :sizeof(char);
            *++text = (token==Inc) ? ADD : SUB;
            *++text = (expr_type==CHAR) ? SC : SI;
            *++text = PUSH;
            *++text = IMM;
            *++text = (expr_type>PTR) ? sizeof(int) : sizeof(char);
            *++text = (token==Inc) ? SUB : ADD;
            match(token);

        } else if (token==Brak) {
            // array access var[xx]
            match(Brak);
            
            *++text = PUSH;
            expression(Assign);
            match(']');

            if (tmp>PTR) {
                // pointer, not including `char *`
                *++text = PUSH;
                *++text = IMM;
                *++text = sizeof(int);
                *++text = MUL;
            } else if (tmp<PTR) {
                printf("%d: Pointer type expected\n", line);
                exit(-1);
            }
            expr_type = tmp - PTR;
            *++text = ADD;
            *++text = (expr_type==CHAR) ? LC : LI;

        } else {
            printf("%d: Compiling Error, token = %d\n", line, token);
            exit(-1);
        }
    }
}

void statement() {
    // if statement and while statement will cause jump between two sections
    // we declare two pointer to `section1` and `section2`
    int *section1, *section2;

    if (token==If) {
        // if (...) <statement> [else <statement>]
        //
        // 0 |  if (...)      |            <condition>
        // 1 |                |            JZ section1
        // 2 |   <statement1> |            <statement1>
        // 3 | else:          |            JMP section2
        // 4 |                | section1:
        // 5 |   <statement2> |            <statement2>
        // 6 |                | section2:

        match(If);
        match('(');
        expression(Assign);  // parse condition
        match(')');

        // emit code for if
        *++text = JZ;
        section2 = ++text;

        statement();         // parse statement
        if (token==Else) {
            match(Else);

            // emit code for section2
            *section2 = (int)(text + 3);
            *++text = JMP;
            section2 = ++text;

            statement();
        }

        *section2 = (int)(text + 1);

    } else if (token==While) {
        //                    | section1:
        //    while (<cond>)  |            <cond>
        //                    |            JZ section2
        //     <statement>    |            <statement>
        //                    |            JMP section1
        //                    | section2:

        match(While);

        section1 = text + 1;

        match('(');
        expression(Assign);
        match(')');

        *++text = JZ;
        section2 = ++text;

        statement();
        *++text = JMP;
        *++text = (int)section1;
        *section2 = (int)(text + 1);
    
    } else if (token==Return) {
        // return [ expression ]

        match(Return);

        if (token!=';') {
            expression(Assign);
        }

        match(';');

        // emit code for return
        *++text = LEV;

    } else if (token=='{') {
        // { <statement> }

        match('{');

        while (token!='}') {
            statement();
        }

        match('}');

    } else if (token==';') {
        // empty statement

        match(';');
    
    } else {
        // Assignment:     `a = b;`
        // Function call:  `func_name();`

        expression(Assign);
        match(';');
    }
}

void function_body() {
    // type func_name (...) { ... }
    // -------------------
    // {
    //   1. local declarations
    //   2. statements
    // }

    int pos_local;            // position of local variables on the stack
    int type;
    pos_local = index_of_bp;

    while (token==Int || token==Char) {
        basetype = (token==Int) ? INT : CHAR;
        match(token);

        while (token!=';') {
            type = basetype;
            while (token==Mul) {
                match(Mul);
                type = type + PTR;
            }

            if (token!=Id) {
                printf("%d: Bad local declaration\n", line);
                exit(-1);
            }
            if (curr_id[Class]==Loc) {
                printf("%d: Duplicate local declaration\n", line);
                exit(-1);
            }
            match(Id);

            curr_id[BClass] = curr_id[Class]; curr_id[Class] = Loc;
            curr_id[BType]  = curr_id[Type];  curr_id[Type]  = type;
            curr_id[BValue] = curr_id[Value]; curr_id[Value] = ++pos_local;

            if (token==',') {
                match(',');
            }
        }
        match(';');
    }

    // save the stack size for local variables
    *++text = ENT;
    *++text = pos_local - index_of_bp;

    // statements
    while (token!='}') {
        statement();
    }

    // emit code for leaving the sub function
    *++text = LEV;
}

void function_parameter() {
    int type;
    int params;  // index of current parameter
    params = 0;
    while (token!=')') {

        // int name
        type = INT;
        if (token==Int) {
            match(Int);
        } else if (token==Char) {
            type = CHAR;
            match(Char);
        }

        // pointer type
        while (token==Mul) {
            match(Mul);
            type = type + PTR;
        }

        // parameter name
        if (token!=Id) {
            printf("%d: Bad parameter declaration\n", line);
            exit(-1);
        }
        if (curr_id[Class]==Loc) {
            printf("%d: Duplicate parameter declaration\n", line);
            exit(-1);
        }
        match(Id);

        // backup the information for global variables which might be
        // shadowed by local variables
        curr_id[BClass] = curr_id[Class]; curr_id[Class] = Loc;
        curr_id[BType]  = curr_id[Type];  curr_id[Type]  = type;
        curr_id[BValue] = curr_id[Value]; curr_id[Value] = params++;

        if (token==',') {
            match(',');
        }

        index_of_bp = params + 1;
    }
}

void function_declaration() {
    // type func_name (...) { ... }

    match('(');
    function_parameter();
    match(')');

    match('{');
    function_body();
    // match('}');
    // --------------------------
    // We skip consuming '}' here, since `variable_declaration()` and `function_declaration()`
    // are parsing together in `global_declaration()`, and if '}' is consumed here, then the
    // while loop in `global_declaration()` won't be able to know that the 
    // `function_declaration()` part is finished

    // Here we try to recover the local variables of the same name as the global variables
    // before exiting a function
    curr_id = symbols;
    while (curr_id[Token]) {
        if (curr_id[Class]==Loc) {
            curr_id[Class] = curr_id[BClass];
            curr_id[Type]  = curr_id[BType];
            curr_id[Value] = curr_id[BValue];
        }
        curr_id = curr_id + IdSize;
    }
}

void enum_declaration() {
    int i;
    i = 0;
    while (token!='}') {
        if (token!=Id) {
            printf("%d: Bad enum identifier %d\n", line, token);
            exit(-1);
        }
        next();
        if (token==Assign) {
            // like {a = 10}
            next();
            if (token!=Num) {
                printf("%d: Bad enum initializer\n", line);
                exit(-1);
            }
            i = token_val;
            next();
        }
        curr_id[Class] = Num;
        curr_id[Type] = INT;
        curr_id[Value] = i++;

        if (token==',') {
            next();
        }
    }
}

void global_declaration() {
    // global_declaration    ::= enum_declaration | variable_declaration | function_declaration
    // enum_declaration      ::= 'enum' [id] '{' id ['=' 'num'] { ',' id ['=' 'num'] } '}'
    // variable_declaration  ::= type {'*'} id { ',' {'*'} id } ';'
    // function_declaration  ::= type {'*'} id '(' parameter_declaration ')' '{' body_declaration '}'

    int type;   // the actual type of variable;
    int i;

    basetype = INT;

    if (token==Enum) {
        // parse enum, this should be treated alone.
        // enum [id] { a = 10, b = 20, ... }
        match(Enum);
        if (token!='{') {
            // skip the [id] part
            match(Id);
        }
        if (token=='{') {
            // parse the assign part
            match('{');
            enum_declaration();
            match('}');
        }
        match(';');
        return ;
    }

    // parse type information
    if (token==Int) {
        match(Int);
    } else if (token==Char) {
        basetype = CHAR;
        match(Char);
    }

    while (token!=';' && token!='}') {
        type = basetype;
        while (token==Mul) {
            // parse pointer type
            // NOTE: there may exist `int ****x`, and it's supported by this compiler
            match(Mul);
            type = type + PTR;
        }

        if (token!=Id) {
            printf("%d: Bad global declaration\n", line);
            exit(-1);
        }
        if (curr_id[Class]) {
            printf("%d: Duplicate global declaration\n", line);
            exit(-1);
        }
        match(Id);
        curr_id[Type] = type;

        if (token='(') {
            // if '(' comes after the variable name
            // it is defined as a function
            curr_id[Class] = Fun;
            curr_id[Value] = (int)(text + 1); // the memory address of the function
            function_declaration();
        } else {
            // global variable otherwise
            curr_id[Class] = Glo;
            curr_id[Value] = (int)data;
            data = data + sizeof(int);
        }

        if (token==',') {
            match(',');
        }
    }
    next();
}

void program() {
    next();
    while (token>0) {
        global_declaration();
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
    int *tmp;

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

    if (!(pc = (int *)idmain[Value])) {
        printf("main() not defined\n");
        return -1;
    }

    // setup stack
    sp = (int *)((int)stack + poolsz);
    *--sp = EXIT;            // call exit if main returns
    *--sp = PUSH; tmp = sp;
    *--sp = argc;
    *--sp = (int)argv;
    *--sp = (int)tmp;

    return eval();
}