#include "type.h"

#include "bank.h"
#include "error.h"

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
		case TYPE_ALIAS:
		case TYPE_UNKNOWN:
			return "UNKNOWN";
#define X(NAME, LITERAL) case TYPE_##NAME: return #LITERAL;
	TYPE_KINDS
#undef X
		case TYPE_ARRAY: return "[]";
		case TYPE_OPTION: return "?";
		case TYPE_FUNCTION: return "()";
	}
	return "INVALID";
}
const char *Type_toString(const Type *t)
{
	if (!t)
		return "INVALID";
	if (t->kind == TYPE_ALIAS)
		return TokenPosition_toString(&t->alias.name.pos);
	if (t->kind != TYPE_OPTION && t->kind != TYPE_ARRAY && t->kind != TYPE_FUNCTION)
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
	else if (t->kind == TYPE_FUNCTION)
	{
		sb_append_cstr(&sb, Type_toString(t->function.retType));
		da_append(&sb, '(');
		da_foreach(ArgInfo, param, &t->function.args)
		{
			if (param->isMutable)
				sb_append_cstr(&sb, "mut ");
			sb_append_cstr(&sb, Type_toString(param->type));
			da_append(&sb, ',');
		}
		if (t->function.varArgItem)
		{
			if (t->function.varArgItem->isMutable)
				sb_append_cstr(&sb, "mut ");
			sb_append_cstr(&sb, Type_toString(t->function.varArgItem->type));
			sb_append_cstr(&sb, "...");
		}
		da_append(&sb, ')');
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
	if (a->kind == TYPE_ALIAS || b->kind == TYPE_ALIAS)
		PANIC("Can't compare unresolved aliases");
	if (a->kind == TYPE_OPTION)
	{
		if (b->kind == TYPE_NULL)
			return true;
		if (b->kind == TYPE_OPTION)
			return Type_areCompatible(a->option.underlying, b->option.underlying);
		return Type_areCompatible(a->option.underlying, b);
	}
	else if (a->kind == TYPE_FUNCTION)
	{
		if (b->kind != TYPE_FUNCTION)
			return false;
		if (a->function.args.count != b->function.args.count)
			return false;
		if (a->function.varArgItem && b->function.varArgItem)
		{
			if (a->function.varArgItem->type && b->function.varArgItem->type)
				if (!Type_areCompatible(a->function.varArgItem->type, b->function.varArgItem->type))
					return false;
		}
		if (
			(!a->function.varArgItem && b->function.varArgItem) ||
			(a->function.varArgItem && !b->function.varArgItem)
		) return false;
		if (!Type_areCompatible(a->function.retType, b->function.retType))
			return false;
		for (size_t i = 0; i < a->function.args.count; i++)
			if (!Type_areCompatible(a->function.args.items[i].type, b->function.args.items[i].type))
				return false;
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
bool Type_isRef(const Type *t)
{
	switch (t->kind)
	{
		case TYPE_UNKNOWN:
#define X(NAME, LITERAL) case TYPE_##NAME:
	TYPE_KINDS
#undef X
		case TYPE_OPTION:
		case TYPE_FUNCTION:
			return false;
		case TYPE_ARRAY:
			return true;
		case TYPE_ALIAS:
			PANIC("Can't know if an alias is a refference");
	}
	return false;
}
