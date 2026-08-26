#pragma once

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
