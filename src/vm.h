#pragma once

#include "value.h"
#include "globals.h"

#include "nob.h"

#define COG_OPCODE_TYPE \
	/*COG_X(RETURN, 0)*/ \
	COG_X(POP, 0) \
	COG_X(POP_IFPR, 0) \
	COG_X(EXIT, 0) \
	/* load const */ \
	COG_X(CLOAD_INT, sizeof(size_t)) \
	COG_X(CLOAD_UINT, sizeof(size_t)) \
	COG_X(CLOAD_FLOAT, sizeof(size_t)) \
	COG_X(CLOAD_TRUE, 0) \
	COG_X(CLOAD_FALSE, 0) \
	COG_X(CLOAD_NULL, 0) \
	COG_X(CLOAD_FUNC, sizeof(size_t)) \
	COG_X(CLOAD_CHAR, sizeof(size_t)) \
	/* add */ \
	COG_X(ADD_INT, 0) \
	COG_X(ADD_UINT, 0) \
	COG_X(ADD_FLOAT, 0) \
	/* sub */ \
	COG_X(SUB_INT, 0) \
	COG_X(SUB_UINT, 0) \
	COG_X(SUB_FLOAT, 0) \
	/* div */ \
	COG_X(DIV_INT, 0) \
	COG_X(DIV_UINT, 0) \
	COG_X(DIV_FLOAT, 0) \
	/* mul */ \
	COG_X(MUL_INT, 0) \
	COG_X(MUL_UINT, 0) \
	COG_X(MUL_FLOAT, 0) \
	/* pow */ \
	COG_X(POW_INT, 0) \
	COG_X(POW_UINT, 0) \
	COG_X(POW_FLOAT, 0) \
	/* equal */ \
	COG_X(EQ_INT, 0) \
	COG_X(EQ_UINT, 0) \
	COG_X(EQ_FLOAT, 0) \
	COG_X(EQ_CHAR, 0) \
	/* not equal */ \
	COG_X(NEQ_INT, 0) \
	COG_X(NEQ_UINT, 0) \
	COG_X(NEQ_FLOAT, 0) \
	COG_X(NEQ_CHAR, 0) \
	/* greater than */ \
	COG_X(GT_INT, 0) \
	COG_X(GT_UINT, 0) \
	COG_X(GT_FLOAT, 0) \
	/* less than */ \
	COG_X(LT_INT, 0) \
	COG_X(LT_UINT, 0) \
	COG_X(LT_FLOAT, 0) \
	/* equal or greater than */ \
	COG_X(EGT_INT, 0) \
	COG_X(EGT_UINT, 0) \
	COG_X(EGT_FLOAT, 0) \
	/* equal or less than */ \
	COG_X(ELT_INT, 0) \
	COG_X(ELT_UINT, 0) \
	COG_X(ELT_FLOAT, 0) \
	/* logic */ \
	COG_X(AND, 0) \
	COG_X(OR, 0) \
	COG_X(NOT, 0) \
	/* negation */ \
	COG_X(NEG_INT, 0) \
	COG_X(NEG_FLOAT, 0) \
	/* cast */ \
	COG_X(CAST_ITOU, 0)/*int to uint*/ \
	COG_X(CAST_ITOF, 0)/*int to float*/ \
	COG_X(CAST_UTOI, 0)/*uint to int*/ \
	COG_X(CAST_UTOF, 0)/*uint to float*/ \
	COG_X(CAST_FTOI, 0)/*float to int*/ \
	COG_X(CAST_FTOU, 0)/*float to uint*/ \
	/* scope */ \
	COG_X(SCOPE_ENTER, sizeof(size_t)) \
	COG_X(SCOPE_READ, sizeof(size_t) * 2) \
	COG_X(SCOPE_WRITE, sizeof(size_t) * 2) \
	COG_X(SCOPE_EXIT, 0) \
	/* jumps */ \
	COG_X(JUMPF, sizeof(size_t)) \
	COG_X(JUMPF_IF_R, sizeof(size_t)) \
	COG_X(JUMPF_IFN_R, sizeof(size_t)) \
	COG_X(JUMPF_IFN, sizeof(size_t)) \
	COG_X(JUMPB, sizeof(size_t)) \
	/* heap objects */ \
	COG_X(GC_ALLOC, sizeof(size_t)) \
	COG_X(GC_ALLOC_FROMSTACK, 0) \
	COG_X(GC_ACCESS, sizeof(size_t)) \
	COG_X(GC_ACCESS_FROMSTACK, 0) \
	COG_X(GC_ASSIGN, sizeof(size_t)) \
	COG_X(GC_ASSIGN_FROMSTACK, 0) \
	COG_X(GC_FILL, 0) \
	COG_X(GC_ASSIGNCOPY, sizeof(size_t)) \
	COG_X(GC_SIZEOF, 0) \
	COG_X(GC_REALLOC, 0) \
	COG_X(GC_CONCAT, 0) \
	COG_X(GC_REPEAT, 0) \
	/* options */ \
	COG_X(OPT_UNWRAP, 0) \
	COG_X(OPT_CHECK, 0) \
	/* functions */ \
	COG_X(CALL, sizeof(size_t)) \
	COG_X(CALLN, sizeof(size_t)) \
	/* stringification */ \
	COG_X(TOSTRING_INT, 0) \
	COG_X(TOSTRING_UINT, 0) \
	COG_X(TOSTRING_FLOAT, 0) \
	COG_X(TOSTRING_BOOL, 0) \

typedef enum {
	COG_OP_NOOP = 0,
#define COG_X(name, argl) COG_OP_##name,
	COG_OPCODE_TYPE
#undef COG_X
	COG_OP_COUNT
} Cog_Opcode;

#define COG_CONST_ARRAY(T) struct { size_t count, capacity; T *items; }

typedef struct Cog_Chunk {
	COG_CONST_ARRAY(int64_t) intConsts;
	COG_CONST_ARRAY(uint64_t) uintConsts;
	COG_CONST_ARRAY(double) floatConsts;
	COG_CONST_ARRAY(struct Cog_Chunk) functions;
	COG_CONST_ARRAY(uint32_t) charConsts;
	struct {
		union { size_t count, length; };
		size_t capacity;
		union { uint8_t *code, *items; };
	} instr;
	size_t refCount;
} Cog_Chunk;

typedef struct Cog_Closure Cog_Closure;

#undef COG_CONST_ARRAY

void Cog_Chunk_free(const Cog_Chunk *);
void Cog_Chunk_print(const Cog_Chunk *);
int Cog_run(const Cog_Chunk *, Cog_Globals *);
