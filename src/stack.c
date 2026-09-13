#include "stack.h"
#include "error.h"

#ifdef DEBUG
void Stack_print(const Stack *this)
{
	printf("--STACK--\n");
	if (this->count)
		for (size_t i = 0; i < this->count; i++)
		{
			printf("%zu - ", i);
			Value_print(this->values + i);
		}
	else
		printf("  *empty*\n");
}
#endif
void Stack_checkCapacity(Stack *this)
{
	if (!this->values)
	{
		this->values = malloc(sizeof(Value) * 64);
		if (!this->values)
			PANIC("Out of memory");
		this->capacity = 64;
		return;
	}
	if (this->count < this->capacity)
		return;
	this->values = realloc(this->values, sizeof(Value) * this->capacity * 2);
	if (!this->values)
		PANIC("Out of memory");
	this->capacity *= 2;
}
void Stack_push(Stack *this, Value value)
{
	Stack_checkCapacity(this);
	this->values[this->count++] = value;
}
Value Stack_pop(Stack *this)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values[--this->count];
}
Value Stack_current(Stack *this)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values[this->count - 1];
}
Value *Stack_currentPtr(Stack *this)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values + (this->count - 1);
}
Value *Stack_countBack(Stack *this, size_t count)
{
	if (this->count < 1)
		PANIC("Stack is empty");
	return this->values + (this->count - count);
}
