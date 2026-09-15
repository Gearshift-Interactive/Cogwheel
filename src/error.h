#pragma once

#include "lexer.h"

#define PANIC(...) PANIC_IMPL(__VA_ARGS__)

#ifdef DEBUG
#	define PANIC_IMPL(...) do { \
		printf("%s:%d: error: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		exit(EXIT_FAILURE); \
	} while(0)
#else
#	define PANIC_IMPL(...) do { \
		nob_log(ERROR, __VA_ARGS__); \
		exit(EXIT_FAILURE); \
	} while(0)
#endif

#define MESSAGE_LEVELS \
	X(INFO, info) \
	X(WARN, warning) \
	X(ERRORN, error) \
	X(ERROR, error)

typedef enum {
#define X(NAME, TEXT) MESSAGE_##NAME,
	MESSAGE_LEVELS
#undef X
} MessageLevel;

extern bool errorOccured;

#define comptimeMessage(level, pos, ...) comptimeMessage_impl(__FILE__, __LINE__, level, pos, __VA_ARGS__)
void comptimeMessage_impl(const char *file, size_t ln, MessageLevel level, TokenPosition pos, const char *fmt, ...);
