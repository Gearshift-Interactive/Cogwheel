#pragma once

#include "token.h"
#include "token_stream.h"

Cog_TokenStream Cog_tokenize(String_View text, const char *filename);
