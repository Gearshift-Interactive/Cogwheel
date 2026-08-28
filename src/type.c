#include "type.h"

#include "bank.h"

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
		case TYPE_ARRAY: {
			// TODO: global pointer bank
			// Pointers like these are stored in a global
			// bank and freed later before bytecode execution
			String_Builder sb = {0};
			sb_appendf(&sb, "%s[", Type_toString(t->array.underlying));
			if (t->array.size)
				sb_appendf(&sb, "%zu", t->array.size);
			sb_appendf(&sb, "]");
			nob_sb_append_null(&sb);
			Bank_handOff(sb.items);
			return sb.items;
		}
		default: return "INVALID";
	}
}
