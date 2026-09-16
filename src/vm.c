#include "vm.h"
#include "error.h"
#include "stack.h"

#include <math.h>
#include <inttypes.h>

#ifdef DEBUG
#	include "parser.h"
#endif

typedef struct Scope {
	struct Scope *parent;
	size_t size;
	size_t refCount;
	Value values[];
} Scope;

typedef struct Closure {
	Chunk *chunk;
	Scope *env;
} Closure;


static void Scope_retain(Scope *this)
{
	this->refCount++;
}
static void Scope_release(Scope *this)
{
	if (!this) return;
	if (--this->refCount == 0)
	{
		if (this->parent)
			Scope_release(this->parent);
		free(this);
	}
}

typedef struct {
	bool marked;
	size_t count;
	Value items[];
} HeapObject;

typedef struct {
	struct {
		HeapObject **items;
		size_t count, capacity;
	} allObjects;
	struct {
		Closure **items;
		size_t count, capacity;
	} closures;
} GC;

static Value GC_alloc(GC *this, size_t valueCount)
{
	HeapObject *obj = calloc(1, sizeof(*obj) + sizeof(Value) * valueCount);
	obj->count = valueCount;
	da_append(&this->allObjects, obj);
	return (Value){
#ifdef DEBUG
		.type = VALUE_HEAP,
#endif
		.isHeap = true,
		.v_heap = obj,
	};
}
static Value GC_allocClosure(GC *this, Chunk *chunk, Scope *env)
{
	Closure *closure = calloc(1, sizeof *closure);
	closure->chunk = chunk;
	closure->env = env;
	da_append(&this->closures, closure);
	return (Value) {
#ifdef DEBUG
		.type = VALUE_FUNC,
#endif
		.isHeap = true,
		.v_func = closure,
	};
}
static void GC_freeAll(GC *this)
{
	da_foreach(HeapObject*, obj, &this->allObjects)
		free(*obj);
	if (this->allObjects.items)
		free(this->allObjects.items);
	da_foreach(Closure*, obj, &this->closures)
	{
		Scope_release((*obj)->env);
		free(*obj);
	}
	if (this->closures.items)
		free(this->closures.items);
}

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
	FREE_IF_PRESENT(this->charConsts.items);
#undef FREE_IF_PRESENT
	if (this->functions.items)
	{
		da_foreach(Chunk, chunk, &this->functions)
			Chunk_free(chunk);
		free(this->functions.items);
	}
}
static void printLevel(size_t level)
{
	for (size_t i = 0; i < level; i++)
		printf("    ");
}
static void Chunk_printImpl(const Chunk *this, size_t level)
{
#define da_enumerate(I, ARR) for (size_t I = 0; I < (ARR)->count; I++)
	printLevel(level);
	printf("INT_CONSTANTS:\n");
	da_enumerate(ii, &this->intConsts)
	{
		printLevel(level);
		printf("  %ld - %"PRIi64",\n", ii, this->intConsts.items[ii]);
	}
	printLevel(level);
	printf("UINT_CONSTANTS:\n");
	da_enumerate(ui, &this->uintConsts)
	{
		printLevel(level);
		printf("  %ld - %"PRIu64",\n", ui, this->uintConsts.items[ui]);
	}
	printLevel(level);
	printf("FLOAT_CONSTANTS:\n");
	da_enumerate(fi, &this->floatConsts)
	{
		printLevel(level);
		printf("  %ld - %f,\n", fi, this->floatConsts.items[fi]);
	}
	printLevel(level);
	printf("CHAR_CONSTANTS:\n");
	da_enumerate(ci, &this->charConsts)
	{
		printLevel(level);
		printf("  %ld - ", ci);
		uint32_t value = this->charConsts.items[ci];
		uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&value];
		for (size_t bi = 0; bi < charSize; ++bi)
			putchar((value >> (bi * 8)) & 0xff);
		putchar('\n');
	}
	printLevel(level);
	printf("FUNCTIONS:\n");
	da_enumerate(fni, &this->functions)
	{
		printLevel(level);
		Chunk_printImpl(&this->functions.items[fni], level + 1);
	}
	printLevel(level);
	printf("CODE:\n");
	da_enumerate(ini, &this->instr)
	{
		printLevel(level);
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
	}
