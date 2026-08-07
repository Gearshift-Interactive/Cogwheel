#include "stdio.h"

#include "lexer.h"
#include "parser.h"

#ifdef COG_STANDALONE

int main(void)
{
	TokenStream tokens = tokenize("a = (10 + 2 * 3);");
	da_foreach(Token, i, &tokens)
	{
		Token_print(*i);
		printf("\n");
	}
	Node *ast = parse(tokens);
	Node_print(ast);
	Node_free(ast);
    return 0;
}

#endif
