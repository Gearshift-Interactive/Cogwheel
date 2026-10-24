#include "vm.h"
#include "error.h"

#include <math.h>
#include <inttypes.h>

#ifdef COG_DEBUG
#	include "parsing/parser.h"
#endif

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
		free(this->values);
		free(this);
	}
}

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

#ifdef COG_DEBUG
static void VM_print(VM *this)
{
	printf("--STACK--\n");
	if (this->stackBase != this->stackTop)
		for (Cog_Value *i = this->stackBase; i != this->stackTop; i++)
			// printf("%zu - ", i);
			Cog_Value_print(i);
	else
		printf("  *empty*\n");
}
#endif
static void VM_init(VM *this)
{
	this->stackBase = malloc(sizeof(Cog_Value) * 64);
	this->stackEnd = this->stackBase + 64;
	this->stackTop = this->stackBase;
}
static void VM_stackFree(VM *this)
{
	free(this->stackBase);
}
void VM_push(VM *this, Cog_Value value)
{
	if (this->stackTop == this->stackEnd)
	{
		size_t capacity = this->stackEnd - this->stackBase;
		this->stackBase = realloc(this->stackBase, sizeof(Cog_Value) * capacity * 2);
		if (!this->stackBase) COG_PANIC("Out of memory");
		this->stackEnd = this->stackBase + capacity * 2;
		this->stackTop = this->stackBase + capacity;
	}
	*this->stackTop++ = value;
}
inline Cog_Value *VM_pop(VM *this)
{
	return --this->stackTop;
}
inline Cog_Value *VM_current(VM *this)
{
	return this->stackTop - 1;
}
inline Cog_Value *VM_countBack(VM *this, size_t number)
{
	return this->stackTop - 1 - number;
}

