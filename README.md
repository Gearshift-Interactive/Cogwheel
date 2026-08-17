# Cogwheel
Cogwheel is a programming language.

## Usage

### Data Types

There are 3 core data types:
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

All infix operations can only be performed on two numbers of the same type:
- `53u - 12u`: OK
- `24.23 * 32.9f`: OK
- `56u / 5.23f`: ERR

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

### Keywords

#### `exit`
`exit` keyword lets you immideately close the program with the given exit code.
```
int var1 = 12 + 24;
uint var2 = 100u - (uint)var1;
exit var2;
```
