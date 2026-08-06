#include "stdio.h"

#include "lexer.h"

int main(void)
{
	TokenStream tokens = tokenize("let a := 10");
    return 0;
}
