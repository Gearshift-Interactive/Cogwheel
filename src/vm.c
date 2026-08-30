#include "vm.h"
#include "error.h"
#include "value.h"
#include "gc.h"

#include <math.h>
#include <inttypes.h>

#ifdef DEBUG
#	include "parser.h"
#endif


typedef struct Scope {
	struct Scope *parent;
	size_t size;
	Value values[];
} Scope;

static void Scope_free(Scope *this)
{
	if (this->parent)
		Scope_free(this->parent);
	free(this);
}

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
	GC gc;
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
			/* displaying args in little endidan */ \
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
			PANIC("Out of memory");
		this->capacity = 64;
		return;
	}
	if (this->count < this->capacity)
		return;
	this->values = realloc(this->values, sizeof(Value) * this->capacity * 2);
	if (!this->values)
		PANIC("Out of memory");
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
		PANIC("Stack is empty");
	return this->values[--this->count];
}
static Value Stack_current(Stack *this)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values[this->count - 1];
}
static Value *Stack_currentPtr(Stack *this)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values + (this->count - 1);
}
static Value *Stack_countBack(Stack *this, size_t count)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values + (this->count - count);
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
	size_t arg1, arg2;
	Scope *scope;
	vm->pc++;
	Value *curValue;
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
			.type = VALUE_INT,
#endif
			.v_int = chunk->intConsts.items[arg1],
		});
		break;
	case OP_CLOAD_UINT:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_UINT,
#endif
			.v_uint = chunk->uintConsts.items[arg1],
		});
		break;
	case OP_CLOAD_FLOAT:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_FLOAT,
#endif
			.v_float = chunk->floatConsts.items[arg1],
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
		{
			Value rhs = Stack_pop(&vm->stack);
			Value lhs = Stack_pop(&vm->stack);
			if (rhs.v_int == 0)
				PANIC("Devision by zero");
			Value result = {
#ifdef DEBUG
				.type = VALUE_INT,
#endif
				.v_int = lhs.v_int / rhs.v_int,
			};
			Stack_push(&vm->stack, result);
		}
		break;
	case OP_DIV_UINT:
		{
			Value rhs = Stack_pop(&vm->stack);
			Value lhs = Stack_pop(&vm->stack);
			if (rhs.v_uint == 0)
				PANIC("Devision by zero");
			Value result = {
#ifdef DEBUG
				.type = VALUE_UINT,
#endif
				.v_uint = lhs.v_uint / rhs.v_uint,
			};
			Stack_push(&vm->stack, result);
		}
		break;
	case OP_DIV_FLOAT:
		{
			Value rhs = Stack_pop(&vm->stack);
			Value lhs = Stack_pop(&vm->stack);
			if (rhs.v_float == 0)
				PANIC("Devision by zero");
			Value result = {
#ifdef DEBUG
				.type = VALUE_FLOAT,
#endif
				.v_float = lhs.v_float / rhs.v_float,
			};
			Stack_push(&vm->stack, result);
		}
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
	case OP_POW_INT: {
		int64_t rhs = Stack_pop(&vm->stack).v_int;
		int64_t lhs = Stack_pop(&vm->stack).v_int;
		Value result = {
#ifdef DEBUG
			.type = VALUE_INT,
#endif
			.v_int = round(pow(lhs, rhs)),
		};
		Stack_push(&vm->stack, result);
		}
		break;
	case OP_POW_UINT: {
		uint64_t rhs = Stack_pop(&vm->stack).v_uint;
		uint64_t lhs = Stack_pop(&vm->stack).v_uint;
		Value result = {
#ifdef DEBUG
			.type = VALUE_UINT,
#endif
			.v_uint = round(pow(lhs, rhs)),
		};
		Stack_push(&vm->stack, result);
		}
		break;
	case OP_POW_FLOAT: {
		double rhs = Stack_pop(&vm->stack).v_float;
		double lhs = Stack_pop(&vm->stack).v_float;
		Value result = {
#ifdef DEBUG
			.type = VALUE_FLOAT,
#endif
			.v_float = pow(lhs, rhs),
		};
		Stack_push(&vm->stack, result);
		}
		break;
	case OP_CAST_ITOU:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_UINT,
