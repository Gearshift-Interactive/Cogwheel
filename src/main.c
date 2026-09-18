#include "stdio.h"

#include "lexer.h"
#include "parser.h"
#include "vm.h"
#include "compiler.h"
#include "semantic_analyzer.h"
#include "error.h"
#include "bank.h"
#include "globals.h"
#include "module.h"

#ifdef COG_STANDALONE

void printInt_f(Cog_Stack *stack, __attribute__((unused)) size_t argc)
{
	printf("%ld", Cog_Stack_pop(stack).v_int);
}
void printFloat_f(Cog_Stack *stack, __attribute__((unused)) size_t argc)
{
	printf("%f", Cog_Stack_pop(stack).v_float);
}
void printChar_f(Cog_Stack *stack, __attribute__((unused)) size_t argc)
{
	Cog_Value v = Cog_Stack_pop(stack);
	uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&v.v_char];
	for (size_t i = 0; i < charSize; ++i)
		putchar((v.v_char >> (i * 8)) & 0xff);
}

void buildStd(Cog_Globals *globals)
{
	Cog_GlobalValue printInt = {
		.value = (Cog_Value) {
			.v_nfunc = printInt_f,
#ifdef COG_DEBUG
			.type = COG_VALUE_NFUNC,
#endif
		},
		.type = calloc(1, sizeof(Cog_Type)),
		.name = "putInt",
	};
	printInt.type->kind = COG_TYPE_FUNCTION;
	printInt.type->function.isNative = true;
	printInt.type->function.retType = &COG_TYPE_VOID_OBJ;
	Cog_ArgInfo argInfo_printInt = {
		.type = &COG_TYPE_INT_OBJ,
	};
	da_append(&printInt.type->function.args, argInfo_printInt);
	Cog_Bank_handOff(printInt.type);
	Cog_Bank_handOff(printInt.type->function.args.items);
	da_append(globals, printInt);

	Cog_GlobalValue printFloat = {
		.value = (Cog_Value) {
			.v_nfunc = printFloat_f,
#ifdef COG_DEBUG
			.type = COG_VALUE_NFUNC,
#endif
		},
		.type = calloc(1, sizeof(Cog_Type)),
		.name = "putFloat",
	};
	printFloat.type->kind = COG_TYPE_FUNCTION;
	printFloat.type->function.isNative = true;
	printFloat.type->function.retType = &COG_TYPE_VOID_OBJ;
	Cog_ArgInfo argInfo_printFloat = {
		.type = &COG_TYPE_FLOAT_OBJ,
	};
	da_append(&printFloat.type->function.args, argInfo_printFloat);
	Cog_Bank_handOff(printFloat.type);
	Cog_Bank_handOff(printFloat.type->function.args.items);
	da_append(globals, printFloat);

	Cog_GlobalValue printChar = {
		.value = (Cog_Value) {
			.v_nfunc = printChar_f,
#ifdef COG_DEBUG
			.type = COG_VALUE_NFUNC,
#endif
		},
		.type = calloc(1, sizeof(Cog_Type)),
		.name = "putChar",
	};
	printChar.type->kind = COG_TYPE_FUNCTION;
	printChar.type->function.isNative = true;
	printChar.type->function.retType = &COG_TYPE_VOID_OBJ;
	Cog_ArgInfo argInfo_printChar = {
		.type = &COG_TYPE_CHAR_OBJ,
	};
	da_append(&printChar.type->function.args, argInfo_printChar);
	Cog_Bank_handOff(printChar.type);
	Cog_Bank_handOff(printChar.type->function.args.items);
	da_append(globals, printChar);

	Cog_GlobalValue PI = {
		.value = (Cog_Value) {
			.v_float = 3.1415926535897932384626433832795028841971693993751058209749445923078164062,
#ifdef COG_DEBUG
			.type = COG_VALUE_NFUNC,
#endif
		},
		.type = &COG_TYPE_FLOAT_OBJ,
		.name = "PI",
	};
	da_append(globals, PI);
}

int main(int argc, char **argv)
{
	Cog_Bank_init();
	Cog_Globals globals = {0};
	buildStd(&globals);
	assert(argc == 2);
	Cog_Module *module = Cog_loadModule(argv[1]);
#ifdef COG_DEBUG
	Cog_Module_print(module);
#endif
	Cog_Node *ast = Cog_finalizeModule(module, &globals);
	Cog_Module_freeAll();
	Cog_Chunk code = Cog_compile(ast);
#	ifdef COG_DEBUG
	printf("//// BYTECODE ////\n");
	Cog_Chunk_print(&code);
	fflush(stdout);
#	endif
#	ifdef COG_DEBUG
	printf("//// EXECUTION ////\n");
#	endif
	int result = Cog_run(&code, &globals);
	Cog_Bank_freeAll();
	free(globals.items);
	printf("RESULT: %d\n", result);
	return result;
}

#endif
