#include "stdio.h"

#include "lexer.h"
#include "parser.h"
#include "vm.h"
#include "compiler.h"
#include "semantic_analyzer.h"

#ifdef COG_STANDALONE

int main(int argc, char **argv)
{
	TokenStream tokens = tokenize(argv[1]);
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
	printf("\n");
#endif
	analyzeAndMark(ast);
#ifdef DEBUG
	Node_print(ast);
	printf("\n");
#endif
	Chunk code = compile(ast);
#ifdef DEBUG
	Chunk_print(&code);
#endif
    return run(&code);
}

#endif
