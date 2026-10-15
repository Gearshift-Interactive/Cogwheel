#pragma once

#include "nob.h"

#define NOBU_DA(T) struct { T* items; size_t count, capacity; }
#define NOBU_DA_ALT(T, ALT) struct { union { T* items, *ALT; }; size_t count, capacity; }
#define NOBU_DA_BARE(T) T* items; size_t count, capacity;
#define NOBU_DA_BARE_ALT(T, ALT) union { T* items, *ALT; }; size_t count, capacity;
