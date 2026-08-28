# Cogwheel
Cogwheel is a programming language.

## Usage

### Statements

**All** statements require a trailing semicolon.

### Data Types

#### Numeric

There are 3 numeric data types:
- `int`: signed integer
- `uint`: unsigned integer
- `float`: floating point number

All of them are 64 bits long.
Number literals are also typed:
- `12 547 23`: int
- `12u 547u 23u`: uint
- `12.4 547f 23.0f`: float

You can cast these types with classical C-style cast:
- `(int)12u`: `12`
- `(float)(12u - (uint)8)`: `4.0f`

There are multiple infix math operations:
- **Addition**: `+`
- **Substraction**: `-`
- **Multiplication**: `*`
- **Division**: `/`
- **Power**:  `^`

All infix operations can only be performed on two numbers of the same type:
- `53u - 12u`: OK
- `24.23 * 32.9f`: OK
- `56u / 5.23f`: ERR

#### Boolean

There is `boolean` data type, that can only store two states: `true` or `false`.
There are also `and` and `or` infix and `not` prefix operators.

There are also these operators for numeric comparison that all always return boolean:
- `==`: Equal
- `!=`: Not equal
- `>`: Greater than
- `<`: Less than
- `>=`: Greater than or equal to
- `<=`: Less than or equal to

#### Arrays

Arrays are a sequence of values with fixed size.

```
int testArray = new int[5](12);
// all members will be     ^^
// initialized with 12
exit testArray[2u];
```

Array indexes can only be of type `uint`

### Variables

You can declare a variable with it's type, followed by its name, an equal sign and a value:
```
int myVar = 12;
```
Variables should always be initialized. The language doesn't allow uninitiallized variables.
Variables are immutable (constant) by default. You can use `mut` to make them mutable.
```
int var1 = 0;
var1 = var1 + 1; // ERR

mut int var2 = 0;
var2 = var2 + 1; // OK
```
Assignment as well as declaration return the set variable value, so you can do stuff like this:
```
uint variable = mut uint variable3 = 2u;
variable2 = variable3 = variable2 + 8;
```

### Free Blocks

Free blocks can be used as an expression:
```
int variable = {
	mut int inner = 10;
	inner = inner + 1;
	yield inner;
} + 1;
```
Block itself creates a new scope, and returns the yielded value.

### If statement

If statement is an expression that evalueates to different branches depending on the tesult of the given condition.
```
boolean cond = true;
int value = if(cond) 1 else 2;
// if statement used in an expression is required to have "else" branch

if (cond) {
	exit 12;
}
// free if statement can have only "then" branch
```

### While loop

While loop executes an expression while the given condition returns `true`.
```
int TARGET = 10;
mut int current = 0;
while(current < TARGET)
	current = current + 1;
exit current;  // 10
```
You can also use `break` to immediately exit out of the loop.
```
while (current < TARGET)
{
	if (current == 5)
		break;
	current = current + 1;
};
```
You can also break the loop with a specific value, that will be returned.
But in this case you will also need to provide the `else` block:
```
mut int i = 0;
exit while (i < 100) {
	i = i + 1;
	if (i == 50)
		break 0;
} else -1;
```
`else` block evaluates when the loop was executed without `break`s, or wasn't executed at all.

### Keywords

#### `exit`
`exit` keyword lets you immediately close the program with the given exit code.
```
int var1 = 12 + 24;
uint var2 = 100u - (uint)var1;
exit (int)var2;
```
