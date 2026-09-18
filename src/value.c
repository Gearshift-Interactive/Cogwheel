#include "value.h"
#include "error.h"

#include <stdio.h>
#include <inttypes.h>

#ifdef COG_DEBUG
static const char *ValueType_toString(Cog_ValueType type)
{
	switch (type)
	{
#	define COG_X(TYPE) case COG_VALUE_##TYPE: return #TYPE;
		COG_VALUE_TYPES
#undef COG_X
		default: return "UNKNOWN";
	}
}
void Cog_Value_print(const Cog_Value *this) {
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
		case COG_VALUE_INT:
			printf("(%"PRId64")\n", this->v_int);
			break;
		case COG_VALUE_UINT:
			printf("(%"PRIu64")\n", this->v_uint);
			break;
		case COG_VALUE_FLOAT:
			printf("(%f)\n", this->v_float);
			break;
		case COG_VALUE_BOOL:
			printf("(%s)\n", this->v_bool ? "true" : "false");
			break;
		case COG_VALUE_HEAP:
		case COG_VALUE_FUNC:
		case COG_VALUE_NFUNC:
			printf("(%p)\n", this->v_heap);
			break;
		case COG_VALUE_UNKNOWN:
			printf("(WTF)\n");
			__attribute__((fallthrough));
		case COG_VALUE_VOID:
			COG_PANIC("TS is void");
		case COG_VALUE_NULL:
			COG_PANIC("TS is null");
		case COG_VALUE_CHAR:
			printf("(%lc)\n", this->v_char);
			break;
	}
}
#endif
