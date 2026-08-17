#include "vm.h"

#include <inttypes.h>
#include <math.h>

#ifdef DEBUG
#	include "parser.h"
#endif

typedef
#ifdef DEBUG
  struct {
	Type *type;
#endif
	union {
		int64_t v_int;
		uint64_t v_uint;
		double v_float;
		bool v_bool;
#ifdef DEBUG
	};
#endif
} Value;

typedef struct {
	size_t size;
	Value values[];
} Scope;

typedef struct {
	Value *values;
	size_t count, capacity;
} Stack;

typedef struct {
	Stack stack;
	Scope *scope;
	size_t pc;
	int retCode;
	bool done;
} VM;

void Chunk_free(const Chunk *this)
{
#define FREE_IF_PRESENT(PTR) if (PTR) free(PTR)
	FREE_IF_PRESENT(this->intConsts.items);
	FREE_IF_PRESENT(this->uintConsts.items);
	FREE_IF_PRESENT(this->floatConsts.items);
	FREE_IF_PRESENT(this->instr.code);
#undef FREE_IF_PRESENT
}
void Chunk_print(const Chunk *this)
{
#define da_enumerate(I, ARR) for (size_t I = 0; I < (ARR)->count; I++)
	printf("INT_CONSTANTS:\n");
	da_enumerate(ii, &this->intConsts)
		printf("  %ld - %"PRIi64",\n", ii, this->intConsts.items[ii]);
	printf("UINT_CONSTANTS:\n");
	da_enumerate(ui, &this->uintConsts)
		printf("  %ld - %"PRIu64",\n", ui, this->uintConsts.items[ui]);
	printf("FLOAT_CONSTANTS:\n");
	da_enumerate(fi, &this->floatConsts)
		printf("  %ld - %f,\n", fi, this->floatConsts.items[fi]);
	printf("CODE:\n");
	da_enumerate(ini, &this->instr)
		switch (this->instr.code[ini])
		{
#define X(NAME, ARGL) \
	case OP_##NAME: \
		printf("  %ld - %s", ini, #NAME); \
		if (ARGL) \
		{ \
			size_t argl = ARGL; \
			printf(" - "); \
			for (size_t iini = 0; iini < argl; iini++) \
				printf("%X", this->instr.code[ini + 1 + iini]); \
			ini += argl; \
		} \
		printf("\n"); \
		break;
	OPCODE_TYPE
#undef X
		}
#undef da_enumerate
}
#ifdef DEBUG
static void Value_print(const Value *this) {
	printf("%s", Type_toString(this->type));
	switch (this->type->kind)
	{
		case TYPE_INT:
			printf("(%"PRId64")\n", this->v_int);
			break;
		case TYPE_UINT:
			printf("(%"PRIu64")\n", this->v_uint);
			break;
		case TYPE_FLOAT:
			printf("(%f)\n", this->v_float);
			break;
		case TYPE_BOOL:
			printf("(%s)\n", this->v_bool ? "true" : "false");
			break;
		case TYPE_VOID:
			exit(EXIT_FAILURE);
	}
}
static void Stack_print(const Stack *this)
{
	printf("--STACK--\n");
	if (this->count)
		for (size_t i = 0; i < this->count; i++)
		{
			printf("%zu - ", i);
			Value_print(this->values + i);
		}
	else
		printf("  *empty*\n");
}
#endif
static void Stack_checkCapacity(Stack *this)
{
	if (!this->values)
	{
		this->values = malloc(sizeof(Value) * 64);
		if (!this->values)
		{
			nob_log(ERROR, "Out of memory");
			exit(EXIT_FAILURE);
		}
		this->capacity = 64;
		return;
	}
	if (this->count < this->capacity)
		return;
	this->values = realloc(this->values, sizeof(Value) * this->capacity * 2);
	if (!this->values)
	{
		nob_log(ERROR, "Out of memory");
		exit(EXIT_FAILURE);
	}
	this->capacity *= 2;
}
static void Stack_push(Stack *this, Value value)
{
	Stack_checkCapacity(this);
	this->values[this->count++] = value;
}
static Value Stack_pop(Stack *this)
{
	if (this->count < 1)
	{
		nob_log(ERROR, "Stack is empty");
		exit(EXIT_FAILURE);
	}
	return this->values[--this->count];
}
static Value Stack_current(Stack *this)
{
	if (this->count < 1)
	{
		nob_log(ERROR, "Stack is empty");
		exit(EXIT_FAILURE);
	}
	return this->values[this->count - 1];
}
static void Stack_free(const Stack *this)
{
	if (this->values)
		free(this->values);
}
static size_t readSizeT(VM *vm, const Chunk *chunk)
{
	size_t value;
    memcpy(&value, &chunk->instr.code[vm->pc], sizeof(value));
    vm->pc += sizeof(value);
    return value;
}
static void runInstruction(VM *vm, const Chunk *chunk)
{
#ifdef DEBUG
#	define INFIX(TYPE, FIELD, OP)               \
		do {                                     \
			Value rhs = Stack_pop(&vm->stack);   \
			Value lhs = Stack_pop(&vm->stack);   \
			Value result = {                     \
				.type = (TYPE),                  \
				.FIELD = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Stack_push(&vm->stack, result);      \
		} while (0)
#else
#	define INFIX(TYPE, FIELD, OP)               \
		do {                                     \
			Value rhs = Stack_pop(&vm->stack);   \
			Value lhs = Stack_pop(&vm->stack);   \
			Value result = {                     \
				.FIELD = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Stack_push(&vm->stack, result);      \
		} while (0)
#endif
	Opcode current = (Opcode)chunk->instr.code[vm->pc];
	size_t arg1;
	vm->pc++;
	switch (current)
	{
	case OP_EXIT:
		vm->retCode = Stack_pop(&vm->stack).v_int;
		vm->done = true;
		break;
	case OP_CLOAD_INT:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_INT_OBJ,
#endif
			.v_int = chunk->intConsts.items[arg1],
		});
		break;
	case OP_CLOAD_UINT:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_UINT_OBJ,
#endif
			.v_uint = chunk->uintConsts.items[arg1],
		});
		break;
	case OP_CLOAD_FLOAT:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_FLOAT_OBJ,
#endif
			.v_float = chunk->floatConsts.items[arg1],
		});
		break;
	case OP_ADD_INT:
		INFIX(&TYPE_INT_OBJ, v_int, +);
		break;
	case OP_ADD_UINT:
		INFIX(&TYPE_UINT_OBJ, v_uint, +);
		break;
	case OP_ADD_FLOAT:
		INFIX(&TYPE_FLOAT_OBJ, v_float, +);
		break;
	case OP_SUB_INT:
		INFIX(&TYPE_INT_OBJ, v_int, -);
		break;
	case OP_SUB_UINT:
		INFIX(&TYPE_UINT_OBJ, v_uint, -);
		break;
	case OP_SUB_FLOAT:
		INFIX(&TYPE_FLOAT_OBJ, v_float, -);
		break;
	case OP_DIV_INT:
		INFIX(&TYPE_INT_OBJ, v_int, /);
		break;
	case OP_DIV_UINT:
		INFIX(&TYPE_UINT_OBJ, v_uint, /);
		break;
	case OP_DIV_FLOAT:
		INFIX(&TYPE_FLOAT_OBJ, v_float, /);
		break;
	case OP_MUL_INT:
		INFIX(&TYPE_INT_OBJ, v_int, *);
		break;
	case OP_MUL_UINT:
		INFIX(&TYPE_UINT_OBJ, v_uint, *);
		break;
	case OP_MUL_FLOAT:
		INFIX(&TYPE_FLOAT_OBJ, v_float, *);
		break;
	case OP_POW_INT:
		nob_log(ERROR, "power is unsopported yet");
		exit(EXIT_FAILURE);
		break;
	case OP_POW_UINT:
		nob_log(ERROR, "power is unsopported yet");
		exit(EXIT_FAILURE);
		break;
	case OP_POW_FLOAT:
		nob_log(ERROR, "power is unsopported yet");
		exit(EXIT_FAILURE);
		break;
	case OP_CAST_ITOU:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_UINT_OBJ,
