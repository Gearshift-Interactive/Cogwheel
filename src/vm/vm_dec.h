#pragma once

#include "nob.h"

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
	size_t pc;
	int retCode;
	bool done;
	GC gc;
} VM;
