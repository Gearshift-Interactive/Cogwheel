#include "nob.h"

#include "compilation/compiler.h"
#include "vm/vm.h"
#include "bank.h"
#include "globals.h"
#include "analysis/module.h"
#include "time.h"

static void printInt_f(VM *vm, __attribute__((unused)) size_t argc)
{
	printf("%ld", VM_pop(vm)->v_int);
}
static void printFloat_f(VM *vm, __attribute__((unused)) size_t argc)
{
	printf("%f", VM_pop(vm)->v_float);
}
static void printChar_f(VM *vm, __attribute__((unused)) size_t argc)
{
	Cog_Value *v = VM_pop(vm);
	uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&v->v_char];
	for (size_t i = 0; i < charSize; ++i)
		putchar((v->v_char >> (i * 8)) & 0xff);
}

static void buildStd(Cog_Globals *globals)
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
		.type = &COG_TYPE_FLOAT_OBJ,
		.isMutable = false,
		.value = {
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = 3.14159
		}
	};
	da_append(globals, PI);
}

#ifdef COG_STANDALONE

static void printHelp(void)
{
	printf(
		"Usage: cogwheel [--help] [--time] <file>\n"
		"Options:\n"
		"  --help   Display this information.\n"
		"  --time   Measure program execution time.\n"
	);
}
static int runModule(char *file, bool benchmark)
{
	Cog_Globals globals = {0};
	buildStd(&globals);
	Cog_Module *module = Cog_loadModule(file);
#ifdef COG_DEBUG
	nob_da_foreach(Cog_Module*, module, &Cog_allModules)
		Cog_Module_print(*module);
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
	clock_t start, end;
	if (benchmark)
		start = clock();
	int result = Cog_run(&code, &globals);
	if (benchmark)
		end = clock();
	Cog_Bank_freeAll();
	free(globals.items);
#	ifdef COG_DEBUG
	printf("[Result: %d]\n", result);
#	endif
	if (benchmark)
		printf("[Program executed in %f seconds]\n", (float)(end - start) / CLOCKS_PER_SEC);
	return result;
}

int main(int argc, char **argv)
{
	Cog_Bank_init();
	char *module = NULL;
	bool benchmark, help = false;
	for (size_t i = 1; i < (size_t)argc; i++)
	{
		if (module)
		{
			printf("Unexpected CLI argument after file \"%s\"\n", argv[i]);
			return 1;
		}
		if (!strcmp(argv[i], "--help"))
			help = true;
		else if (!strcmp(argv[i], "--time"))
			benchmark = true;
		else
			module = argv[i];
	}
	if (help)
		printHelp();
	if (!module)
	{
		printf("File not specified\n");
		return help? 0 : 1;
	}
	return runModule(module, benchmark);
}

#endif
