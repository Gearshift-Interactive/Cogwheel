#include "stdio.h"

#include "lexer.h"
#include "parser.h"
#include "vm.h"
#include "compiler.h"
#include "semantic_analyzer.h"
#include "error.h"

#ifdef COG_STANDALONE

int main(int argc, char **argv)
{
	assert(argc == 2);
	String_Builder sb = {0};
	read_entire_file(argv[1], &sb);
	da_append(&sb, 0);
	TokenStream tokens = tokenize(nob_sv_from_parts(sb.items, sb.count), argv[1]);
#	ifdef DEBUG
	da_foreach(Token, i, &tokens)
	{
		Token_print(*i);
		printf("\n");
	}
#	endif
	Node *ast = parse(tokens);
#	ifdef DEBUG
	printf("//// AST ////\n");
	Node_print(ast);
	printf("\n");
#	endif
	analyzeAndMark(&ast);
#	ifdef DEBUG
	printf("//// MARKED AST ////\n");
	Node_print(ast);
	printf("\n");
#	endif
	if (errorOccured)
	{
		free(sb.items);
		Node_free(ast);
		return EXIT_FAILURE;
	}
	Chunk code = compile(ast);
#	ifdef DEBUG
	printf("//// BYTECODE ////\n");
	Chunk_print(&code);
	printf("//// EXECUTION ////\n");
#	endif
	free(sb.items);
	// Chunk_free(&code);
	// return 0;
	int result = run(&code);
	printf("RESULT: %d\n", result);
	return result;
}

#endif
