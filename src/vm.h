#pragma once

#define OPCODE_TYPE \
	X(RETURN)       \
	/* add */       \
	X(ADD_INT)      \
	X(ADD_UINT)     \
	X(ADD_FINT)     \
	/* sub */       \
	X(SUB_INT)      \
	X(SUB_UINT)     \
	X(SUB_FINT)     \
	/* div */       \
	X(DIV_INT)      \
	X(DIV_UINT)     \
	X(DIV_FINT)     \
	/* mul */       \
	X(MUL_INT)      \
	X(MUL_UINT)     \
	X(MUL_FINT)     \
	/* pow */       \
	X(POW_INT)      \
	X(POW_UINT)     \
	X(POW_FINT)     \

typedef enum {
	OP_NOOP = 0,
#define X(name) OP_##name,
	OPCODE_TYPE
#undef X
	OP_COUNT
} Opcode;

typedef struct {
	union { uint8_t *code, *items; };
	union { size_t count, length; };
	size_t capacity;
} Chunk;

void Chunk_free(Chunk *);
void run(Chunk *);
