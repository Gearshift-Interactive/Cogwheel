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
void Cog_Stack_checkCapacity(Cog_Stack *this)
{
	if (!this->values)
	{
		this->values = malloc(sizeof(Cog_Value) * 64);
		if (!this->values)
			COG_PANIC("Out of memory");
		this->capacity = 64;
		return;
	}
	if (this->count < this->capacity)
		return;
	this->values = realloc(this->values, sizeof(Cog_Value) * this->capacity * 2);
	if (!this->values)
		COG_PANIC("Out of memory");
	this->capacity *= 2;
}
void Cog_Stack_push(Cog_Stack *this, Cog_Value value)
{
	Cog_Stack_checkCapacity(this);
	this->values[this->count++] = value;
}
Cog_Value Cog_Stack_pop(Cog_Stack *this)
{
	if (this->count < 1)
		COG_PANIC("Stack is empty");
	return this->values[--this->count];
}
Cog_Value Cog_Stack_current(Cog_Stack *this)
{
	if (this->count < 1)
		COG_PANIC("Stack is empty");
	return this->values[this->count - 1];
}
Cog_Value *Cog_Stack_currentPtr(Cog_Stack *this)
{
	if (this->count < 1)
		COG_PANIC("Stack is empty");
	return this->values + (this->count - 1);
}
Cog_Value *Cog_Stack_countBack(Cog_Stack *this, size_t count)
{
	if (this->count < 1)
		COG_PANIC("Stack is empty");
	return this->values + (this->count - count);
}
