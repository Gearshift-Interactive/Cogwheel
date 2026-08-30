#include "value.h"
#include "error.h"

#include <stdio.h>
#include <inttypes.h>

#ifdef DEBUG
static const char *ValueType_toString(ValueType type)
{
	switch (type)
	{
#	define X(TYPE) case VALUE_##TYPE: return #TYPE;
		VALUE_TYPES
#	undef X
		default: return "UNKNOWN";
	}
}
void Value_print(const Value *this) {
	if (this->isNull)
	{
		printf("NULL\n");
		return;
	}
	printf("%s", ValueType_toString(this->type));
	if (!this->type)
	{
		printf("(UNKNOWN_TYPE)\n");
		return;
	}
	switch (this->type)
	{
		case VALUE_INT:
			printf("(%"PRId64")\n", this->v_int);
			break;
		case VALUE_UINT:
			printf("(%"PRIu64")\n", this->v_uint);
			break;
		case VALUE_FLOAT:
			printf("(%f)\n", this->v_float);
			break;
		case VALUE_BOOL:
			printf("(%s)\n", this->v_bool ? "true" : "false");
			break;
		case VALUE_HEAP:
			printf("(%p)\n", this->v_heap);
			break;
		case VALUE_UNKNOWN:
			printf("(WTF)\n");
			__attribute__((fallthrough));
		case VALUE_VOID:
			PANIC("TS is void");
		case VALUE_NULL:
			PANIC("TS is null");
	}
}
#endif
