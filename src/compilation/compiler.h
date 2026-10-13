#pragma once

#include "parsing/node.h"
#include "vm/vm.h"

Cog_Chunk Cog_compile(const Cog_Node *);
