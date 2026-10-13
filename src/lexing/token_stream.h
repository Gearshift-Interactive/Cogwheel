#pragma once

#include "nob.h"
#include "token.h"

typedef struct {
	Cog_Token *items;
	size_t count;
	size_t capacity;
	size_t next;
} Cog_TokenStream;

Cog_Token Cog_TokenStream_consume(Cog_TokenStream *);
Cog_Token *Cog_TokenStream_current(const Cog_TokenStream *);
Cog_Token *Cog_TokenStream_peek(const Cog_TokenStream *);
Cog_Token *Cog_TokenStream_peekForward(const Cog_TokenStream *, size_t countForward);
void Cog_TokenStream_free(const Cog_TokenStream *);
