#include "stdio.h"

#include "lexer.h"
#include "parser.h"
#include "vm.h"
#include "compiler.h"
#include "semantic_analyzer.h"

#ifdef COG_STANDALONE

int main(int argc, char **argv)
{
	assert(argc == 2);
	TokenStream tokens = tokenize(argv[1]);
#ifdef DEBUG
	printf("//// TOKENS ////\n");
	da_foreach(Token, i, &tokens)
	{
		Token_print(*i);
		printf("\n");
	}
#endif
	Node *ast = parse(tokens);
#ifdef DEBUG
	printf("//// AST ////\n");
	Node_print(ast);
	printf("\n");
#endif
	analyzeAndMark(ast);
#ifdef DEBUG
	printf("//// MARKED AST ////\n");
	Node_print(ast);
	printf("\n");
#endif
	Chunk code = compile(ast);
#ifdef DEBUG
	printf("//// BYTECODE ////\n");
	Chunk_print(&code);
#endif
    return run(&code);
}

#endif
