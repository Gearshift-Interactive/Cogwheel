#include "type.h"

#include "bank.h"
#include "error.h"

Cog_Type COG_TYPE_INT_OBJ = {
	.kind = COG_TYPE_INT,
};
Cog_Type COG_TYPE_UINT_OBJ = {
	.kind = COG_TYPE_UINT,
};
Cog_Type COG_TYPE_FLOAT_OBJ = {
	.kind = COG_TYPE_FLOAT,
};
Cog_Type COG_TYPE_BOOL_OBJ = {
	.kind = COG_TYPE_BOOL,
};
Cog_Type COG_TYPE_VOID_OBJ = {
	.kind = COG_TYPE_VOID,
};
Cog_Type COG_TYPE_NULL_OBJ = {
	.kind = COG_TYPE_NULL,
};
Cog_Type COG_TYPE_CHAR_OBJ = {
	.kind = COG_TYPE_CHAR,
};
Cog_Type COG_TYPE_STRING_OBJ = {
	.kind = COG_TYPE_ARRAY,
	.array = {
		.underlying = &COG_TYPE_CHAR_OBJ,
	},
};
static const char *TypeKind_toString(const Cog_TypeKind tk)
{
	switch (tk)
	{
		case COG_TYPE_ALIAS:
		case COG_TYPE_UNKNOWN:
			return "UNKNOWN";
#define COG_X(NAME, LITERAL) case COG_TYPE_##NAME: return #LITERAL;
	COG_TYPE_KINDS
#undef COG_X
		case COG_TYPE_ARRAY: return "[]";
		case COG_TYPE_OPTION: return "?";
		case COG_TYPE_FUNCTION: return "()";
	}
	return "INVALID";
}
const char *Cog_Type_toString(const Cog_Type *t)
{
	if (!t)
		return "INVALID";
	if (t->kind == COG_TYPE_ALIAS)
		return Cog_TokenPosition_toString(&t->alias.name.pos);
	if (t->kind != COG_TYPE_OPTION && t->kind != COG_TYPE_ARRAY && t->kind != COG_TYPE_FUNCTION)
		return (char*)TypeKind_toString(t->kind);
	String_Builder sb = {0};
	if (t->kind == COG_TYPE_ARRAY)
	{
		sb_append_cstr(&sb, Cog_Type_toString(t->array.underlying));
		da_append(&sb, '[');
		if (t->array.size)
			sb_appendf(&sb, "%zu", t->array.size);
		sb_appendf(&sb, "]");
	}
	else if (t->kind == COG_TYPE_OPTION)
	{
		sb_append_cstr(&sb, Cog_Type_toString(t->option.underlying));
		da_append(&sb, '?');
	}
	else if (t->kind == COG_TYPE_FUNCTION)
	{
		sb_append_cstr(&sb, Cog_Type_toString(t->function.retType));
		da_append(&sb, '(');
		da_foreach(Cog_ArgInfo, param, &t->function.args)
		{
			if (param->isMutable)
				sb_append_cstr(&sb, "mut ");
			sb_append_cstr(&sb, Cog_Type_toString(param->type));
			da_append(&sb, ',');
		}
		if (t->function.varArgItem)
		{
			if (t->function.varArgItem->isMutable)
				sb_append_cstr(&sb, "mut ");
			sb_append_cstr(&sb, Cog_Type_toString(t->function.varArgItem->type));
			sb_append_cstr(&sb, "...");
		}
		da_append(&sb, ')');
	}
	nob_sb_append_null(&sb);
	Cog_Bank_handOff(sb.items);
	return sb.items;
}
bool Cog_Type_areCompatible(const Cog_Type *a, const Cog_Type *b)
{
	// printf("%p, %p\n", a, b);
	// fflush(stdout);
	if (!a || !b)
		return false;
	if (a->kind == COG_TYPE_ALIAS || b->kind == COG_TYPE_ALIAS)
	{
#ifdef COG_DEBUG
		COG_PANIC("Can't compare unresolved aliases");
#else
		exit(1);
#endif
	}
	if (a->kind == COG_TYPE_OPTION)
	{
		if (b->kind == COG_TYPE_NULL)
			return true;
		if (b->kind == COG_TYPE_OPTION)
			return Cog_Type_areCompatible(a->option.underlying, b->option.underlying);
		return Cog_Type_areCompatible(a->option.underlying, b);
	}
	else if (a->kind == COG_TYPE_FUNCTION)
	{
		if (b->kind != COG_TYPE_FUNCTION)
			return false;
		if (a->function.args.count != b->function.args.count)
			return false;
		if (a->function.varArgItem && b->function.varArgItem)
		{
			if (a->function.varArgItem->type && b->function.varArgItem->type)
				if (!Cog_Type_areCompatible(a->function.varArgItem->type, b->function.varArgItem->type))
					return false;
		}
		if (
			(!a->function.varArgItem && b->function.varArgItem) ||
			(a->function.varArgItem && !b->function.varArgItem)
		) return false;
		if (!Cog_Type_areCompatible(a->function.retType, b->function.retType))
			return false;
		for (size_t i = 0; i < a->function.args.count; i++)
			if (!Cog_Type_areCompatible(a->function.args.items[i].type, b->function.args.items[i].type))
				return false;
	}
	if (a->kind != b->kind)
		return false;
	if (a->kind == COG_TYPE_ARRAY)
	{
		if (!Cog_Type_areCompatible(a->array.underlying, b->array.underlying))
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
Cog_Type *Cog_Type_copy(const Cog_Type *src)
{
	Cog_Type *result = malloc(sizeof *result);
	*result = *src;
	Cog_Bank_handOff(result);
	return result;
}
bool Cog_Type_isRef(const Cog_Type *t)
{
	switch (t->kind)
	{
		case COG_TYPE_UNKNOWN:
#define COG_X(NAME, LITERAL) case COG_TYPE_##NAME:
	COG_TYPE_KINDS
#undef COG_X
		case COG_TYPE_OPTION:
		case COG_TYPE_FUNCTION:
			return false;
		case COG_TYPE_ARRAY:
			return true;
		case COG_TYPE_ALIAS:
			COG_PANIC("Can't know if an alias is a refference");
	}
	return false;
}
