#pragma once

#include "vm/vm.h"

#define COG_CHUNK_BUILDER_CONST_ARRAY(T) struct { T *items; size_t count, capacity; }

typedef struct Cog_ChunkBuilder {
	COG_CHUNK_BUILDER_CONST_ARRAY(int64_t) intConsts;
	COG_CHUNK_BUILDER_CONST_ARRAY(uint64_t) uintConsts;
	COG_CHUNK_BUILDER_CONST_ARRAY(double) floatConsts;
	COG_CHUNK_BUILDER_CONST_ARRAY(struct Cog_Chunk) functions;
	COG_CHUNK_BUILDER_CONST_ARRAY(uint32_t) charConsts;
	struct {
		union { size_t count, length; };
		size_t capacity;
		union { uint8_t *code, *items; };
	} instr;
} Cog_ChunkBuilder;

Cog_Chunk Cog_ChunkBuilder_finalize(const Cog_ChunkBuilder);
