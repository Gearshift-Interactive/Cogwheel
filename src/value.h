#pragma once

#include "stdint.h"
#include "stdbool.h"

#ifdef DEBUG

#	define VALUE_TYPES \
		X(VOID) \
		X(INT) \
		X(UINT) \
		X(FLOAT) \
		X(BOOL) \
		X(HEAP)

typedef enum {
	VALUE_UNKNOWN = 0,
#	define X(TYPE) VALUE_##TYPE,
	VALUE_TYPES
#	undef X
} ValueType;

#endif

typedef
  struct {
#ifdef DEBUG
	ValueType type;
#endif
	bool isHeap;
	union {
		int64_t v_int;
		uint64_t v_uint;
		double v_float;
		bool v_bool;
		void *v_heap;
	};
} Value;

#ifdef DEBUG
void Value_print(const Value *);
#endif
