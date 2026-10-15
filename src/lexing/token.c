#include "token.h"

#include "bank.h"

const char *Cog_TokenType_toString(const Cog_TokenType tt)
{
	switch (tt)
	{
#define COG_X(name) case COG_TOKEN_##name: return #name; break;
	COG_TOKEN_TYPE
#undef COG_X
	}
	return "INVALID";
}
void Cog_TokenPosition_print(const Cog_TokenPosition tp)
{
	printf("%.*s", (int)tp.length, tp.origin + tp.start);
}
void Cog_Token_print(const Cog_Token token)
{
	printf("%s(", Cog_TokenType_toString(token.type));
	Cog_TokenPosition_print(token.pos);
	printf(")");
}
bool Cog_TokenPosition_eq(const Cog_TokenPosition *a, const Cog_TokenPosition *b)
{
	if (a->length != b->length)
		return false;
	return !memcmp(a->origin + a->start, b->origin + b->start, a->length);
}
bool Cog_TokenPosition_contains(const Cog_TokenPosition *this, char character)
{
	for (size_t i = 0; i < this->length; i++)
		if (*(char*)(this->origin + this->start + i) == character)
			return true;
	return false;
}
char *Cog_TokenPosition_toString(const Cog_TokenPosition *tp)
{
	char *result = calloc(tp->length + 1, sizeof *result);
	Cog_Bank_handOff(result);
	memcpy(result, (char*)tp->origin + tp->start, tp->length);
	return result;
}
Cog_TokenPosition Cog_TokenPosition_fromString(const char *chars)
{
	return (Cog_TokenPosition) {
		.origin = chars,
		.start = 0,
		.length = strlen(chars),
		"inline",
	};
}
