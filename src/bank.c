#include "bank.h"

#include "nob.h"

typedef struct {
	void **items;
	size_t count, capacity;
} Bank;

Bank bank;

void Bank_init(void)
{
	bank = (Bank){0};
}
void *Bank_alloc(size_t size)
{
	void *result = calloc(1, size);
	da_append(&bank, result);
	// printf("alloc: %zu\n", size);
	// fflush(stdout);
	return result;
}
void *Bank_realloc(void *oldPtr, size_t newSize)
{
	void *result;
	if (oldPtr)
	{
		da_foreach(void*, i, &bank)
			if (*i == oldPtr)
			{
				*i = NULL;
				break;
			}
		result = realloc(oldPtr, newSize);
	} else
		result = Bank_alloc(newSize);
	da_append(&bank, result);
	return result;
}
void Bank_free(void *oldPtr)
{
	if (!oldPtr)
		return;
	da_foreach(void*, i, &bank)
		if (*i == oldPtr)
		{
			*i = 0;
			break;
		}
	free(oldPtr);
}
void Bank_handOff(void *ptr)
{
	da_append(&bank, ptr);
}
void Bank_freeAll(void)
{
	da_foreach(void*, i, &bank)
		if (*i)
			free(*i);
	if (bank.items)
		free(bank.items);
}
