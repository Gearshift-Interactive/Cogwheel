#include "vm.h"
#include "error.h"
#include "stack.h"

#include <math.h>
#include <inttypes.h>

#ifdef COG_DEBUG
#	include "parser.h"
#endif

typedef struct Scope {
	struct Scope *parent;
	size_t size;
	size_t refCount;
	Cog_Value values[];
} Scope;

typedef struct Cog_Closure {
	Cog_Chunk *chunk;
	Scope *env;
} Cog_Closure;


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
	Cog_Value *items;
} HeapObject;

typedef struct {
	struct {
		HeapObject **items;
		size_t count, capacity;
	} allObjects;
	struct {
		Cog_Closure **items;
		size_t count, capacity;
	} closures;
} GC;

static Cog_Value GC_alloc(GC *this, size_t valueCount)
{
	HeapObject *obj = calloc(1, sizeof *obj);
	obj->count = valueCount;
	obj->items = calloc(valueCount, sizeof(Cog_Value));
	da_append(&this->allObjects, obj);
	return (Cog_Value){
#ifdef COG_DEBUG
		.type = COG_VALUE_HEAP,
#endif
		.isHeap = true,
		.v_heap = obj,
	};
}
static Cog_Value GC_allocClosure(GC *this, Cog_Chunk *chunk, Scope *env)
{
	Cog_Closure *closure = calloc(1, sizeof *closure);
	closure->chunk = chunk;
	closure->env = env;
	da_append(&this->closures, closure);
	return (Cog_Value) {
#ifdef COG_DEBUG
		.type = COG_VALUE_FUNC,
#endif
		.isHeap = true,
		.v_func = closure,
	};
}
static void GC_freeAll(GC *this)
{
	da_foreach(HeapObject*, obj, &this->allObjects)
	{
		free((*obj)->items);
		free(*obj);
	}
	if (this->allObjects.items)
		free(this->allObjects.items);
	da_foreach(Cog_Closure*, obj, &this->closures)
	{
		Scope_release((*obj)->env);
		free(*obj);
	}
	if (this->closures.items)
		free(this->closures.items);
}

typedef struct {
	Cog_Stack stack;
	Scope *scope;
	size_t pc;
	int retCode;
	bool done;
	GC gc;
} VM;

void Cog_Chunk_free(const Cog_Chunk *this)
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
		da_foreach(Cog_Chunk, chunk, &this->functions)
			Cog_Chunk_free(chunk);
		free(this->functions.items);
	}
}
static void printLevel(size_t level)
{
	for (size_t i = 0; i < level; i++)
		printf("    ");
}
static void Chunk_printImpl(const Cog_Chunk *this, size_t level)
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
#define COG_X(NAME, ARGL) \
	case COG_OP_##NAME: \
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
	COG_OPCODE_TYPE
#undef COG_X
		}
	}