void Cog_Chunk_free(const Cog_Chunk *this)
{
#define FREE_IF_PRESENT(PTR) if (PTR) free(PTR)
	FREE_IF_PRESENT(this->intConsts.start);
	FREE_IF_PRESENT(this->uintConsts.start);
	FREE_IF_PRESENT(this->floatConsts.start);
	FREE_IF_PRESENT(this->instr.start);
	FREE_IF_PRESENT(this->charConsts.start);
#undef FREE_IF_PRESENT
	if (this->functions.start)
	{
		for (
			Cog_Chunk *chunk = this->functions.start;
			chunk <= this->functions.end;
			chunk++
		) Cog_Chunk_free(chunk);
		free(this->functions.start);
	}
}
static void printLevel(size_t level)
{
	for (size_t i = 0; i < level; i++)
		printf("    ");
}
static void Chunk_printImpl(const Cog_Chunk *this, size_t level)
{
#define PRINTOUT(ARR, FORMAT, T) printf("%p===%p\n", (ARR).start, (ARR).end); fflush(stdout); if ((ARR).start != NULL && (ARR).end != NULL) for (T *i = (ARR).start; i < (ARR).end; i++) { printLevel(level); printf("  %ld - %"FORMAT",\n", i - (ARR).start, *i); }
	printLevel(level);
	printf("INT_CONSTANTS:\n");
	PRINTOUT(this->intConsts, PRIi64, int64_t)
	printLevel(level);
	printf("UINT_CONSTANTS:\n");
	PRINTOUT(this->uintConsts, PRIu64, uint64_t)
	printLevel(level);
	printf("FLOAT_CONSTANTS:\n");
	PRINTOUT(this->floatConsts, "f", double)
	printLevel(level);
	printf("CHAR_CONSTANTS:\n");
	for (uint32_t *i = this->charConsts.start; i < this->charConsts.end; i++)
	{
		printLevel(level);
		printf("  %ld - ", i - this->charConsts.start);
		uint32_t value = *i;
		uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&value];
		for (size_t bi = 0; bi < charSize; ++bi)
			putchar((value >> (bi * 8)) & 0xff);
		putchar('\n');
	}
	printLevel(level);
	printf("FUNCTIONS:\n");
	for (Cog_Chunk *i = this->functions.start; i <= this->functions.end; i++)
	{
		printLevel(level);
		Chunk_printImpl(i, level + 1);
	}
	printLevel(level);
	printf("CODE:\n");
	for (uint8_t *ip = this->instr.start; ip <= this->instr.end; ip++)
	{
		printLevel(level);
		switch (*ip)
		{
#define COG_X(NAME, ARGL) \
	case COG_OP_##NAME: \
		printf("  %ld - %s", ip - this->instr.start, #NAME); \
		if (ARGL) \
		{ \
			size_t argl = ARGL; \
			printf(" - "); \
			/* displaying args in little endidan */ \
			for (size_t iini = 0; iini < argl; iini++) \
				printf("%X", *(ip + 1 + iini)); \
			ip += argl; \
		} \
		printf("\n"); \
		break;
	COG_OPCODE_TYPE
#undef COG_X
		}
	}
}
void Cog_Chunk_print(const Cog_Chunk *this)
{
	Chunk_printImpl(this, 0);
}
// static void Stack_free(const Cog_Stack *this)
// {
// 	if (this->values)
// 		free(this->values);
// }
static size_t readSizeT(VM *vm, const Cog_Chunk *chunk)
{
	NOB_UNUSED(chunk);
	size_t *result = (size_t *)vm->ip;
	vm->ip += sizeof *result;
	return *result;
}
static void call(VM *, const Cog_Closure *, size_t);
static void Scope_enter(VM *vm, size_t varCount)
{
	Scope *parent = vm->scope;
	vm->scope = calloc(1, sizeof(*(vm->scope)));
	vm->scope->values = calloc(varCount, sizeof(Cog_Value));
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
#define INFIX(TYPE, FIELD, OP) \
	do { \
		Cog_Value *lhs = VM_countBack(vm, 1); \
		lhs->FIELD = lhs->FIELD OP VM_current(vm)->FIELD; \
		vm->stackTop--; \
	} while(0);
	Cog_Opcode current = *(Cog_Opcode *)vm->ip;
	size_t arg1, arg2;
	vm->ip++;
	Cog_Value *curValue;
	switch (current)
	{
	case COG_OP_EXIT:
		vm->retCode = VM_pop(vm)->v_int;
		vm->done = true;
		break;
	case COG_OP_CLOAD_INT:
		arg1 = readSizeT(vm, chunk);
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_INT,
#endif
			.v_int = chunk->intConsts.start[arg1],
		});
		break;
	case COG_OP_CLOAD_UINT:
		arg1 = readSizeT(vm, chunk);
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = chunk->uintConsts.start[arg1],
		});
		break;
	case COG_OP_CLOAD_FLOAT:
		arg1 = readSizeT(vm, chunk);
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_FLOAT,
#endif
			.v_float = chunk->floatConsts.start[arg1],
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
	case COG_OP_DIV_INT: {
		Cog_Value *lhs = VM_countBack(vm, 1);
		Cog_Value *rhs = VM_current(vm);
		if (rhs->v_int == 0) COG_PANIC("Division by zero");
		lhs->v_int = lhs->v_int / rhs->v_int;
		vm->stackTop--;
	} break;
	case COG_OP_DIV_UINT: {
		Cog_Value *lhs = VM_countBack(vm, 1);
		Cog_Value *rhs = VM_current(vm);
		if (rhs->v_uint == 0) COG_PANIC("Division by zero");
		lhs->v_uint = lhs->v_uint / rhs->v_uint;
		vm->stackTop--;
	} break;
	case COG_OP_DIV_FLOAT: {
		Cog_Value *lhs = VM_countBack(vm, 1);
		Cog_Value *rhs = VM_current(vm);
		if (rhs->v_float == 0) COG_PANIC("Division by zero");
		lhs->v_float = lhs->v_float / rhs->v_float;
		vm->stackTop--;
	} break;
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
		Cog_Value *lhs = VM_countBack(vm, 1);
		Cog_Value *rhs = VM_current(vm);
		lhs->v_int = round(pow(lhs->v_int, rhs->v_int));
		vm->stackTop--;
	} break;
	case COG_OP_POW_UINT: {
		Cog_Value *lhs = VM_countBack(vm, 1);
		Cog_Value *rhs = VM_current(vm);
		lhs->v_uint = round(pow(lhs->v_uint, rhs->v_uint));
		vm->stackTop--;
	} break;
	case COG_OP_POW_FLOAT: {
		Cog_Value *lhs = VM_countBack(vm, 1);
		Cog_Value *rhs = VM_current(vm);
		lhs->v_float = pow(lhs->v_float, rhs->v_float);
		vm->stackTop--;
	} break;
	case COG_OP_CAST_ITOU: {
		Cog_Value *v = VM_current(vm);
		v->v_uint = (uint64_t)v->v_int;
#ifdef COG_DEBUG
		v->type = COG_VALUE_UINT;
#endif
	} break;
	case COG_OP_CAST_ITOF: {
		Cog_Value *v = VM_current(vm);
		v->v_float = (double)v->v_int;
#ifdef COG_DEBUG
		v->type = COG_VALUE_FLOAT;
#endif
	} break;
	case COG_OP_CAST_UTOI: {
		Cog_Value *v = VM_current(vm);
		v->v_int = (int64_t)v->v_uint;
#ifdef COG_DEBUG
		v->type = COG_VALUE_INT;
#endif
	} break;
	case COG_OP_CAST_UTOF: {
		Cog_Value *v = VM_current(vm);
		v->v_float = (double)v->v_uint;
#ifdef COG_DEBUG
		v->type = COG_VALUE_FLOAT;
#endif
	} break;
	case COG_OP_CAST_FTOI: {
		Cog_Value *v = VM_current(vm);
		v->v_int = (int64_t)v->v_float;
#ifdef COG_DEBUG
		v->type = COG_VALUE_FLOAT;
#endif
	} break;
	case COG_OP_CAST_FTOU: {
		Cog_Value *v = VM_current(vm);
		v->v_uint = (uint64_t)v->v_float;
#ifdef COG_DEBUG
		v->type = COG_VALUE_UINT;
#endif
	} break;
	case COG_OP_NEG_INT: {
		Cog_Value *v = VM_current(vm);
		v->v_int = -(v->v_int);
	} break;
	case COG_OP_NEG_FLOAT: {
		Cog_Value *v = VM_current(vm);
		v->v_float = -(v->v_float);
	} break;
	case COG_OP_POP:
		VM_pop(vm);
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
		VM_push(vm, scopeRead(vm, arg1, arg2));
		break;
	case COG_OP_SCOPE_WRITE:
		arg1 = readSizeT(vm, chunk);  // depth
		arg2 = readSizeT(vm, chunk);  // id
		scopeWrite(vm, arg1, arg2, *VM_current(vm));
		break;
	case COG_OP_CLOAD_TRUE:
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = true,
		});
		break;
	case COG_OP_CLOAD_FALSE:
		VM_push(vm, (Cog_Value){
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
		vm->ip += arg1 - sizeof(size_t);
		break;
	case COG_OP_JUMPF_IFN:
		arg1 = readSizeT(vm, chunk);
		if (!VM_pop(vm)->v_bool)
			vm->ip += arg1 - sizeof(size_t);
		break;
#ifdef COG_DEBUG
#	define COG_INFIX_LOG(FIELD, OP) \
		do { \
			Cog_Value *rhs = VM_current(vm); \
			Cog_Value *lhs = VM_countBack(vm, 1); \
			lhs->v_bool = lhs->FIELD OP rhs->FIELD; \
			lhs->type = COG_VALUE_BOOL; \
			vm->stackTop--; \
		} while (0)
#else
#	define COG_INFIX_LOG(FIELD, OP)               \
		do {                                     \
			Cog_Value *rhs = VM_current(vm); \
			Cog_Value *lhs = VM_countBack(vm, 1); \
			lhs->v_bool = lhs->FIELD OP rhs->FIELD; \
			vm->stackTop--; \
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
		curValue = VM_current(vm);
		curValue->v_bool = !curValue->v_bool;
		break;
	case COG_OP_JUMPB:
		arg1 = readSizeT(vm, chunk);
		vm->ip -= arg1 + sizeof(size_t);
		break;
	case COG_OP_POP_IFPR:
		if (vm->stackBase != vm->stackTop)
			VM_pop(vm);
		break;
	case COG_OP_GC_ALLOC:
		arg1 = readSizeT(vm, chunk);
		VM_push(vm, GC_alloc(&vm->gc, arg1));
		break;
	case COG_OP_GC_ACCESS:
		COG_PANIC("UNIMPLEMENTED");
		break;
	case COG_OP_GC_ACCESS_FROMSTACK: {
		Cog_Value *index = VM_pop(vm);
		Cog_Value *target = VM_pop(vm);
		HeapObject *obj = target->v_heap;
		if (index->v_uint < obj->count)
			VM_push(vm, obj->items[index->v_uint]);
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_ASSIGN: {
		Cog_Value *value = VM_pop(vm);
		Cog_Value *target = VM_current(vm);
		arg1 = readSizeT(vm, chunk);
		HeapObject *obj = target->v_heap;
		if (arg1 < obj->count)
			obj->items[arg1] = *value;
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_ASSIGN_FROMSTACK: {
		Cog_Value *value = VM_pop(vm);
		Cog_Value *index = VM_pop(vm);
		Cog_Value *target = VM_current(vm);
		HeapObject *obj = target->v_heap;
		if (index->v_uint < obj->count)
			obj->items[index->v_uint] = *value;
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_ASSIGNCOPY: {
		Cog_Value *target = VM_countBack(vm, 2);
		Cog_Value *value = VM_countBack(vm, 1);
		arg1 = readSizeT(vm, chunk);
		HeapObject *obj = target->v_heap;
		if (arg1 < obj->count)
			obj->items[arg1] = *value;
		else
			COG_PANIC("Buffer overflow");
	} break;
	case COG_OP_GC_SIZEOF: {
		Cog_Value *value = VM_pop(vm);
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_UINT,
#endif
			.v_uint = ((HeapObject*)value->v_heap)->count,
		});
	} break;
	case COG_OP_CLOAD_NULL:
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_NULL,
#endif
			.isNull = true,
		});
		break;
	case COG_OP_OPT_UNWRAP: {
		Cog_Value *value = VM_current(vm);
		if (value->isNull)
			COG_PANIC("Unwrap failed");
	} break;
	case COG_OP_OPT_CHECK: {
		Cog_Value *value = VM_current(vm);
		*value = (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = !value->isNull,
		};
	} break;
	case COG_OP_CLOAD_FUNC: {
		size_t chunkIndex = readSizeT(vm, chunk);
		Cog_Chunk *funcChunk = &chunk->functions.start[chunkIndex];
		// Scope_release(funcChunk->vmData);
		// funcChunk->vmData = vm->scope;
		if (vm->scope)
			Scope_retain(vm->scope);
		VM_push(vm, GC_allocClosure(&vm->gc, funcChunk, vm->scope));
	} break;
	case COG_OP_CALL: {
		size_t argc = readSizeT(vm, chunk);
		Cog_Value *function = VM_pop(vm);
		call(vm, function->v_func, argc);
	} break;
	case COG_OP_GC_ALLOC_FROMSTACK: {
		Cog_Value *size = VM_pop(vm);
		VM_push(vm, GC_alloc(&vm->gc, size->v_uint));
	} break;
	case COG_OP_GC_FILL: {
		Cog_Value *value = VM_pop(vm);
		HeapObject *obj = VM_current(vm)->v_heap;
		for (size_t i = 0; i < obj->count; i++)
			obj->items[i] = *value;
	} break;
	case COG_OP_CALLN: {
		Cog_Value *function = VM_pop(vm);
		function->v_nfunc(vm, readSizeT(vm, chunk));
	} break;
	case COG_OP_CLOAD_CHAR:
		arg1 = readSizeT(vm, chunk);
		VM_push(vm, (Cog_Value){
#ifdef COG_DEBUG
			.type = COG_VALUE_CHAR,
#endif
			.v_char = chunk->charConsts.start[arg1],
		});
		break;
	case COG_OP_GC_REALLOC: {
		Cog_Value *fillValue = VM_pop(vm);
		Cog_Value *newSize = VM_pop(vm);
		Cog_Value *array = VM_current(vm);
		HeapObject *obj = array->v_heap;
		if (newSize->v_uint == obj->count)
			break;
		size_t oldSize = obj->count;
		obj->items = realloc(obj->items, newSize->v_uint * sizeof *obj->items);
		obj->count = newSize->v_uint;
		if (oldSize > obj->count)
			break;
		for (size_t i = oldSize; i < obj->count; i++)
			obj->items[i] = *fillValue;
	} break;
	case COG_OP_GC_CONCAT: {
		HeapObject *obj2 = VM_pop(vm)->v_heap;
		HeapObject *obj1 = VM_pop(vm)->v_heap;
		Cog_Value result = GC_alloc(&vm->gc, obj1->count + obj2->count);
		HeapObject *resultHeap = result.v_heap;
		for (size_t i = 0; i < obj1->count; i++)
			resultHeap->items[i] = obj1->items[i];
		for (size_t i = 0; i < obj2->count; i++)
			resultHeap->items[obj1->count + i] = obj2->items[i];
		VM_push(vm, result);
	} break;
	case COG_OP_GC_REPEAT: {
		Cog_Value *repeatTimes = VM_pop(vm);
		HeapObject *obj = VM_pop(vm)->v_heap;
		Cog_Value result = GC_alloc(&vm->gc, obj->count * repeatTimes->v_uint);
		HeapObject *resultHeap = result.v_heap;
		for (size_t i = 0; i < resultHeap->count; i++)
			resultHeap->items[i] = obj->items[i % obj->count];
		VM_push(vm, result);
	} break;
	case COG_OP_TOSTRING_INT: {
		const int64_t value = VM_pop(vm)->v_int;
		const size_t charCount = snprintf(NULL, 0, "%"PRId64, value) + 1;
		char *const string = calloc(charCount, sizeof(char));
		const size_t length = snprintf(string, charCount, "%"PRId64, value);
		const Cog_Value result = cstrToCogstr(vm, string, length);
		free(string);
		VM_push(vm, result);
	} break;
	case COG_OP_TOSTRING_UINT: {
		const uint64_t value = VM_pop(vm)->v_uint;
		const size_t charCount = snprintf(NULL, 0, "%"PRIu64, value) + 1;
		char *const string = calloc(charCount, sizeof(char));
		const size_t length = snprintf(string, charCount, "%"PRIu64, value);
		const Cog_Value result = cstrToCogstr(vm, string, length);
		free(string);
		VM_push(vm, result);
	} break;
	case COG_OP_TOSTRING_FLOAT: {
		const double value = VM_pop(vm)->v_float;
		const size_t charCount = snprintf(NULL, 0, "%f", value) + 1;
		char *const string = calloc(charCount, sizeof(char));
		const size_t length = snprintf(string, charCount, "%f", value);
		const Cog_Value result = cstrToCogstr(vm, string, length);
		free(string);
		VM_push(vm, result);
	} break;
	case COG_OP_TOSTRING_BOOL: {
		const bool value = VM_pop(vm)->v_bool;
		const Cog_Value result = cstrToCogstr(vm,
			value
			? "true"
			: "false",
			value
			? 4
			: 5
		);
		VM_push(vm, result);
	} break;
	case COG_OP_EQ_CHAR: {
		Cog_Value *value1 = VM_pop(vm);
		Cog_Value *value2 = VM_pop(vm);
		VM_push(vm, (Cog_Value) {
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = value1->v_char == value2->v_char
		});
	} break;
	case COG_OP_NEQ_CHAR: {
		Cog_Value *value1 = VM_pop(vm);
		Cog_Value *value2 = VM_pop(vm);
		VM_push(vm, (Cog_Value) {
#ifdef COG_DEBUG
			.type = COG_VALUE_BOOL,
#endif
			.v_bool = value1->v_char != value2->v_char
		});
	} break;
	case COG_OP_JUMPF_IFN_R:
		arg1 = readSizeT(vm, chunk);
		if (!VM_current(vm)->v_bool)
			vm->ip += arg1 - sizeof(size_t);
		break;
	case COG_OP_JUMPF_IF_R:
		arg1 = readSizeT(vm, chunk);
		if (VM_current(vm)->v_bool)
			vm->ip += arg1 - sizeof(size_t);
		break;
	default:
		COG_PANIC("Unsupported operation at %ld", chunk->instr.end - vm->ip - 1);
		break;
	}
#undef INFIX
#undef COG_INFIX_LOG
}

