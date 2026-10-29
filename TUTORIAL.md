# Statements

**All** statements require a trailing semicolon.

# Data Types

## Numeric

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

## Boolean

There is `boolean` data type, that can only store two states: `true` or `false`.
There are also `and` and `or` infix and `not` prefix operators.

There are also these operators for numeric comparison that all always return boolean:
- `==`: Equal
- `!=`: Not equal
- `>`: Greater than
- `<`: Less than
- `>=`: Greater than or equal to
- `<=`: Less than or equal to

# Character

Character value stores a single string character.
```
'w', 'b', 'ё', 'عَ'
```
Characters support following escapse sequences:
- `\a`: Alert
- `\b`: Backspace
- `\f`: Formfeed Page Break
- `\n`: Newline
- `\r`: Carriage Return
- `\t`: Horizontal tab
- `\v`: Vertical tab
- `\\`: Backslash
- `\'`: Apostrophe

# Variables

You can declare a variable with `var` keyword, followed by its name, optionally a type, an equal sign and a value:
```
var myVar: int = 12;
```
Variables should always be initialized. The language doesn't allow uninitiallized variables.
Variables are immutable (constant) by default. You can use `mut` to make them mutable.
```
var let: int = 0;
let = let + 1; // ERR

mut var let2: int = 0;
let2 = let2 + 1; // OK
```
Assignment as well as declaration return the set variable value, so you can do stuff like this:
```
var variable: uint = mut var variable3: uint = 2u;
variable2 = variable3 = variable2 + 8;
```
You can also declare a variable without specifying it's type:
```
var something = 10; // int
var somethingTheSequel = new [10u; 0u]; // uint[]
```

# Arrays

Arrays are a sequence of values with fixed size.
```
var testArray = new [ 5u; 12 ];
exit testArray[2u];
```
In this case all 5 elements of the array will be initialized with value of 12.
You can also specify the exact init values of each element
```
var someArray = new [ 2u, 4u, 8u ];
```
If you need to explicitly specify the type of array item, you can use `with` keyword:
```
var someArray = new [ 2u, null, 8u ] with uint?;
```
You can also initialize arrays with no values, but this will require to specify the type of the item:
```
var someArray = new [] with uint?;
```
You can use `#` operator to get the amount of elements in the given array.

Array indexes and sizes can only be of type `uint`

Arrays can also be concatenated:
```
var numbers1 = new [ 1, 2, 3 ];
var numbers2 = new [ 4, 5, 6 ];
var numbers = numbers1 + numbers2;
// 1, 2, 3, 4, 5, 6
```
and repeated:
```
var numbers1 = new [ 1, 2, 3 ];
var numbers = numbers1 * 2u;
// 1, 2, 3, 1, 2, 3
```
`numbers` is a new array, with values from old ones shallowly copied. Old arrays are not modified.

# Strings

Strings are basically nonexistent in this language at runtime. All strings are is
just a bunch of syntactical sugar: Strings here are basically an array of characters:
```
var string: char[] = "Hello, Engineer!";
```
That's it.
Strings can also accept all chars escape sequences, but `\'` was replaces with `\"`.

# Stringification

There is an operator, that converts every atomic type into a string with its representations:
```
var string1: char[] = $12
var string2: char[] = $false
```

# Options

Option is a kind of value, that can have either an actual value of given type or `null`.
```
var request: int? = null;
//              ^ indicates option
```
You can't use this kind of value directly. To use it you need to **unwrap** it with `!`:
```
var number: int = request!;
```
Unwrap operator will throw a runtime error, if the given value contains `null`.
To avoid that you can use **check** operator: `?`, that will return `true`, if the value is
present and `false` when its `null`:
```
var present: bool = request?;
```

# Free Blocks

Free blocks can be used as an expression:
```
var variable: int = {
	mut var inner: int = 10;
	inner = inner + 1;
	yield inner;
} + 1;
```
Block itself creates a new scope, and returns the yielded value.

# Type aliases

You can create type aliases to an existing types like this:
```
alias VectorItem = float;
alias Vector = VectorItem[];
alias Vector2d = VectorItem[2];
alias Vector3d = VectorItem[3];
alias VectorAction = VectorItem(VectorItem, VectorItem);
```
Then you can use them as any other type:
```
var vector: Vector = /***/;
var vector3: Vector3d = /***/;
```

# If statement

If statement is an expression that evalueates to different branches depending on the tesult of the given condition.
```
var cond: boolean = true;
var value: int = if(cond) 1 else 2;
// if statement used in an expression is required to have "else" branch

if (cond) {
	exit 12;
};
// free if statement can have "then" branch only
```

# While loop

While loop executes an expression while the given condition returns `true`.
```
var TARGET: int = 10;
mut var current: int = 0;
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
mut var i: int = 0;
exit while (i < 100) {
	i = i + 1;
	if (i == 50)
		break 0;
} else -1;
```
`else` block evaluates when the loop was executed without `break`s, or wasn't executed at all.

# Functions

Functions can be created with **function literals** like this:
```
var addFunc = (a: int, b: int) -> a + b;
```
And then it can be called:
```
exit addFunc(3, 51);
```
Functions are fist-class-sitizens, so they can be passed around like every other value.
To annotate function type you can use this syntax:
```
var addFunc: int(int, int) = (a: int, b: int) -> a + b;
```
It is important to note, that function arguments also can be mutable or immutable.
```
var sort = (mut array: int[] , condition: boolean(int, int)) -> { ... };
//          ^^^
mut var array1: int[] = new [ ... ];
    var array2: int[] = new [ ... ];
var basicConditon = (int a, int b) -> a > b;

sort(array1, basicConditon); // OK
sort(array2, basicConditon); // ERR
```
You cannot pass immutable refference variables as a mutable function argument.
Functions are also closures. That means that you can use variables declared outside of function body:
```
mut var globalValue = 0;

var increment = () -> globalValue = globalValue + 1;

increment();
increment();
increment();

exit globalValue; // 3
```
```
var add = (a: int) -> (b: int) -> a + b;
exit add(2)(4);
```

## Variadic arguments

You can define a variadic argument using elipsis:
```
var sum = (values: int...) -> { /* * */ };
```
This will create a regular array, containing all the variadics.
Here is an example of a basic variadic function:
```
var sum = (a: int, values: int...) -> {
    mut var result = a;
    mut var i = 0u;
    while (i < #values)
    {
        result = result + values[i];
        i = i + 1u;
    };
    yield result;
};
```
Variadic argument must be the last argument.

# Modules

You can separate your project into multiple different files. Each file is a module.
```
// string.cog
alias String = char[];
public var print = (strs: String...) -> {
    mut var j = 0u;
    while (j < #strs)
    {
        var str = strs[j];
        mut var i = 0u;
        while (i < #str)
        {
            putChar(str[i]);
            i = i + 1u;
        };
        j = j + 1u;
    };
};
public var println = (strs: String...) -> {
    mut var i = 0u;
    while (i < #strs)
    {
        print(strs[i]);
        i = i + 1u;
    };
    putChar('\n');
};
```
```
// main.cog
import string;
string::println("Hello, World!");
```
In this example module `main.cog` imports `string.cog` module with `import` keyword. In `string.cog`
functions `print` and `println` are declared `public`, that means, that they can be accessed from
outside of this module. All imported modules are namespaced, so you need to prefix modules public
values with it's name.

# `exit`

`exit` keyword lets you immediately close the program with the given exit code.
```
var var1: int = 12 + 24;
var var2: uint = 100u - (uint)var1;
exit (int)var2;
```
