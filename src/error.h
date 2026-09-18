#pragma once

#include "lexer.h"

#define COG_PANIC(...) do{ fflush(stdout); COG_PANIC_IMPL(__VA_ARGS__); }while(0)

#ifdef COG_DEBUG
#	define COG_PANIC_IMPL(...) do { \
		printf("%s:%d: error: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		exit(EXIT_FAILURE); \
	} while(0)
#else
#	define COG_PANIC_IMPL(...) do { \
		nob_log(ERROR, __VA_ARGS__); \
		exit(EXIT_FAILURE); \
	} while(0)
#endif

#define COG_MESSAGE_LEVELS \
	COG_X(INFO, info) \
	COG_X(WARN, warning) \
	COG_X(ERRORN, error) \
	COG_X(ERROR, error)

typedef enum {
#define COG_X(NAME, TEXT) COG_MESSAGE_##NAME,
	COG_MESSAGE_LEVELS
#undef COG_X
} Cog_MessageLevel;

extern bool Cog_errorOccured;

#define Cog_comptimeMessage(level, pos, ...) Cog_comptimeMessage_impl(__FILE__, __LINE__, level, pos, __VA_ARGS__)
void Cog_comptimeMessage_impl(const char *file, size_t ln, Cog_MessageLevel level, Cog_TokenPosition pos, const char *fmt, ...);