#endif
			.v_uint = (uint64_t)Stack_pop(&vm->stack).v_int,
		});
		break;
	case OP_CAST_ITOF:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_FLOAT,
#endif
			.v_float = (double)Stack_pop(&vm->stack).v_int,
		});
		break;
	case OP_CAST_UTOI:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_INT,
#endif
			.v_int = (int64_t)Stack_pop(&vm->stack).v_uint,
		});
		break;
	case OP_CAST_UTOF:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_FLOAT,
#endif
			.v_float = (double)Stack_pop(&vm->stack).v_uint,
		});
		break;
	case OP_CAST_FTOI:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_INT,
#endif
			.v_int = (int64_t)Stack_pop(&vm->stack).v_float,
		});
		break;
	case OP_CAST_FTOU:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_UINT,
#endif
			.v_uint = (uint64_t)Stack_pop(&vm->stack).v_float,
		});
		break;
	case OP_NEG_INT:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_INT,
#endif
			.v_int = -(int64_t)Stack_pop(&vm->stack).v_int,
		});
		break;
	case OP_NEG_FLOAT:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_FLOAT,
#endif
			.v_float = -(double)Stack_pop(&vm->stack).v_float,
		});
		break;
	case OP_POP:
		Stack_pop(&vm->stack);
		break;
	case OP_SCOPE_ENTER:
		arg1 = readSizeT(vm, chunk);
		scope = vm->scope;
		vm->scope = calloc(1, sizeof(*(vm->scope)) + sizeof(Value[arg1]));
		vm->scope->parent = scope;
		break;
	case OP_SCOPE_EXIT:
		scope = vm->scope;
		vm->scope = scope->parent;
		free(scope);
		break;
	case OP_SCOPE_READ:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		scope = vm->scope;
		for (size_t i = 0; i < arg1; i++)
			scope = scope->parent;
		Stack_push(&vm->stack, scope->values[arg2]);
		break;
	case OP_SCOPE_WRITE:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		scope = vm->scope;
		for (size_t i = 0; i < arg1; i++)
			scope = scope->parent;
		scope->values[arg2] = Stack_current(&vm->stack);
		break;
	case OP_CLOAD_TRUE:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_BOOL,
#endif
			.v_bool = true,
		});
		break;
	case OP_CLOAD_FALSE:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_BOOL,
#endif
			.v_bool = false,
		});
		break;
	case OP_OR:
		INFIX(VALUE_BOOL, v_bool, ||);
		break;
	case OP_AND:
		INFIX(VALUE_BOOL, v_bool, &&);
		break;
	case OP_JUMPF:
		arg1 = readSizeT(vm, chunk);
		vm->pc += arg1 - sizeof(size_t);
		break;
	case OP_JUMPF_IFN:
		arg1 = readSizeT(vm, chunk);
		if (!Stack_pop(&vm->stack).v_bool)
			vm->pc += arg1 - sizeof(size_t);
		break;
