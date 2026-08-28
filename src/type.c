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
bool Type_areCompatible(const Type *a, const Type *b)
{
	// printf("%p, %p\n", a, b);
	// fflush(stdout);
	if (a->kind != b->kind)
		return false;
	if (a->kind == TYPE_ARRAY)
	{
		if (!Type_areCompatible(a->array.underlying, b->array.underlying))
			return false;
		// printf("a: %d, b: %d\n", a->array.size, b->array.size);
		// printf("a: %b, b: %b\n", a->array.size, !b->array.size);
		if (a->array.size && !b->array.size)
			return false;
		if (a->array.size && a->array.size != b->array.size)
			return false;
	}
	return true;
}
