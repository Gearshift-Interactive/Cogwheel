#include "stdio.h"

#include "lexer.h"
#include "parser.h"

#ifdef COG_STANDALONE

int main(void)
{
	TokenStream tokens = tokenize("a = 10 + 2 * 3; a = 12;");
#ifdef DEBUG
	da_foreach(Token, i, &tokens)
	{
		Token_print(*i);
		printf("\n");
	}
#endif
	Node *ast = parse(tokens);
#ifdef DEBUG
	Node_print(ast);
#endif
	Node_free(ast);
    return 0;
}

#endif
