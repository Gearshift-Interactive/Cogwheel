#pragma once

#include "value.h"
#include "globals.h"

#include "nob.h"

#define OPCODE_TYPE \
	/*X(RETURN, 0)*/ \
	X(POP, 0) \
	X(POP_IFPR, 0) \
	X(EXIT, 0) \
	/* load const */ \
	X(CLOAD_INT, sizeof(size_t)) \
	X(CLOAD_UINT, sizeof(size_t)) \
	X(CLOAD_FLOAT, sizeof(size_t)) \
	X(CLOAD_TRUE, 0) \
	X(CLOAD_FALSE, 0) \
	X(CLOAD_NULL, 0) \
	X(CLOAD_FUNC, sizeof(size_t)) \
	X(CLOAD_CHAR, sizeof(size_t)) \
	/* add */ \
	X(ADD_INT, 0) \
	X(ADD_UINT, 0) \
	X(ADD_FLOAT, 0) \
	/* sub */ \
	X(SUB_INT, 0) \
	X(SUB_UINT, 0) \
	X(SUB_FLOAT, 0) \
	/* div */ \
	X(DIV_INT, 0) \
	X(DIV_UINT, 0) \
	X(DIV_FLOAT, 0) \
	/* mul */ \
	X(MUL_INT, 0) \
	X(MUL_UINT, 0) \
	X(MUL_FLOAT, 0) \
	/* pow */ \
	X(POW_INT, 0) \
	X(POW_UINT, 0) \
	X(POW_FLOAT, 0) \
	/* equal */ \
	X(EQ_INT, 0) \
	X(EQ_UINT, 0) \
	X(EQ_FLOAT, 0) \
	/* not equal */ \
	X(NEQ_INT, 0) \
	X(NEQ_UINT, 0) \
	X(NEQ_FLOAT, 0) \
	/* greater than */ \
	X(GT_INT, 0) \
	X(GT_UINT, 0) \
	X(GT_FLOAT, 0) \
	/* less than */ \
	X(LT_INT, 0) \
	X(LT_UINT, 0) \
	X(LT_FLOAT, 0) \
	/* equal or greater than */ \
	X(EGT_INT, 0) \
	X(EGT_UINT, 0) \
	X(EGT_FLOAT, 0) \
	/* equal or less than */ \
	X(ELT_INT, 0) \
	X(ELT_UINT, 0) \
	X(ELT_FLOAT, 0) \
	/* logic */ \
	X(AND, 0) \
	X(OR, 0) \
	X(NOT, 0) \
	/* negation */ \
	X(NEG_INT, 0) \
	X(NEG_FLOAT, 0) \
	/* cast */ \
	X(CAST_ITOU, 0)/*int to uint*/ \
	X(CAST_ITOF, 0)/*int to float*/ \
	X(CAST_UTOI, 0)/*uint to int*/ \
	X(CAST_UTOF, 0)/*uint to float*/ \
	X(CAST_FTOI, 0)/*float to int*/ \
	X(CAST_FTOU, 0)/*float to uint*/ \
	/* scope */ \
	X(SCOPE_ENTER, sizeof(size_t)) \
	X(SCOPE_READ, sizeof(size_t) * 2) \
	X(SCOPE_WRITE, sizeof(size_t) * 2) \
	X(SCOPE_EXIT, 0) \
	/* jumps */ \
	X(JUMPF, sizeof(size_t)) \
	X(JUMPF_IFN, sizeof(size_t)) \
	X(JUMPB, sizeof(size_t)) \
	/* heap objects */ \
	X(GC_ALLOC, sizeof(size_t)) \
	X(GC_ALLOC_FROMSTACK, 0) \
	X(GC_ACCESS, sizeof(size_t)) \
	X(GC_ACCESS_FROMSTACK, 0) \
	X(GC_ASSIGN, sizeof(size_t)) \
	X(GC_ASSIGN_FROMSTACK, 0) \
	X(GC_FILL, 0) \
	X(GC_ASSIGNCOPY, sizeof(size_t)) \
	X(GC_SIZEOF, 0) \
	/* options */ \
	X(OPT_UNWRAP, 0) \
	X(OPT_CHECK, 0) \
	/* functions */ \
	X(CALL, sizeof(size_t)) \
	X(CALLN, sizeof(size_t)) \

typedef enum {
	OP_NOOP = 0,
#define X(name, argl) OP_##name,
	OPCODE_TYPE
#undef X
	OP_COUNT
} Opcode;

#define CONST_ARRAY(T) struct { size_t count, capacity; T *items; }

typedef struct Chunk {
	CONST_ARRAY(int64_t) intConsts;
	CONST_ARRAY(uint64_t) uintConsts;
	CONST_ARRAY(double) floatConsts;
	CONST_ARRAY(struct Chunk) functions;
	CONST_ARRAY(wchar_t) charConsts;
	struct {
		union { size_t count, length; };
		size_t capacity;
		union { uint8_t *code, *items; };
	} instr;
	size_t refCount;
} Chunk;

typedef struct Closure Closure;

#undef CONST_ARRAY

void Chunk_free(const Chunk *);
void Chunk_print(const Chunk *);
int run(const Chunk *, Globals *);