#endif
			.v_uint = (uint64_t)Stack_pop(&vm->stack).v_int,
		});
		break;
	case OP_CAST_ITOF:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_FLOAT_OBJ,
#endif
			.v_float = (double)Stack_pop(&vm->stack).v_int,
		});
		break;
	case OP_CAST_UTOI:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_INT_OBJ,
#endif
			.v_int = (int64_t)Stack_pop(&vm->stack).v_uint,
		});
		break;
	case OP_CAST_UTOF:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_FLOAT_OBJ,
#endif
			.v_float = (double)Stack_pop(&vm->stack).v_uint,
		});
		break;
	case OP_CAST_FTOI:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_INT_OBJ,
#endif
			.v_int = (int64_t)Stack_pop(&vm->stack).v_float,
		});
		break;
	case OP_CAST_FTOU:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_UINT_OBJ,
#endif
			.v_uint = (uint64_t)Stack_pop(&vm->stack).v_float,
		});
		break;
	case OP_NEG_INT:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_INT_OBJ,
#endif
			.v_int = -(int64_t)Stack_pop(&vm->stack).v_int,
		});
		break;
	case OP_NEG_FLOAT:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_FLOAT_OBJ,
#endif
			.v_float = -(double)Stack_pop(&vm->stack).v_float,
		});
		break;
	case OP_POP:
		Stack_pop(&vm->stack);
		break;
	case OP_SCOPE_ENTER:
		arg1 = readSizeT(vm, chunk);
		vm->scope = calloc(1, sizeof(*(vm->scope)) + sizeof(Value[arg1]));
		break;
	case OP_SCOPE_EXIT:
		free(vm->scope);
		vm->scope = NULL;
		break;
	case OP_SCOPE_READ:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, vm->scope->values[arg1]);
		break;
	case OP_SCOPE_WRITE:
		arg1 = readSizeT(vm, chunk);
		vm->scope->values[arg1] = Stack_current(&vm->stack);
		break;
	case OP_CLOAD_TRUE:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_BOOL_OBJ,
#endif
			.v_bool = true,
		});
		break;
	case OP_CLOAD_FALSE:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = &TYPE_BOOL_OBJ,
#endif
			.v_bool = false,
		});
		break;
	case OP_OR:
		INFIX(&TYPE_BOOL_OBJ, v_bool, ||);
		break;
	case OP_AND:
		INFIX(&TYPE_BOOL_OBJ, v_bool, &&);
		break;
	default:
		nob_log(ERROR, "Unsupported operation at %ld", vm->pc - 1);
		exit(EXIT_FAILURE);
		break;
	}
#undef INFIX
}
int run(const Chunk *chunk)
{
	VM vm = {0};
	while (!vm.done && vm.pc < chunk->instr.count)
	{
#ifdef DEBUG
		Stack_print(&vm.stack);
#endif
		runInstruction(&vm, chunk);
	}
	Stack_free(&vm.stack);
	Chunk_free(chunk);
	if (vm.scope) free(vm.scope);
	return vm.retCode;
}
