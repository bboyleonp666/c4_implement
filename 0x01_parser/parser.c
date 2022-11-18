#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#define int long long // Work with 64-bit machines

int poolsz;           // default size of text/data/stack
int line;             // line number
char *src, *old_src;  // pointer to source code string
int token;            // current token

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

    program();
    return eval();
}