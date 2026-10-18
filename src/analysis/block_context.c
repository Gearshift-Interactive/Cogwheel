#include "block_context.h"

Cog_BlockContext *Cog_BlockContext_findParent
	(Cog_BlockContext *this, Cog_BlockContextType type)
{
	for (Cog_BlockContext *current = this; current; current = current->parent)
		if (current->type == type)
			return current;
	return NULL;
}
