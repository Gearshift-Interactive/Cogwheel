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
Type TYPE_NULL_OBJ = {
	.kind = TYPE_NULL,
};
static const char *TypeKind_toString(const TypeKind tk)
{
	switch (tk)
	{
		case TYPE_UNKNOWN: return "UNKNOWN";
#define X(NAME, LITERAL) case TYPE_##NAME: return #LITERAL;
	TYPE_KINDS
#undef X
		case TYPE_ARRAY: return "[]";
		case TYPE_OPTION: return "?";
	}
	return "INVALID";
}
const char *Type_toString(const Type *t)
{
	if (!t)
		return "INVALID";
	if (t->kind != TYPE_OPTION && t->kind != TYPE_ARRAY)
		return (char*)TypeKind_toString(t->kind);
	String_Builder sb = {0};
	if (t->kind == TYPE_ARRAY)
	{
		sb_append_cstr(&sb, Type_toString(t->array.underlying));
		da_append(&sb, '[');
		if (t->array.size)
			sb_appendf(&sb, "%zu", t->array.size);
		sb_appendf(&sb, "]");
	}
	else if (t->kind == TYPE_OPTION)
	{
		sb_append_cstr(&sb, Type_toString(t->option.underlying));
		da_append(&sb, '?');
	}
	nob_sb_append_null(&sb);
	Bank_handOff(sb.items);
	return sb.items;
}
bool Type_areCompatible(const Type *a, const Type *b)
{
	// printf("%p, %p\n", a, b);
	// fflush(stdout);
	if (!a || !b)
		return false;
	if (a->kind == TYPE_OPTION)
	{
		if (b->kind == TYPE_NULL)
			return true;
		if (b->kind == TYPE_OPTION)
			return Type_areCompatible(a->option.underlying, b->option.underlying);
		return Type_areCompatible(a->option.underlying, b);
	}
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
Type *Type_copy(const Type *src)
{
	Type *result = malloc(sizeof *result);
	*result = *src;
	Bank_handOff(result);
	return result;
}
