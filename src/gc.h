#pragma once

#include "value.h"

#include "nob.h"

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
} GC;

Value GC_alloc(GC *, size_t valueCount);
void GC_freeAll(GC *);