static void execute(VM *vm, const Cog_Chunk *chunk)
{
	while (!vm->done && vm->ip <= chunk->instr.end)
	{
#ifdef COG_DEBUG
		printf("%zu ", vm->ip - chunk->instr.start);
		VM_print(vm);
		fflush(stdout);
#endif
		runInstruction(vm, chunk);
	}
}
static void call(VM *vm, const Cog_Closure *closure, size_t argc)
{
	uint8_t *oldIp  = vm->ip;
	Scope *oldScope = vm->scope;

	vm->ip    = closure->chunk->instr.start;
	vm->scope = closure->env;

	Scope_enter(vm, argc);
	for (int i = argc - 1; i >= 0; i--)
		scopeWrite(vm, 0, i, *VM_pop(vm));
	execute(vm, closure->chunk);
	if (vm->done) return;
	Scope_exit(vm);
	vm->ip    = oldIp;
	vm->scope = oldScope;
}
int Cog_run(const Cog_Chunk *chunk, Cog_Globals *globals)
{
	VM vm = {0};
	VM_init(&vm);
	Scope_enter(&vm, globals->count);
	for (size_t i = 0; i < globals->count; i++)
		scopeWrite(&vm, 0, i, globals->items[i].value);
	vm.ip = chunk->instr.start;
	execute(&vm, chunk);
	VM_stackFree(&vm);
	GC_freeAll(&vm.gc);
	Cog_Chunk_free(chunk);
	while (vm.scope) Scope_exit(&vm);
	return vm.retCode;
}