#ifdef DEBUG
#	define INFIX_LOG(FIELD, OP)               \
		do {                                     \
			Value rhs = Stack_pop(&vm->stack);   \
			Value lhs = Stack_pop(&vm->stack);   \
			Value result = {                     \
				.type = VALUE_BOOL,        \
				.v_bool = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Stack_push(&vm->stack, result);      \
		} while (0)
#else
#	define INFIX_LOG(FIELD, OP)               \
		do {                                     \
			Value rhs = Stack_pop(&vm->stack);   \
			Value lhs = Stack_pop(&vm->stack);   \
			Value result = {                     \
				.v_bool = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Stack_push(&vm->stack, result);      \
		} while (0)
#endif
	case OP_EQ_INT:
		INFIX_LOG(v_int, ==);
		break;
	case OP_EQ_UINT:
		INFIX_LOG(v_uint, ==);
		break;
	case OP_EQ_FLOAT:
		INFIX_LOG(v_float, ==);
		break;
	case OP_NEQ_INT:
		INFIX_LOG(v_int, !=);
		break;
	case OP_NEQ_UINT:
		INFIX_LOG(v_uint, !=);
		break;
	case OP_NEQ_FLOAT:
		INFIX_LOG(v_float, !=);
		break;
	case OP_GT_INT:
		INFIX_LOG(v_int, >);
		break;
	case OP_GT_UINT:
		INFIX_LOG(v_uint, >);
		break;
	case OP_GT_FLOAT:
		INFIX_LOG(v_float, >);
		break;
	case OP_LT_INT:
		INFIX_LOG(v_int, <);
		break;
	case OP_LT_UINT:
		INFIX_LOG(v_uint, <);
		break;
	case OP_LT_FLOAT:
		INFIX_LOG(v_float, <);
		break;
	case OP_EGT_INT:
		INFIX_LOG(v_int, >=);
		break;
	case OP_EGT_UINT:
		INFIX_LOG(v_uint, >=);
		break;
	case OP_EGT_FLOAT:
		INFIX_LOG(v_float, >=);
		break;
	case OP_ELT_INT:
		INFIX_LOG(v_int, <=);
		break;
	case OP_ELT_UINT:
		INFIX_LOG(v_uint, <=);
		break;
	case OP_ELT_FLOAT:
		INFIX_LOG(v_float, <=);
		break;
	case OP_NOT:
		curValue = Stack_currentPtr(&vm->stack);
		curValue->v_bool = !curValue->v_bool;
		break;
	case OP_JUMPB:
		arg1 = readSizeT(vm, chunk);
		vm->pc -= arg1 + sizeof(size_t);
		break;
	case OP_POP_IFPR:
		if (vm->stack.count)
			Stack_pop(&vm->stack);
		break;
	case OP_GC_ALLOC:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, GC_alloc(&vm->gc, arg1));
		break;
	case OP_GC_ACCESS:
		PANIC("UNIMPLEMENTED");
		break;
	case OP_GC_ACCESS_FROMSTACK: {
		Value index = Stack_pop(&vm->stack);
		Value target = Stack_pop(&vm->stack);
		HeapObject *obj = target.v_heap;
		if (index.v_uint < obj->count)
			Stack_push(&vm->stack, obj->items[index.v_uint]);
		else
			PANIC("Buffer overflow");
	} break;
	case OP_GC_ASSIGN: {
		Value value = Stack_pop(&vm->stack);
		Value *target = Stack_currentPtr(&vm->stack);
		arg1 = readSizeT(vm, chunk);
		HeapObject *obj = target->v_heap;
		if (arg1 < obj->count)
			obj->items[arg1] = value;
		else
			PANIC("Buffer overflow");
	} break;
	case OP_GC_ASSIGN_FROMSTACK: {
		Value value = Stack_pop(&vm->stack);
		Value index = Stack_pop(&vm->stack);
		Value *target = Stack_currentPtr(&vm->stack);
		HeapObject *obj = target->v_heap;
		if (index.v_uint < obj->count)
			obj->items[index.v_uint] = value;
		else
			PANIC("Buffer overflow");
	} break;
	case OP_GC_ASSIGNCOPY: {
		Value *target = Stack_countBack(&vm->stack, 2);
		Value *value = Stack_countBack(&vm->stack, 1);
		arg1 = readSizeT(vm, chunk);
		HeapObject *obj = target->v_heap;
		if (arg1 < obj->count)
			obj->items[arg1] = *value;
		else
			PANIC("Buffer overflow");
	} break;
	case OP_GC_SIZEOF: {
		Value value = Stack_pop(&vm->stack);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_UINT,
#endif
			.v_uint = ((HeapObject*)value.v_heap)->count,
		});
	} break;
	case OP_CLOAD_NULL:
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_NULL,
#endif
			.isNull = true,
		});
		break;
	case OP_OPT_UNWRAP: {
		Value *value = Stack_currentPtr(&vm->stack);
		if (value->isNull)
			PANIC("Unwrap failed");
	} break;
	case OP_OPT_CHECK: {
		Value *value = Stack_currentPtr(&vm->stack);
		*value = (Value){
#ifdef DEBUG
			.type = VALUE_BOOL,
#endif
			.v_bool = !value->isNull,
		};
	} break;
	default:
		PANIC("Unsupported operation at %ld", vm->pc - 1);
		break;
	}
#undef INFIX
#undef INFIX_LOG
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
	GC_freeAll(&vm.gc);
	Chunk_free(chunk);
	if (vm.scope) Scope_free(vm.scope);
	return vm.retCode;
}