#undef da_enumerate
}
void Cog_Chunk_print(const Cog_Chunk *this)
{
	Chunk_printImpl(this, 0);
}
static void Stack_free(const Cog_Stack *this)
{
	if (this->values)
		free(this->values);
}
static size_t readSizeT(VM *vm, const Cog_Chunk *chunk)
{
	size_t value;
    memcpy(&value, &chunk->instr.code[vm->pc], sizeof(value));
    vm->pc += sizeof(value);
    return value;
}
static void call(VM *, const Cog_Closure *, size_t);
static void Scope_enter(VM *vm, size_t varCount)
{
	Scope *parent = vm->scope;
	vm->scope = calloc(1, sizeof(*(vm->scope)) + sizeof(Cog_Value[varCount]));
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
static Cog_Value scopeRead(VM *vm, size_t depth, size_t id)
{
	Scope *scope = vm->scope;
	for (size_t i = 0; i < depth; i++)
		scope = scope->parent;
	return scope->values[id];
}
static void scopeWrite(VM *vm, size_t depth, size_t id, Cog_Value value)
{
	Scope *scope = vm->scope;
	for (size_t i = 0; i < depth; i++)
		scope = scope->parent;
	scope->values[id] = value;
}
static Cog_Value cstrToCogstr(VM *vm, const char *cstr, size_t length)
{
	struct {
		Cog_Value *items;
		size_t count, capacity;
	} resultb = {0};
	for(size_t currentByte = 0; currentByte < length; currentByte++)
	{
		const uint8_t *s = (const uint8_t *)cstr + currentByte;
		size_t charSize = nob_bytes_for_utf8[*s];
		uint32_t resultData = 0;
		for (size_t i = 0; i < charSize; ++i)
			resultData |= (uint32_t)s[i] << (i * 8);
		currentByte += charSize - 1;
		Cog_Value character = {
#ifdef COG_DEBUG
			.type = COG_VALUE_CHAR,
#endif
			.v_char = resultData,
		};
		da_append(&resultb, character);
	}
	Cog_Value result = GC_alloc(&vm->gc, resultb.count);
	memcpy(((HeapObject*)result.v_heap)->items, resultb.items, resultb.count * sizeof *resultb.items);
	free(resultb.items);
	return result;
}
static void runInstruction(VM *vm, const Cog_Chunk *chunk)
{
#ifdef COG_DEBUG
#	define INFIX(TYPE, FIELD, OP)               \
		do {                                     \
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value result = {                     \
				.type = (TYPE),                  \
				.FIELD = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Cog_Stack_push(&vm->stack, result);      \
		} while (0)
#else
#	define INFIX(TYPE, FIELD, OP)               \
		do {                                     \
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value result = {                     \
				.FIELD = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Cog_Stack_push(&vm->stack, result);      \
		} while (0)
#endif
	Cog_Opcode current = (Cog_Opcode)chunk->instr.code[vm->pc];
	size_t arg1, arg2;
	vm->pc++;
	Cog_Value *curValue;
	switch (current)
	{
	case COG_OP_EXIT:
		vm->retCode = Cog_Stack_pop(&vm->stack).v_int;
		vm->done = true;
		break;
	case COG_OP_CLOAD_INT:
		arg1 = readSizeT(vm, chunk);
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_INT,
#endif
			.v_int = chunk->intConsts.items[arg1],
		});
		break;
	case COG_OP_CLOAD_UINT:
		arg1 = readSizeT(vm, chunk);
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = chunk->uintConsts.items[arg1],
		});
		break;
	case COG_OP_CLOAD_FLOAT:
		arg1 = readSizeT(vm, chunk);
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = chunk->floatConsts.items[arg1],
		});
		break;
	case COG_OP_ADD_INT:
		INFIX(COG_VALUE_INT, v_int, +);
		break;
	case COG_OP_ADD_UINT:
		INFIX(COG_VALUE_UINT, v_uint, +);
		break;
	case COG_OP_ADD_FLOAT:
		INFIX(COG_VALUE_FLOAT, v_float, +);
		break;
	case COG_OP_SUB_INT:
		INFIX(COG_VALUE_INT, v_int, -);
		break;
	case COG_OP_SUB_UINT:
		INFIX(COG_VALUE_UINT, v_uint, -);
		break;
	case COG_OP_SUB_FLOAT:
		INFIX(COG_VALUE_FLOAT, v_float, -);
		break;
	case COG_OP_DIV_INT:
		{
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);
			if (rhs.v_int == 0)
				COG_PANIC("Division by zero");
			Cog_Value result = {
#ifdef COG_DEBUG
				.type = COG_VALUE_INT,
#endif
				.v_int = lhs.v_int / rhs.v_int,
			};
			Cog_Stack_push(&vm->stack, result);
		}
		break;
	case COG_OP_DIV_UINT:
		{
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);
			if (rhs.v_uint == 0)
				COG_PANIC("Division by zero");
			Cog_Value result = {
#ifdef COG_DEBUG
				.type = COG_VALUE_UINT,
#endif
				.v_uint = lhs.v_uint / rhs.v_uint,
			};
			Cog_Stack_push(&vm->stack, result);
		}
		break;
	case COG_OP_DIV_FLOAT:
		{
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);
			if (rhs.v_float == 0)
				COG_PANIC("Division by zero");
			Cog_Value result = {
#ifdef COG_DEBUG
				.type = COG_VALUE_FLOAT,
#endif
				.v_float = lhs.v_float / rhs.v_float,
			};
			Cog_Stack_push(&vm->stack, result);
		}
		break;
	case COG_OP_MUL_INT:
		INFIX(COG_VALUE_INT, v_int, *);
		break;
	case COG_OP_MUL_UINT:
		INFIX(COG_VALUE_UINT, v_uint, *);
		break;
	case COG_OP_MUL_FLOAT:
		INFIX(COG_VALUE_FLOAT, v_float, *);
		break;
	case COG_OP_POW_INT: {
		int64_t rhs = Cog_Stack_pop(&vm->stack).v_int;
		int64_t lhs = Cog_Stack_pop(&vm->stack).v_int;
		Cog_Value result = {
#ifdef COG_DEBUG
			.type = COG_VALUE_INT,
#endif
			.v_int = round(pow(lhs, rhs)),
		};
		Cog_Stack_push(&vm->stack, result);
		}
		break;
	case COG_OP_POW_UINT: {
		uint64_t rhs = Cog_Stack_pop(&vm->stack).v_uint;
		uint64_t lhs = Cog_Stack_pop(&vm->stack).v_uint;
		Cog_Value result = {
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = round(pow(lhs, rhs)),
		};
		Cog_Stack_push(&vm->stack, result);
		}
		break;
	case COG_OP_POW_FLOAT: {
		double rhs = Cog_Stack_pop(&vm->stack).v_float;
		double lhs = Cog_Stack_pop(&vm->stack).v_float;
		Cog_Value result = {
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = pow(lhs, rhs),
		};
		Cog_Stack_push(&vm->stack, result);
		}
		break;
	case COG_OP_CAST_ITOU:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = (uint64_t)Cog_Stack_pop(&vm->stack).v_int,
		});
		break;
	case COG_OP_CAST_ITOF:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = (double)Cog_Stack_pop(&vm->stack).v_int,
		});
		break;
	case COG_OP_CAST_UTOI:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_INT,
