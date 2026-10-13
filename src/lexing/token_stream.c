#include "token_stream.h"

#include "error.h"

Cog_Token Cog_TokenStream_consume(Cog_TokenStream *this)
{
	assert(this);
	fflush(stdout);
	if (this->next >= this->count)
		COG_PANIC("Ran out of tokens");
	Cog_Token token = *(this->items + (this->next++));
	return token;
}
Cog_Token *Cog_TokenStream_current(const Cog_TokenStream *this)
{
	assert(this);
	if (this->next - 1 >= this->count)
		return NULL;
	return this->items + this->next - 1;
}
Cog_Token *Cog_TokenStream_peek(const Cog_TokenStream *this)
{
	assert(this);
	if (this->next >= this->count)
		return NULL;
	return this->items + this->next;
}
Cog_Token *Cog_TokenStream_peekForward(const Cog_TokenStream *this, size_t countForward)
{
	assert(this);
	if (this->next + countForward >= this->count)
		return NULL;
	return this->items + this->next + countForward;
}
void Cog_TokenStream_free(const Cog_TokenStream *this) { assert(this);
	free(this->items);
}
