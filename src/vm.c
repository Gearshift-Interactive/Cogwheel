#include "vm.h"

#include <inttypes.h>
#include <math.h>

typedef struct {
	enum {
		VALUE_INT,
		VALUE_UINT,
		VALUE_FLOAT,
	} type;
	union {
		int64_t v_int;
		uint64_t v_uint;
		double v_float;
	};
} Value;

typedef struct {
	Value *values;
	size_t count, capacity;
} Stack;

typedef struct {
	Stack stack;
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
		printf("  %d - %"PRIi64",\n", ii, this->intConsts.items[ii]);
	printf("UINT_CONSTANTS:\n");
	da_enumerate(ui, &this->uintConsts)
		printf("  %d - %"PRIu64",\n", ui, this->uintConsts.items[ui]);
	printf("FLOAT_CONSTANTS:\n");
	da_enumerate(fi, &this->floatConsts)
		printf("  %d - %f,\n", fi, this->floatConsts.items[fi]);
	printf("CODE:\n");
	da_enumerate(ini, &this->instr)
		switch (this->instr.code[ini])
		{
#define X(NAME, ARGL) \
			case OP_##NAME: \
				printf("  %d - %s", ini, #NAME); \
				if (ARGL) \
				{ \
					printf(" - "); \
					for (size_t iini = 0; iini < ARGL; iini++) \
						printf("%X", this->instr.code[ini + 1 + iini]); \
					ini += ARGL; \
				} \
				printf("\n"); \
				break;
	OPCODE_TYPE
#undef X
		}
#undef da_enumerate
}
static void Value_print(const Value *this)
{
	switch (this->type)
	{
		case VALUE_INT:
			printf("INT(%"PRId64")\n", this->v_int);
			break;
		case VALUE_UINT:
			printf("UINT(%"PRIu64")\n", this->v_uint);
			break;
		case VALUE_FLOAT:
			printf("FLOAT(%f)\n", this->v_float);
			break;
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
static void Stack_checkCapacity(Stack *this)
{
	if (!this->values)
	{
		this->values = malloc(sizeof(Value) * 64);
		this->capacity = 64;
		return;
	}
	if (this->count < this->capacity)
		return;
	this->values = realloc(this->values, this->capacity * 2);
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
#define INFIX(TYPE, FIELD, OP)               \
    do {                                     \
        Value rhs = Stack_pop(&vm->stack);   \
        Value lhs = Stack_pop(&vm->stack);   \
        Value result = {                     \
            .type = (TYPE),                  \
            .FIELD = lhs.FIELD OP rhs.FIELD, \
        };                                   \
        Stack_push(&vm->stack, result);      \
    } while (0)
	Opcode current = (Opcode)chunk->instr.code[vm->pc];
	size_t index;
	vm->pc++;
	switch (current)
	{
		case OP_EXIT:
			vm->retCode = Stack_pop(&vm->stack).v_int;
			vm->done = true;
			break;
		case OP_CLOAD_INT:
			index = readSizeT(vm, chunk);
			Stack_push(&vm->stack, (Value){
				.type = VALUE_INT,
				.v_int = chunk->intConsts.items[index],
			});
			break;
		case OP_CLOAD_UINT:
			index = readSizeT(vm, chunk);
			Stack_push(&vm->stack, (Value){
				.type = VALUE_UINT,
				.v_uint = chunk->uintConsts.items[index],
			});
			break;
		case OP_CLOAD_FLOAT:
			index = readSizeT(vm, chunk);
			Stack_push(&vm->stack, (Value){
				.type = VALUE_FLOAT,
				.v_float = chunk->floatConsts.items[index],
			});
			break;
		case OP_ADD_INT:
			INFIX(VALUE_INT, v_int, +);
			break;
		case OP_ADD_UINT:
			INFIX(VALUE_UINT, v_uint, +);
			break;
		case OP_ADD_FLOAT:
			INFIX(VALUE_FLOAT, v_float, +);
			break;
		case OP_SUB_INT:
			INFIX(VALUE_INT, v_int, -);
			break;
		case OP_SUB_UINT:
			INFIX(VALUE_UINT, v_uint, -);
			break;
		case OP_SUB_FLOAT:
			INFIX(VALUE_FLOAT, v_float, -);
			break;
		case OP_DIV_INT:
			INFIX(VALUE_INT, v_int, /);
			break;
		case OP_DIV_UINT:
			INFIX(VALUE_UINT, v_uint, /);
			break;
		case OP_DIV_FLOAT:
			INFIX(VALUE_FLOAT, v_float, /);
			break;
		case OP_MUL_INT:
			INFIX(VALUE_INT, v_int, *);
			break;
		case OP_MUL_UINT:
			INFIX(VALUE_UINT, v_uint, *);
			break;
		case OP_MUL_FLOAT:
			INFIX(VALUE_FLOAT, v_float, *);
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
				.type = VALUE_UINT,
				.v_uint = (uint64_t)Stack_pop(&vm->stack).v_int,
			});
			break;
		case OP_CAST_ITOF:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_FLOAT,
				.v_float = (double)Stack_pop(&vm->stack).v_int,
			});
			break;
		case OP_CAST_UTOI:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_INT,
				.v_int = (int64_t)Stack_pop(&vm->stack).v_uint,
			});
			break;
		case OP_CAST_UTOF:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_FLOAT,
				.v_float = (double)Stack_pop(&vm->stack).v_uint,
			});
			break;
		case OP_CAST_FTOI:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_INT,
				.v_int = (int64_t)Stack_pop(&vm->stack).v_float,
			});
			break;
		case OP_CAST_FTOU:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_UINT,
				.v_uint = (uint64_t)Stack_pop(&vm->stack).v_float,
			});
			break;
		case OP_NEG_INT:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_INT,
				.v_int = -(int64_t)Stack_pop(&vm->stack).v_int,
			});
			break;
		case OP_NEG_FLOAT:
			Stack_push(&vm->stack, (Value){
				.type = VALUE_FLOAT,
				.v_float = -(double)Stack_pop(&vm->stack).v_float,
			});
			break;
		default:
			nob_log(ERROR, "Unsupported operation at %d", vm->pc - 1);
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
	return vm.retCode;
}
