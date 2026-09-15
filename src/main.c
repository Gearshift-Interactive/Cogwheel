#include "stdio.h"

#include "lexer.h"
#include "parser.h"
#include "vm.h"
#include "compiler.h"
#include "semantic_analyzer.h"
#include "error.h"
#include "bank.h"
#include "globals.h"

#ifdef COG_STANDALONE

void printInt_f(Stack *stack, __attribute__((unused)) size_t argc)
{
	printf("%ld", Stack_pop(stack).v_int);
}
void printFloat_f(Stack *stack, __attribute__((unused)) size_t argc)
{
	printf("%f", Stack_pop(stack).v_float);
}
void printChar_f(Stack *stack, __attribute__((unused)) size_t argc)
{
	printf("%lc", Stack_pop(stack).v_char);
}

void buildStd(Globals *globals)
{
	GlobalValue printInt = {
		.value = (Value) {
			.v_nfunc = printInt_f,
#ifdef DEBUG
			.type = VALUE_NFUNC,
#endif
		},
		.type = calloc(1, sizeof(Type)),
		.name = "putInt",
	};
	printInt.type->kind = TYPE_FUNCTION;
	printInt.type->function.isNative = true;
	printInt.type->function.retType = &TYPE_VOID_OBJ;
	ArgInfo argInfo_printInt = {
		.type = &TYPE_INT_OBJ,
	};
	da_append(&printInt.type->function.args, argInfo_printInt);
	Bank_handOff(printInt.type);
	Bank_handOff(printInt.type->function.args.items);
	da_append(globals, printInt);

	GlobalValue printFloat = {
		.value = (Value) {
			.v_nfunc = printFloat_f,
#ifdef DEBUG
			.type = VALUE_NFUNC,
#endif
		},
		.type = calloc(1, sizeof(Type)),
		.name = "putFloat",
	};
	printFloat.type->kind = TYPE_FUNCTION;
	printFloat.type->function.isNative = true;
	printFloat.type->function.retType = &TYPE_VOID_OBJ;
	ArgInfo argInfo_printFloat = {
		.type = &TYPE_FLOAT_OBJ,
	};
	da_append(&printFloat.type->function.args, argInfo_printFloat);
	Bank_handOff(printFloat.type);
	Bank_handOff(printFloat.type->function.args.items);
	da_append(globals, printFloat);

	GlobalValue printChar = {
		.value = (Value) {
			.v_nfunc = printChar_f,
#ifdef DEBUG
			.type = VALUE_NFUNC,
#endif
		},
		.type = calloc(1, sizeof(Type)),
		.name = "putChar",
	};
	printChar.type->kind = TYPE_FUNCTION;
	printChar.type->function.isNative = true;
	printChar.type->function.retType = &TYPE_VOID_OBJ;
	ArgInfo argInfo_printChar = {
		.type = &TYPE_CHAR_OBJ,
	};
	da_append(&printChar.type->function.args, argInfo_printChar);
	Bank_handOff(printChar.type);
	Bank_handOff(printChar.type->function.args.items);
	da_append(globals, printChar);

	GlobalValue PI = {
		.value = (Value) {
			.v_float = 3.1415926535897932384626433832795028841971693993751058209749445923078164062,
#ifdef DEBUG
			.type = VALUE_NFUNC,
#endif
		},
		.type = &TYPE_FLOAT_OBJ,
		.name = "PI",
	};
	da_append(globals, PI);
}

int main(int argc, char **argv)
{
	Bank_init();
	Globals globals = {0};
	buildStd(&globals);
	assert(argc == 2);
	String_Builder sb = {0};
	read_entire_file(argv[1], &sb);
	da_append(&sb, 0);

	//////////////////
	//// TOKENIZE ////
	//////////////////
	TokenStream tokens = tokenize(nob_sv_from_parts(sb.items, sb.count), argv[1]);

#	ifdef DEBUG
	da_foreach(Token, i, &tokens)
	{
		Token_print(*i);
		printf("\n");
	}
#	endif

	///////////////
	//// PARSE ////
	///////////////
	Node *ast = parse(tokens);

#	ifdef DEBUG
	printf("//// AST ////\n");
	Node_print(ast);
	printf("\n");
	fflush(stdout);
#	endif

	//////////////////////////////
	//// SEMANTICALLY ANALYZE ////
	//////////////////////////////
	analyzeAndMark(&ast, &globals);

#	ifdef DEBUG
	printf("//// MARKED AST ////\n");
	Node_print(ast);
	printf("\n");
	fflush(stdout);
#	endif
	if (errorOccured)
	{
		free(sb.items);
		Node_free(ast);
		Bank_freeAll();
		free(globals.items);
		return EXIT_FAILURE;
	}

	/////////////////
	//// COMPILE ////
	/////////////////
	Chunk code = compile(ast);

#	ifdef DEBUG
	printf("//// BYTECODE ////\n");
	Chunk_print(&code);
	fflush(stdout);
	printf("//// EXECUTION ////\n");
#	endif
	free(sb.items);

	// Chunk_free(&code);
	// return 0;

	/////////////
	//// RUN ////
	/////////////
	int result = run(&code, &globals);

	Bank_freeAll();
	free(globals.items);
	printf("RESULT: %d\n", result);
	return result;
}

// i had hard time reading this shit

#endif
