#include "error.h"
#include <stdarg.h>

bool errorOccured = false;

// typedef struct {
// 	const char *origin;
// 	size_t start;
// 	size_t length;
// } TokenPosition;
static size_t TokenPosition_countLineNumber(TokenPosition pos)
{
	size_t result = 1;
	for (size_t i = 0; i < pos.start && pos.origin[i]; i++)
		if (pos.origin[i] == '\n')
			result++;
	return result;
}
static size_t TokenPosition_countLinePos(TokenPosition pos)
{
	size_t result = 1;
	for (size_t i = 0; i < pos.start && pos.origin[i]; i++)
		if (pos.origin[i] == '\n')
			result = 0;
		else
			result++;
	return result;
}
static const char *MessageLevel_toString(MessageLevel level)
{
	switch (level)
	{
#define X(NAME, TEXT) case MESSAGE_##NAME: return #TEXT;
	MESSAGE_LEVELS
#undef X
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
static void TokenPosition_pprint(TokenPosition pos, size_t lineNumber, size_t linePos)
{
	size_t curLine = 1;
	// size_t curLinePos = 1;
	bool printnl = false;
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
			}
			// if (curLine == lineNumber)
			// 	putchar('\n');
			continue;
		}
		if (curLine == lineNumber || curLine == lineNumber - 1)
			putchar(pos.origin[i]);
		if (curLine > lineNumber)
			break;
	}
	putchar('\n');
	printLineNumber(0);
	for (size_t i = 0; i < linePos - ((lineNumber == 1) ? 1 : 0); i++)
		putchar(' ');
	putchar('^');
	for (size_t i = 0; i < pos.length - 1; i++)
		putchar('~');
}
void comptimeMessage(MessageLevel level, TokenPosition pos, const char *fmt, ...)
{
	va_list args;
	const size_t lineNumber = TokenPosition_countLineNumber(pos);
	const size_t linePos = TokenPosition_countLinePos(pos);

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

	if (level == MESSAGE_ERROR || level == MESSAGE_ERRORN)
		errorOccured = true;
	if (level == MESSAGE_ERROR)
		exit(EXIT_FAILURE);
}