#endif
			.v_int = (int64_t)Cog_Stack_pop(&vm->stack).v_uint,
		});
		break;
	case COG_OP_CAST_UTOF:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = (double)Cog_Stack_pop(&vm->stack).v_uint,
		});
		break;
	case COG_OP_CAST_FTOI:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_INT,
#endif
			.v_int = (int64_t)Cog_Stack_pop(&vm->stack).v_float,
		});
		break;
	case COG_OP_CAST_FTOU:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = (uint64_t)Cog_Stack_pop(&vm->stack).v_float,
		});
		break;
	case COG_OP_NEG_INT:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_INT,
#endif
			.v_int = -(int64_t)Cog_Stack_pop(&vm->stack).v_int,
		});
		break;
	case COG_OP_NEG_FLOAT:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = -(double)Cog_Stack_pop(&vm->stack).v_float,
		});
		break;
	case COG_OP_POP:
		Cog_Stack_pop(&vm->stack);
		break;
	case COG_OP_SCOPE_ENTER:
		arg1 = readSizeT(vm, chunk);
		Scope_enter(vm, arg1);
		break;
	case COG_OP_SCOPE_EXIT:
		Scope_exit(vm);
		break;
	case COG_OP_SCOPE_READ:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		Cog_Stack_push(&vm->stack, scopeRead(vm, arg1, arg2));
		break;
	case COG_OP_SCOPE_WRITE:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		scopeWrite(vm, arg1, arg2, Cog_Stack_current(&vm->stack));
		break;
	case COG_OP_CLOAD_TRUE:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = true,
		});
		break;
	case COG_OP_CLOAD_FALSE:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = false,
		});
		break;
	case COG_OP_OR:
		INFIX(COG_VALUE_BOOL, v_bool, ||);
		break;
	case COG_OP_AND:
		INFIX(COG_VALUE_BOOL, v_bool, &&);
		break;
	case COG_OP_JUMPF:
		arg1 = readSizeT(vm, chunk);
		vm->pc += arg1 - sizeof(size_t);
		break;
	case COG_OP_JUMPF_IFN:
		arg1 = readSizeT(vm, chunk);
		if (!Cog_Stack_pop(&vm->stack).v_bool)
			vm->pc += arg1 - sizeof(size_t);
		break;