#undef da_enumerate
}
void Chunk_print(const Chunk *this)
{
	Chunk_printImpl(this, 0);
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
static void call(VM *, const Closure *, size_t);
static void Scope_enter(VM *vm, size_t varCount)
{
	Scope *parent = vm->scope;
	vm->scope = calloc(1, sizeof(*(vm->scope)) + sizeof(Value[varCount]));
	Scope_retain(vm->scope);
	if (parent)
		Scope_retain(parent);
	vm->scope->parent = parent;
}
static void Scope_exit(VM *vm)
{
	Scope *scope = vm->scope;
	vm->scope = scope->parent;
	Scope_release(scope);
}
static Value scopeRead(VM *vm, size_t depth, size_t id)
{
	Scope *scope = vm->scope;
	for (size_t i = 0; i < depth; i++)
		scope = scope->parent;
	return scope->values[id];
}
static void scopeWrite(VM *vm, size_t depth, size_t id, Value value)
{
	Scope *scope = vm->scope;
	for (size_t i = 0; i < depth; i++)
		scope = scope->parent;
	scope->values[id] = value;
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
				PANIC("Division by zero");
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
				PANIC("Division by zero");
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
				PANIC("Division by zero");
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
		Scope_enter(vm, arg1);
		break;
	case OP_SCOPE_EXIT:
		Scope_exit(vm);
		break;
	case OP_SCOPE_READ:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		Stack_push(&vm->stack, scopeRead(vm, arg1, arg2));
		break;
	case OP_SCOPE_WRITE:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		scopeWrite(vm, arg1, arg2, Stack_current(&vm->stack));
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
	case OP_CLOAD_FUNC: {
		size_t chunkIndex = readSizeT(vm, chunk);
		Chunk *funcChunk = &chunk->functions.items[chunkIndex];
		// Scope_release(funcChunk->vmData);
		// funcChunk->vmData = vm->scope;
		if (vm->scope)
			Scope_retain(vm->scope);
		Stack_push(&vm->stack, GC_allocClosure(&vm->gc, funcChunk, vm->scope));
	} break;
	case OP_CALL: {
		size_t argc = readSizeT(vm, chunk);
		Value function = Stack_pop(&vm->stack);
		call(vm, function.v_func, argc);
	} break;
	case OP_GC_ALLOC_FROMSTACK: {
		Value size = Stack_pop(&vm->stack);
		Stack_push(&vm->stack, GC_alloc(&vm->gc, size.v_uint));
	} break;
	case OP_GC_FILL: {
		Value value = Stack_pop(&vm->stack);
		HeapObject *obj = Stack_currentPtr(&vm->stack)->v_heap;
		for (size_t i = 0; i < obj->count; i++)
			obj->items[i] = value;
	} break;
	case OP_CALLN: {
		Value function = Stack_pop(&vm->stack);
		function.v_nfunc(&vm->stack, readSizeT(vm, chunk));
	} break;
	case OP_CLOAD_CHAR:
		arg1 = readSizeT(vm, chunk);
		Stack_push(&vm->stack, (Value){
#ifdef DEBUG
			.type = VALUE_CHAR,
#endif
			.v_char = chunk->charConsts.items[arg1],
		});
		break;
	default:
		PANIC("Unsupported operation at %ld", vm->pc - 1);
		break;
	}
#undef INFIX
#undef INFIX_LOG
}

static void execute(VM *vm, const Chunk *chunk)
{
	while (!vm->done && vm->pc < chunk->instr.count)
	{
#ifdef DEBUG
		printf("%zu ", vm->pc);
		Stack_print(&vm->stack);
		fflush(stdout);
#endif
		runInstruction(vm, chunk);
	}
}
static void call(VM *vm, const Closure *closure, size_t argc)
{
	size_t oldPc    = vm->pc;
	Scope *oldScope = vm->scope;

	vm->pc    = 0;
	vm->scope = closure->env;

	Scope_enter(vm, argc);
	for (int i = argc - 1; i >= 0; i--)
		scopeWrite(vm, 0, i, Stack_pop(&vm->stack));
	execute(vm, closure->chunk);
	if (vm->done) return;
	Scope_exit(vm);

	vm->pc    = oldPc;
	vm->scope = oldScope;
}
int run(const Chunk *chunk, Globals *globals)
{
	VM vm = {0};
	Scope_enter(&vm, globals->count);
	for (size_t i = 0; i < globals->count; i++)
		scopeWrite(&vm, 0, i, globals->items[i].value);
	execute(&vm, chunk);
	Stack_free(&vm.stack);
	GC_freeAll(&vm.gc);
	Chunk_free(chunk);
	while (vm.scope) Scope_exit(&vm);
	return vm.retCode;
}
