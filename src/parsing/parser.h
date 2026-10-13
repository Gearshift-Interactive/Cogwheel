#pragma once

#include "lexing/token_stream.h"
#include "node.h"

Cog_Node *Cog_parse(Cog_TokenStream tokens);