#ifdef COG_DEBUG
#	define COG_INFIX_LOG(FIELD, OP)               \
		do {                                     \
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value result = {                     \
				.type = COG_VALUE_BOOL,        \
				.v_bool = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Cog_Stack_push(&vm->stack, result);      \
		} while (0)
#else
#	define COG_INFIX_LOG(FIELD, OP)               \
		do {                                     \
			Cog_Value rhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value lhs = Cog_Stack_pop(&vm->stack);   \
			Cog_Value result = {                     \
				.v_bool = lhs.FIELD OP rhs.FIELD, \
			};                                   \
			Cog_Stack_push(&vm->stack, result);      \
		} while (0)
#endif
	case COG_OP_EQ_INT:
		COG_INFIX_LOG(v_int, ==);
		break;
	case COG_OP_EQ_UINT:
		COG_INFIX_LOG(v_uint, ==);
		break;
	case COG_OP_EQ_FLOAT:
		COG_INFIX_LOG(v_float, ==);
		break;
	case COG_OP_NEQ_INT:
		COG_INFIX_LOG(v_int, !=);
		break;
	case COG_OP_NEQ_UINT:
		COG_INFIX_LOG(v_uint, !=);
		break;
	case COG_OP_NEQ_FLOAT:
		COG_INFIX_LOG(v_float, !=);
		break;
	case COG_OP_GT_INT:
		COG_INFIX_LOG(v_int, >);
		break;
	case COG_OP_GT_UINT:
		COG_INFIX_LOG(v_uint, >);
		break;
	case COG_OP_GT_FLOAT:
		COG_INFIX_LOG(v_float, >);
		break;
	case COG_OP_LT_INT:
		COG_INFIX_LOG(v_int, <);
		break;
	case COG_OP_LT_UINT:
		COG_INFIX_LOG(v_uint, <);
		break;
	case COG_OP_LT_FLOAT:
		COG_INFIX_LOG(v_float, <);
		break;
	case COG_OP_EGT_INT:
		COG_INFIX_LOG(v_int, >=);
		break;
	case COG_OP_EGT_UINT:
		COG_INFIX_LOG(v_uint, >=);
		break;
	case COG_OP_EGT_FLOAT:
		COG_INFIX_LOG(v_float, >=);
		break;
	case COG_OP_ELT_INT:
		COG_INFIX_LOG(v_int, <=);
		break;
	case COG_OP_ELT_UINT:
		COG_INFIX_LOG(v_uint, <=);
		break;
	case COG_OP_ELT_FLOAT:
		COG_INFIX_LOG(v_float, <=);
		break;
	case COG_OP_NOT:
		curValue = Cog_Stack_currentPtr(&vm->stack);
		curValue->v_bool = !curValue->v_bool;
		break;
	case COG_OP_JUMPB:
		arg1 = readSizeT(vm, chunk);
		vm->pc -= arg1 + sizeof(size_t);
		break;
	case COG_OP_POP_IFPR:
		if (vm->stack.count)
			Cog_Stack_pop(&vm->stack);
		break;
	case COG_OP_GC_ALLOC:
		arg1 = readSizeT(vm, chunk);
		Cog_Stack_push(&vm->stack, GC_alloc(&vm->gc, arg1));
		break;
	case COG_OP_GC_ACCESS:
		COG_PANIC("UNIMPLEMENTED");
		break;
	case COG_OP_GC_ACCESS_FROMSTACK: {
		Cog_Value index = Cog_Stack_pop(&vm->stack);
		Cog_Value target = Cog_Stack_pop(&vm->stack);
		HeapObject *obj = target.v_heap;
		if (index.v_uint < obj->count)
			Cog_Stack_push(&vm->stack, obj->items[index.v_uint]);
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_ASSIGN: {
		Cog_Value value = Cog_Stack_pop(&vm->stack);
		Cog_Value *target = Cog_Stack_currentPtr(&vm->stack);
		arg1 = readSizeT(vm, chunk);
		HeapObject *obj = target->v_heap;
		if (arg1 < obj->count)
			obj->items[arg1] = value;
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_ASSIGN_FROMSTACK: {
		Cog_Value value = Cog_Stack_pop(&vm->stack);
		Cog_Value index = Cog_Stack_pop(&vm->stack);
		Cog_Value *target = Cog_Stack_currentPtr(&vm->stack);
		HeapObject *obj = target->v_heap;
		if (index.v_uint < obj->count)
			obj->items[index.v_uint] = value;
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_ASSIGNCOPY: {
		Cog_Value *target = Cog_Stack_countBack(&vm->stack, 2);
		Cog_Value *value = Cog_Stack_countBack(&vm->stack, 1);
		arg1 = readSizeT(vm, chunk);
		HeapObject *obj = target->v_heap;
		if (arg1 < obj->count)
			obj->items[arg1] = *value;
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_SIZEOF: {
		Cog_Value value = Cog_Stack_pop(&vm->stack);
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = ((HeapObject*)value.v_heap)->count,
		});
	} break;
	case COG_OP_CLOAD_NULL:
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_NULL,
#endif
			.isNull = true,
		});
		break;
	case COG_OP_OPT_UNWRAP: {
		Cog_Value *value = Cog_Stack_currentPtr(&vm->stack);
		if (value->isNull)
			COG_PANIC("Unwrap failed");
	} break;
	case COG_OP_OPT_CHECK: {
		Cog_Value *value = Cog_Stack_currentPtr(&vm->stack);
		*value = (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = !value->isNull,
		};
	} break;
	case COG_OP_CLOAD_FUNC: {
		size_t chunkIndex = readSizeT(vm, chunk);
		Cog_Chunk *funcChunk = &chunk->functions.items[chunkIndex];
		// Scope_release(funcChunk->vmData);
		// funcChunk->vmData = vm->scope;
		if (vm->scope)
			Scope_retain(vm->scope);
		Cog_Stack_push(&vm->stack, GC_allocClosure(&vm->gc, funcChunk, vm->scope));
	} break;
	case COG_OP_CALL: {
		size_t argc = readSizeT(vm, chunk);
		Cog_Value function = Cog_Stack_pop(&vm->stack);
		call(vm, function.v_func, argc);
	} break;
	case COG_OP_GC_ALLOC_FROMSTACK: {
		Cog_Value size = Cog_Stack_pop(&vm->stack);
		Cog_Stack_push(&vm->stack, GC_alloc(&vm->gc, size.v_uint));
	} break;
	case COG_OP_GC_FILL: {
		Cog_Value value = Cog_Stack_pop(&vm->stack);
		HeapObject *obj = Cog_Stack_currentPtr(&vm->stack)->v_heap;
		for (size_t i = 0; i < obj->count; i++)
			obj->items[i] = value;
	} break;
	case COG_OP_CALLN: {
		Cog_Value function = Cog_Stack_pop(&vm->stack);
		function.v_nfunc(&vm->stack, readSizeT(vm, chunk));
	} break;
	case COG_OP_CLOAD_CHAR:
		arg1 = readSizeT(vm, chunk);
		Cog_Stack_push(&vm->stack, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_CHAR,
#endif
			.v_char = chunk->charConsts.items[arg1],
		});
		break;
	case COG_OP_GC_REALLOC: {
		Cog_Value fillValue = Cog_Stack_pop(&vm->stack);
		Cog_Value newSize = Cog_Stack_pop(&vm->stack);
		Cog_Value *array = Cog_Stack_currentPtr(&vm->stack);
		HeapObject *obj = array->v_heap;
		if (newSize.v_uint == obj->count)
			break;
		size_t oldSize = obj->count;
		obj->items = realloc(obj->items, newSize.v_uint * sizeof *obj->items);
		obj->count = newSize.v_uint;
		if (oldSize > obj->count)
			break;
		for (size_t i = oldSize; i < obj->count; i++)
			obj->items[i] = fillValue;
	} break;
	case COG_OP_GC_CONCAT: {
		HeapObject *obj2 = Cog_Stack_pop(&vm->stack).v_heap;
		HeapObject *obj1 = Cog_Stack_pop(&vm->stack).v_heap;
		Cog_Value result = GC_alloc(&vm->gc, obj1->count + obj2->count);
		HeapObject *resultHeap = result.v_heap;
		for (size_t i = 0; i < obj1->count; i++)
			resultHeap->items[i] = obj1->items[i];
		for (size_t i = 0; i < obj2->count; i++)
			resultHeap->items[obj1->count + i] = obj2->items[i];
		Cog_Stack_push(&vm->stack, result);
	} break;
	case COG_OP_GC_REPEAT: {
		Cog_Value repeatTimes = Cog_Stack_pop(&vm->stack);
		HeapObject *obj = Cog_Stack_pop(&vm->stack).v_heap;
		Cog_Value result = GC_alloc(&vm->gc, obj->count * repeatTimes.v_uint);
		HeapObject *resultHeap = result.v_heap;
		for (size_t i = 0; i < resultHeap->count; i++)
			resultHeap->items[i] = obj->items[i % obj->count];
		Cog_Stack_push(&vm->stack, result);
	} break;
	case COG_OP_TOSTRING_INT: {
		const int64_t value = Cog_Stack_pop(&vm->stack).v_int;
		const size_t charCount = snprintf(NULL, 0, "%"PRId64, value) + 1;
		char *const string = calloc(charCount, sizeof(char));
		const size_t length = snprintf(string, charCount, "%"PRId64, value);
		const Cog_Value result = cstrToCogstr(vm, string, length);
		free(string);
		Cog_Stack_push(&vm->stack, result);
	} break;
	case COG_OP_TOSTRING_UINT: {
		const uint64_t value = Cog_Stack_pop(&vm->stack).v_uint;
		const size_t charCount = snprintf(NULL, 0, "%"PRIu64, value) + 1;
		char *const string = calloc(charCount, sizeof(char));
		const size_t length = snprintf(string, charCount, "%"PRIu64, value);
		const Cog_Value result = cstrToCogstr(vm, string, length);
		free(string);
		Cog_Stack_push(&vm->stack, result);
	} break;
	case COG_OP_TOSTRING_FLOAT: {
		const double value = Cog_Stack_pop(&vm->stack).v_float;
		const size_t charCount = snprintf(NULL, 0, "%f", value) + 1;
		char *const string = calloc(charCount, sizeof(char));
		const size_t length = snprintf(string, charCount, "%f", value);
		const Cog_Value result = cstrToCogstr(vm, string, length);
		free(string);
		Cog_Stack_push(&vm->stack, result);
	} break;
	case COG_OP_TOSTRING_BOOL: {
		const bool value = Cog_Stack_pop(&vm->stack).v_bool;
		const Cog_Value result = cstrToCogstr(vm,
			value
			? "true"
			: "false",
			value
			? 4
			: 5
		);
		Cog_Stack_push(&vm->stack, result);
	} break;
	case COG_OP_EQ_CHAR: {
		Cog_Value value1 = Cog_Stack_pop(&vm->stack);
		Cog_Value value2 = Cog_Stack_pop(&vm->stack);
		Cog_Stack_push(&vm->stack, (Cog_Value) {
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = value1.v_char == value2.v_char
		});
	} break;
	case COG_OP_NEQ_CHAR: {
		Cog_Value value1 = Cog_Stack_pop(&vm->stack);
		Cog_Value value2 = Cog_Stack_pop(&vm->stack);
		Cog_Stack_push(&vm->stack, (Cog_Value) {
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = value1.v_char != value2.v_char
		});
	} break;
	case COG_OP_JUMPF_IFN_R:
		arg1 = readSizeT(vm, chunk);
		if (!Cog_Stack_currentPtr(&vm->stack)->v_bool)
			vm->pc += arg1 - sizeof(size_t);
		break;
	case COG_OP_JUMPF_IF_R:
		arg1 = readSizeT(vm, chunk);
		if (Cog_Stack_currentPtr(&vm->stack)->v_bool)
			vm->pc += arg1 - sizeof(size_t);
		break;
	default:
		COG_PANIC("Unsupported operation at %ld", vm->pc - 1);
		break;
	}
