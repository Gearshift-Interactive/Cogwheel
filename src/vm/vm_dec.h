#pragma once

#include "nob.h"

#define COG_CHUNK_CONST_ARRAY(T) struct { T *start, *end; }

typedef struct Cog_Chunk {
	COG_CHUNK_CONST_ARRAY(int64_t) intConsts;
	COG_CHUNK_CONST_ARRAY(uint64_t) uintConsts;
	COG_CHUNK_CONST_ARRAY(double) floatConsts;
	COG_CHUNK_CONST_ARRAY(struct Cog_Chunk) functions;
	COG_CHUNK_CONST_ARRAY(uint32_t) charConsts;
	struct {
		uint8_t *start, *end;
	} instr;
	size_t refCount;
} Cog_Chunk;

typedef struct Scope {
	struct Scope *parent;
	size_t size;
	size_t refCount;
	Cog_Value *values;
} Scope;

typedef struct Cog_Closure {
	Cog_Chunk *chunk;
	Scope *env;
} Cog_Closure;

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

typedef struct VM {
	Cog_Value *stackBase; // points to the first item of the stack
	Cog_Value *stackTop; // points **past** the last used element of the stack
	Cog_Value *stackEnd; // points to the last **allocated** item of the stack
	Scope *scope;
	uint8_t *ip;
	int retCode;
	bool done;
	GC gc;
} VM;
