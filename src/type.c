#include "type.h"

Type TYPE_INT_OBJ = {
	.kind = TYPE_INT,
};
Type TYPE_UINT_OBJ = {
	.kind = TYPE_UINT,
};
Type TYPE_FLOAT_OBJ = {
	.kind = TYPE_FLOAT,
};
Type TYPE_BOOL_OBJ = {
	.kind = TYPE_BOOL,
};
Type TYPE_VOID_OBJ = {
	.kind = TYPE_VOID,
};

const char *Type_toString(const Type *t)
{
	if (!t)
		return "UNKNOWN";
	switch (t->kind)
	{
#define X(NAME, LITERAL) case TYPE_##NAME: return #LITERAL;
	TYPE_KINDS
#undef X
		default: return "INVALID";
	}
}
