#pragma once

#include "lexing/lexer.h"
#include "node.h"

Cog_Node *Cog_parse(Cog_TokenStream tokens);
