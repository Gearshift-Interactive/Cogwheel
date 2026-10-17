#include "namespace_builder.h"

static void buildNamespaceImpl(Cog_Namespace *namespace, const Cog_Node *node)
{

}

Cog_Namespace *Cog_buildNamespaceTree(const Cog_Node *node)
{
	Cog_Namespace *result = Cog_Namespace_make(Cog_TokenPosition_fromString("root"));
	buildNamespaceImpl(result, node);
	return result;
}
