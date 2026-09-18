#include "error.h"
#include <stdarg.h>

bool Cog_errorOccured = false;

// typedef struct {
// 	const char *origin;
// 	size_t start;
// 	size_t length;
// } Cog_TokenPosition;
static size_t TokenPosition_countLineNumber(Cog_TokenPosition pos)
{
	size_t result = 1;
	for (size_t i = 0; i < pos.start && pos.origin[i]; i++)
		if (pos.origin[i] == '\n')
			result++;
	return result;
}
static size_t TokenPosition_countLinePos(Cog_TokenPosition pos)
{
	size_t result = 1;
	for (size_t i = 0; i < pos.start && pos.origin[i]; i++)
		if (pos.origin[i] == '\n')
			result = 0;
		else
			result++;
	return result;
}
static const char *MessageLevel_toString(Cog_MessageLevel level)
{
	switch (level)
	{
#define COG_X(NAME, TEXT) case COG_MESSAGE_##NAME: return #TEXT;
	COG_MESSAGE_LEVELS
#undef COG_X
	}
	return "INVALID";
}
static void printLineNumber(size_t lineNumber)
{
	if (!lineNumber)
		printf("      | ");
	else
		printf("%5zu | ", lineNumber);
}
static void TokenPosition_pprint(Cog_TokenPosition pos, size_t lineNumber, size_t linePos)
{
	size_t curLine = 1;
	// size_t curLinePos = 1;
	bool printnl = false;
	size_t continuationBytesCount = 0;
	if (curLine == lineNumber || curLine == lineNumber - 1)
	{
		printLineNumber(curLine);
		printnl = true;
	}
	for (size_t i = 0; pos.origin[i]; i++)
	{
		if (pos.origin[i] == '\n')
		{
			// curLinePos = 1;
			curLine++;
			if (curLine == lineNumber - 1)
			{
				if (printnl)
					putchar('\n');
				printLineNumber(curLine);
			}
			if (curLine == lineNumber)
			{
				putchar('\n');
				printLineNumber(curLine);
				continuationBytesCount = 0;
			}
			// if (curLine == lineNumber)
			// 	putchar('\n');
			continue;
		}
		if (curLine == lineNumber || curLine == lineNumber - 1)
			putchar(pos.origin[i]);
		if (nob_bytes_for_utf8[(uint8_t)pos.origin[i]] != 1)
			continuationBytesCount += nob_bytes_for_utf8[(uint8_t)pos.origin[i]] - 1;
		if (curLine > lineNumber)
			break;
	}
	putchar('\n');
	printLineNumber(0);
	for (size_t i = 0; i < linePos - ((lineNumber == 1) ? 1 : 0); i++)
		putchar(' ');
	putchar('^');
	for (size_t i = 0; i < pos.length - continuationBytesCount - 1; i++)
		putchar('~');
}
#ifdef COG_DEBUG
void Cog_comptimeMessage_impl(const char *file, size_t ln, Cog_MessageLevel level, Cog_TokenPosition pos, const char *fmt, ...)
#else
void Cog_comptimeMessage_impl(__attribute__((unused)) const char *file, __attribute__((unused)) size_t ln, Cog_MessageLevel level, Cog_TokenPosition pos, const char *fmt, ...)
#endif
{
	va_list args;
	const size_t lineNumber = TokenPosition_countLineNumber(pos);
	const size_t linePos = TokenPosition_countLinePos(pos);

#ifdef COG_DEBUG
	printf("%s:%zu:\n", file, ln);
#endif
	printf("%s:%zu:%zu: %s: ",
		pos.originName,
		lineNumber,
		linePos,
		MessageLevel_toString(level)
	);

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	printf("\n");

	TokenPosition_pprint(pos, lineNumber, linePos);

	printf("\n");

	if (level == COG_MESSAGE_ERROR || level == COG_MESSAGE_ERRORN)
		Cog_errorOccured = true;
	if (level == COG_MESSAGE_ERROR)
		exit(EXIT_FAILURE);
}
