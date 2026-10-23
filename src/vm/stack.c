#include "stack.h"
#include "error.h"

#ifdef COG_DEBUG
void Cog_Stack_print(const Cog_Stack *this)
{
	printf("--STACK--\n");
	if (this->count)
		for (size_t i = 0; i < this->count; i++)
		{
			printf("%zu - ", i);
			Cog_Value_print(this->values + i);
		}
	else
		printf("  *empty*\n");
}
#endif
void Cog_Stack_init(Cog_Stack *this)
{
	this->values = malloc(sizeof(Cog_Value) * 64);
#ifdef COG_DEBUG
	if (!this->values)
		COG_PANIC("Out of memory");
#endif
	this->capacity = 64;
}
void Cog_Stack_push(Cog_Stack *this, Cog_Value value)
{
	if (this->count >= this->capacity)
	{
		this->values = realloc(this->values, sizeof(Cog_Value) * this->capacity * 2);
#ifdef COG_DEBUG
		if (!this->values)
			COG_PANIC("Out of memory");
#endif
		this->capacity *= 2;
	}
	this->values[this->count++] = value;
}
inline Cog_Value Cog_Stack_pop(Cog_Stack *this)
{
#ifdef COG_DEBUG
	if (this->count < 1)
		COG_PANIC("Stack is empty");
#endif
	return this->values[--this->count];
}
inline Cog_Value Cog_Stack_current(Cog_Stack *this)
{
#ifdef COG_DEBUG
	if (this->count < 1)
		COG_PANIC("Stack is empty");
#endif
	return this->values[this->count - 1];
}
inline Cog_Value *Cog_Stack_currentPtr(Cog_Stack *this)
{
#ifdef COG_DEBUG
	if (this->count < 1)
		COG_PANIC("Stack is empty");
#endif
	return this->values + (this->count - 1);
}
inline Cog_Value *Cog_Stack_countBack(Cog_Stack *this, size_t count)
{
#ifdef COG_DEBUG
	if (this->count < 1)
		COG_PANIC("Stack is empty");
#endif
	return this->values + (this->count - count);
}