#undef INFIX
#undef COG_INFIX_LOG
}

static void execute(VM *vm, const Cog_Chunk *chunk)
{
	while (!vm->done && vm->pc < chunk->instr.count)
	{
#ifdef COG_DEBUG
		printf("%zu ", vm->pc);
		Cog_Stack_print(&vm->stack);
		fflush(stdout);
#endif
		runInstruction(vm, chunk);
	}
}
static void call(VM *vm, const Cog_Closure *closure, size_t argc)
{
	size_t oldPc    = vm->pc;
	Scope *oldScope = vm->scope;

	vm->pc    = 0;
	vm->scope = closure->env;

	Scope_enter(vm, argc);
	for (int i = argc - 1; i >= 0; i--)
		scopeWrite(vm, 0, i, Cog_Stack_pop(&vm->stack));
	execute(vm, closure->chunk);
	if (vm->done) return;
	Scope_exit(vm);
	vm->pc    = oldPc;
	vm->scope = oldScope;
}
int Cog_run(const Cog_Chunk *chunk, Cog_Globals *globals)
{
	VM vm = {0};
	Scope_enter(&vm, globals->count);
	for (size_t i = 0; i < globals->count; i++)
		scopeWrite(&vm, 0, i, globals->items[i].value);
	execute(&vm, chunk);
	Stack_free(&vm.stack);
	GC_freeAll(&vm.gc);
	Cog_Chunk_free(chunk);
	while (vm.scope) Scope_exit(&vm);
	return vm.retCode;
}
