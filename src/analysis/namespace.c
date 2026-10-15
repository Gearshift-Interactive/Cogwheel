#include "namespace.h"

Cog_Namespace *Cog_Namespace_make(Cog_TokenPosition name)
{
	Cog_Namespace *result = calloc(1, sizeof(Cog_Namespace));
	result->name = name;
	return result;
}
static void printIndent(size_t indent)
{
	for (size_t i = 0; i < indent; i++)
		printf("\t");
}
void Cog_Namespace_printImpl(const Cog_Namespace *this, size_t indent)
{
	printIndent(indent); printf("%s {\n", Cog_TokenPosition_toString(&this->name));
	nob_da_foreach(Cog_VarInfo, item, &this->items)
	{
		printIndent(indent + 1);
		if (item->mutable)
			printf("mut ");
		printf("%s %s;\n",
			Cog_Type_toString(item->type),
			Cog_TokenPosition_toString(&item->name)
		);
	}
	nob_da_foreach(Cog_Namespace *, child, &this->children)
		Cog_Namespace_printImpl(*child, indent + 1);
	printIndent(indent); printf("}\n");
}
void Cog_Namespace_print(const Cog_Namespace *this)
{
	Cog_Namespace_printImpl(this, 0);
}
void Cog_Namespace_free(Cog_Namespace *this)
{
	nob_da_foreach(Cog_Namespace *, child, &this->children)
		Cog_Namespace_free(*child);
	free(this);
}
