#include "stdio.h"

#include "lexer.h"

#ifdef COG_STANDALONE

int main(void)
{
	TokenStream tokens = tokenize("let a := 10");
	TokenStream_free(&tokens);
	Token token;
	for (;;)
	{
		token = TokenStream_consume(&tokens);
		Token_print(token);
		printf("\n");
		if (!TokenStream_peek(&tokens))
			break;
	}
    return 0;
}

#endif
