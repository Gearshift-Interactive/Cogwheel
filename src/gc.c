#include "gc.h"

Value GC_alloc(GC *this, size_t valueCount)
{
	HeapObject *obj = calloc(1, sizeof(*obj) + sizeof(Value) * valueCount);
	obj->count = valueCount;
	da_append(&this->allObjects, obj);
	return (Value){
#ifdef DEBUG
		.type = VALUE_HEAP,
#endif
		.isHeap = true,
		.v_heap = obj,
	};
}
void GC_freeAll(GC *this)
{
	da_foreach(HeapObject*, obj, &this->allObjects)
		free(*obj);
	if (this->allObjects.items)
		free(this->allObjects.items);
}
