#include "namespace_builder.h"

void Cog_Module_buildNamespace(Cog_Module *this)
{
	Cog_TokenPosition namespaceName = Cog_TokenPosition_fromString(this->filePath);
	namespaceName.length -= 4;
	while (Cog_TokenPosition_contains(&namespaceName, '/'))
	{
		namespaceName.start++;
		namespaceName.length--;
	}
	this->namespace = Cog_Namespace_make(namespaceName);
	nob_da_foreach(Cog_Node *, child,)
}
