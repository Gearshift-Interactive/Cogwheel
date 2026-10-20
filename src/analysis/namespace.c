#include "namespace.h"

Cog_Namespace *Cog_Namespace_make(Cog_TokenPosition name, Cog_Namespace *parent)
{
	Cog_Namespace *result = calloc(1, sizeof(Cog_Namespace));
	result->name = name;
	result->parent = parent;
	return result;
}
void Cog_Namespace_addChild(Cog_Namespace *this, Cog_Namespace *child) {
	nob_da_append(&this->children, child);
}
void Cog_Namespace_addItem(Cog_Namespace *this, Cog_VarInfo item) {
	nob_da_append(&this->items, item);
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
Cog_Namespace *Cog_Namespace_searchItem(Cog_Namespace *this, Cog_TokenPosition itemName)
{
	for (Cog_Namespace *current = this; current->parent; current = current->parent)
		nob_da_foreach(Cog_VarInfo, item, &this->items)
			if (Cog_TokenPosition_eq(&itemName, &item->name))
				return current;
	return NULL;
}
Cog_Namespace *Cog_Namespace_searchChild(Cog_Namespace *this, Cog_TokenPosition childName, bool visitParents)
{
	Cog_Namespace *current = this;
	do {
		nob_da_foreach(Cog_Namespace *, child, &current->children)
		if (Cog_TokenPosition_eq(&(*child)->name, &childName))
			return *child;
		if (!visitParents)
		break;
	}
	while (current->parent && (current = current->parent));
	return NULL;
}
Cog_VarInfo *Cog_Namespace_searchItemData(Cog_Namespace *this, Cog_TokenPosition itemName)
{
	nob_da_foreach(Cog_VarInfo, item, &this->items)
		if (Cog_TokenPosition_eq(&itemName, &item->name))
			return item;
	return NULL;
}
void Cog_Namespace_free(Cog_Namespace *this)
{
	nob_da_foreach(Cog_Namespace *, child, &this->children)
		Cog_Namespace_free(*child);
	if (this->children.items)
		free(this->children.items);
	if (this->items.items)
		free(this->items.items);
	free(this);
}
