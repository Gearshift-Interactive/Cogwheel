#include "chunk_builder.h"

Cog_Chunk Cog_ChunkBuilder_finalize(const Cog_ChunkBuilder this)
{
#define INITER(FIELD) .FIELD = { \
	.start = this.FIELD.items, \
	.end = this.FIELD.items + this.FIELD.count, \
}
	return (Cog_Chunk){
		INITER(intConsts),
		INITER(uintConsts),
		INITER(floatConsts),
		INITER(functions),
		INITER(charConsts),
		INITER(instr),
	};
}
