#pragma once

#include "nob.h"

#define OPCODE_TYPE                \
	/*X(RETURN, 0)*/                   \
	/*X(POP, 0)*/                      \
	X(EXIT, 0)                     \
	/* load const */               \
	X(CLOAD_INT, sizeof(size_t))   \
	X(CLOAD_UINT, sizeof(size_t))  \
	X(CLOAD_FLOAT, sizeof(size_t)) \
	/* add */                      \
	X(ADD_INT, 0)                  \
	X(ADD_UINT, 0)                 \
	X(ADD_FLOAT, 0)                \
	/* sub */                      \
	X(SUB_INT, 0)                  \
	X(SUB_UINT, 0)                 \
	X(SUB_FLOAT, 0)                \
	/* div */                      \
	X(DIV_INT, 0)                  \
	X(DIV_UINT, 0)                 \
	X(DIV_FLOAT, 0)                \
	/* mul */                      \
	X(MUL_INT, 0)                  \
	X(MUL_UINT, 0)                 \
	X(MUL_FLOAT, 0)                \
	/* pow */                      \
	X(POW_INT, 0)                  \
	X(POW_UINT, 0)                 \
	X(POW_FLOAT, 0)                \

typedef enum {
	OP_NOOP = 0,
#define X(name, argl) OP_##name,
	OPCODE_TYPE
#undef X
	OP_COUNT
} Opcode;

#define CONST_ARRAY(T) struct { size_t count, capacity; T *items; }

typedef struct {
	CONST_ARRAY(int64_t) intConsts;
	CONST_ARRAY(uint64_t) uintConsts;
	CONST_ARRAY(double) floatConsts;
	struct {
		union { size_t count, length; };
		size_t capacity;
		union { uint8_t *code, *items; };
	} instr;
} Chunk;

#undef CONST_ARRAY

void Chunk_free(const Chunk *);
void Chunk_print(const Chunk *);
int run(const Chunk *);
