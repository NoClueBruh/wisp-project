# Wisp -  Simple functional programming language!

#### WISP is a language that is compiled down to bytecode, easily evaluated by the runtime VM.

Having the Compiler separate from the VM means that the language can be compiled into many configurations!

Some examples include:

* Separate Compiler and VM standalones (current project setup).
* Compiled together into an interpreter.
* Compiled together and embedded into another application.

Having no external dependencies means that the compilation processes for both the Compiler and the VM are quite straight forward.

> [!NOTE]
> Currently, there are some very common features that are missing (like exponent operator, string equality).
>
> Most of these are easy to add but were not added just because of other priorities.
>
> Feel free to play around with the Compiler/VM and the very next version might include your additions!
## How to build?

There are two Makefiles, one for the Compiler and one for the VM.

Both are very simple and can easily be configured or straight-up ignored!

# Explaining the language.

### Data types.
The current language version has the following types: 
* `int`
* `bool`
* `char`
* `string`
* `any`
* `list <type>`
* `array <type>`
* `(<type0>, <type1>, <type2> ... <typeN>)` (tuple)
* `<input_type> -> <output_type>` (function)
* `[<input0>, <input1> ... <inputN>] -> <output_type>` (multi-argument function)

Here are some examples:
* `array string` or `array(string)` represents a sequential array of strings.
* `list int` or `list(int)` represents a linked list of integers.
* `int -> int` represents a function that accepts an integer and outputs another integer.
* `int -> int -> int` represents a function that returns a function.
* `(int, bool) -> int` represents a function that accepts a tuple `(int, bool)` and outputs an integer.
* `[int, int] -> int` represents a function that accepts two integers and outputs another integer.

> [!CAUTION]
> There are a few types that might cause confusion!
> 
> `array(int) -> int` is the same as `array(int -> int)` because of the parsing process.
>
> This can be fixed by writing `(array(int)) -> int` or `[array(int)] -> int`.

### Expressions.
#### Operands.
* integers: `1`, `2` etc.
* booleans: `true` or `false`.
* characters: `'x'`, `'\n'` etc.
* strings: `"Hello, World!"`
* lists: `{ v0, v1, v2 }` (all elements must have the same type).
* arrays: `[ v0, v1, v2]` (all elements must have the same type).
* tuples: `(1, 2)`, `(1, false, (true, false))` etc.

#### Function Values.
Function values can be written like so:
```
L. <type0> arg0, <type1> arg1 ... <typeN> argN: <output_expression>
```

Here are some examples:
```
// function that adds two ints together using multiple arguments.
L. int x, int y: x + y
```

```
// function that adds two ints using currying.
L. int x:
   L. int y:
      x + y
```
---
#### Common operators.
Here are the available **arithmetic** operators:
* `+`, adds two ints.
* `-`, either subtracts two ints or negates an integer depending on it's location (example `1 - 1` vs `-10`).
* `*`, multiplies two ints.
* `/`, divides two ints.

Here are the available **logical** operators:
* `&&`, the "and" operator.
* `||`, the "or" operator.
* `!`, the "not" operator.

Here are the available **comparison** operators:
* `==`, `!=`, `>=`, `<=`, `>`, `<`

And finally:
* `?` is a unary operator that checks if a values is NOT `null` (can be used on objects such as tuples, lists, arrays)

### The trinary operator.
The only trinary operator in this language is the `if - else` "operator".
```
if <condition> then
  <pass>
else
  <fail>

// real world example
if x > 10 then
  x
else if x > 0 then
  x * 2
else
  0
```

> [!CAUTION]
> Both branches must have the same type!

### Operations on Values.
Here is a quick overview on operations that can be done on values:

#### Functions.
```
// functions can be called!
// pretty important feature if you ask me!
  func(arg0, arg1, arg2)
```

#### Arrays.
```
// array access, leads to a vague runtime error if the index is out of bounds, so do your own checks!
// <index_expr> should be of type int.
   arr[ <index_expr> ]

// array slice, does NOT produce a runtime error for out of bounds, it returns null as the array.
// <index_start> and <index_end> should be of type int.
// if the value of the start is after the end, the result is null (cannot be used for reversing, at least not yet).
   arr[ <index_start> to <index_to> ]
```

#### Strings.
```
// strings access, leads to a vague runtime error if the index is out of bounds, so do your own checks!
// <index_expr> should be of type int.
// resulting type is char.
   str[ <index_expr> ]

// string slice, does NOT produce a runtime error for out of bounds, it returns null as the string.
// <index_start> and <index_end> should be of type int.
// if the value of the start is after the end, the result is null (cannot be used for reversing, at least not yet).
   str[ <index_start> to <index_to> ]
```

#### Tuples.
```
// tuples access, index is a static value so there are no runtime checks.
  tup.<idx>
```

#### Lists.
The following are operators but it feels right to write them here.
```
// gets the value of the head of the linked list, runtime error if the list is null.
  $lst

// gets the tail of the list, the list after the head.
  ~lst

// inserts an element at the start of the list
  <new_head> -> lst
```

> [!NOTE]
> There will be examples later on!
---
### Constants
constants are value containers with a name and a type.

They can be thought as variables... without the mutability!

Here is an example:
```
const x = 10;         // automatically becomes of type int.
const y:int = 20;     // y has an expected type, the compiler throws an error if the types don't match.
const z:int = y * 30; // y can be used everywhere below it's declaration!
```

Writing the type is very important with recursive functions!
```
// the compiler needs the type of "factorial" because otherwise it wouldn't be able to determine the return value of it's call.
// if that happens, the compiler will act like "factorial" isn't fully defined yet, so it will return a "unknown constant" error.
const factorial: int -> int = L. int n: (
  if n <= 0 then
    1
  else
    factorial(n - 1) * n
);
```
