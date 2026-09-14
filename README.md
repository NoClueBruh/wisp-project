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

### Data types
The current language version has the following types: 
* `int`
* `bool`
* `char`
* `string`
* `pointer` (useful outside the language itself)
* `list <type>`
* `array <type>`
* `any`, `struct`, `enum` (mentioned later)
* `(<type0>, <type1>, <type2> ... <typeN>)` (tuple)
* `<input_type> -> <output_type>` (function)
* `[<input0>, <input1> ... <inputN>] -> <output_type>` (multi-argument function)
* `void -> <output_type>` (no-argument function)

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

### Expressions
#### Operands
* integers: `1`, `2` or `\xFF` for hex etc.
* booleans: `true` or `false`.
* characters: `'x'`, `'\n'` etc.
* strings: `"Hello, World!"`
* lists: `{ v0, v1, v2 }` (all elements must have the same type).
* arrays: `[ v0, v1, v2]` (all elements must have the same type).
* tuples: `(1, 2)`, `(1, false, (true, false))` etc.

#### Function Values
Function values can be written like so:
```
L. <type0> arg0, <type1> arg1 ... <typeN> argN: <output_expression>
```

Here are some examples:
```
// function that doesn't accept any input (void) and returns 10 when called
L: 10 

// function that adds two ints together using multiple arguments.
L. int x, int y: x + y

// function that adds two ints using currying.
L. int x:
   L. int y:
      x + y
```
---
#### Common operators
Here are the available **arithmetic** operators:
* `+`, adds two ints.
* `-`, either subtracts two ints or negates an integer depending on it's location (example `1 - 1` vs `-10`).
* `*`, multiplies two ints.
* `/`, divides two ints.
* `++`, concatenates two arrays
* `..`, concatenates two strings

Here are the available **logical** operators:
* `&&`, the "and" operator.
* `||`, the "or" operator.
* `!`, the "not" operator.

Here are the available **comparison** operators:
* `==`, `!=`, `>=`, `<=`, `>`, `<`

And finally:
* `?` is a unary operator that checks if a value is NOT `null` (can be used on objects such as tuples, lists, arrays)
---
### The trinary operator
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
---
### The comma operator
The comma operator has 2 different behaviors depending on where it is.

It can be used inside `[ ]`, `{ }`, `( )` in which case it holds the elements.

If it is anywhere else, it acts like the C comma operator where both values are evaluated but only the right one is returned.

```
(1, 2) // this returns a tuple
1, 2   // this returns 2
```

There are a few cases where the behavior may not be so obvious though!
```
L. int x: (
   if x > 0 then
      1, 2
   else
      3, 4
)

// notice how the comma is inside the branches and not outside where it would be treated as a tuple because of the parentheses.
```

To make the behavior more obvious you can use the following syntax
```
body { <exp0>, <exp1> .. <expN> }
```

So the example above can become
```
// looks ugly for this simple example but it is important for more complex functions
L. int x: (
   if x > 0 then body {
      1,
      2
   } else body {
      3,
      4
   }
)
```
---
### Operations on Values
Here is a quick overview on operations that can be done on values:

#### Functions
```
// functions can be called!
// pretty important feature if you ask me!
  func(arg0, arg1, arg2)
```

#### Arrays
```
// array access, leads to a vague runtime error if the index is out of bounds, so do your own checks!
// <index_expr> should be of type int.
   arr [ <index_expr> ]

// array slice, does NOT produce a runtime error for out of bounds, it returns null as the array.
// <index_start> and <index_end> should be of type int.
// if the value of the start is after the end, the result is null (cannot be used for reversing, at least not yet).
   arr [ <index_start> to <index_end> ]

// the length of the array
   arr.length
```

#### Strings
```
// strings access, leads to a vague runtime error if the index is out of bounds, so do your own checks!
// <index_expr> should be of type int.
// resulting type is char.
   str[ <index_expr> ]

// string slice, does NOT produce a runtime error for out of bounds, it returns null as the string.
// <index_start> and <index_end> should be of type int.
// if the value of the start is after the end, the result is null (cannot be used for reversing, at least not yet).
   str[ <index_start> to <index_end> ]

// the length of the string
   str.length
```

#### Tuples
```
// tuple access, index is a static value so there are no runtime checks.
  tup.<idx>
```

#### Lists
The following are operators but it feels right to write them here.
```
// gets the value of the head of the linked list, runtime error if the list is null.
  $lst

// gets the tail of the list, the list after the head, runtime error if the list is null.
  ~lst

// inserts an element at the start of the list
  <new_head> -> lst
```

> [!NOTE]
> There will be examples later on!
---
### Constants
Constants are value containers with a name and a type.

They can be thought as variables... without the mutability!

Here is an example:
```
const x = 10;         // automatically becomes of type int.
const y:int = 20;     // y has an expected type, the compiler throws an error if the types don't match.
const z:int = y * 30; // y can be used everywhere below it's definition!
```

Writing the type is very important for recursive functions!
```
// the compiler needs the type of "factorial" because otherwise it wouldn't be able to determine the return value when its called.
// if that happens, the compiler will act like "factorial" isn't fully defined yet, so it will return a "unknown constant" error.
const factorial: int -> int = L. int n: (
  if n <= 0 then
    1
  else
    factorial(n - 1) * n
);
```

---
### Local variables
You can define local variables inside a function's scope with the following syntax:

```
// simple example where the local variable inherits the type from the value assigned to it.
L. int x: 
  let y = 20 in 
     x + y

// they can be chained!
L. int x:
   let y:int = 20 in
   let z:int = y * 2 in
      x + y + z

// they do not have to be at the top!
// their value is evaluated when their definition is reached.
L. int x:
   if x > 0 then
      let y = 10 * -x in y
   else
      let y = 10 *  x in y
```
---
### Aliases
You can also define aliases inside a function's scope:
```
// aliases visually are exactly the same as local variables.
// the difference is for the runtime, local variables actually take space to hold their value.
// aliases are like copy-pasting their expression which means there is no allocated space at runtime.
// that of course means that every mention of the alias means computation.
L. int x:
   alias magicnum = 10 * 3 * 2 in
      x + magicnum
```

Aliases are really useful for tuples!
```
// remember, this has no runtime cost!
L. (int, int) p:
   alias x = p.0 in
   alias y = p.1 in
      x + y
```

> [!CAUTION]
> Since aliases copy their expression, there are some cases where using them means more work!
> ```
> // in this example, func is called TWICE!
> L. int x:
>    alias y = func(x) in
>       y + y
> ```
---
### Typedefs
Typedefs can be used to refer to a type under a different name.

You can define typedefs like so
```
typedef (int, int) point;
const p: point = (1, 2);

typedef [int, int] -> int operation;
const operations: list(operation) = {
   ( L. int x, int y: x + y ),
   ( L. int x, int y: x - y )
};
```
---
### Enums
Enums are very straight forward to use.

They work like C enums, so there is no runtime overhead, they are just ints!
```
// enums need a typedef to be defined!
typedef enum (
   WORKING,
   SLEEPING,
   FINISHED
) thread_status;

// this is how you get the value!
const status = thread_status.WORKING;
```

You can also manually set the value of each enum entry!
```
typedef enum (
   ENTRY_A = 10, // ENTRY_A is 10
   ENTRY_B,      // ENTRY_B is 11
   ENTRY_C = 99  // ENTRY_C is 99
) example;
```
---
### Structs
Structs are almost exactly like tuples, their only difference is that instead of an index, structs have names for their members!

Internally, they are turned into tuples after compilation so still, no overhead!

```
typedef struct (
   int x,
   int y
) vector;

const v: vector = (1, 2);
const x = v.x;
const y = v.y;
```

You can also have self-referencing struct like so
```
// writing "node" after "struct" declares the struct early so that it can reference itself.
typedef struct node (
   int value,
   node next
) node;

// you can also do funky stuff like this
const add = L. struct(int x, int y) p: (
   p.x + p.y
);

// or even worse
const getnext = L. struct node(int v, node next) p: p.next;
```
---
### Casting
There are a few situations where you may need to cast your values
```
// getting the index of a ascii character
const idx = 'A' as int;

// ascii from index
const oof = 65 as char;

// turning a character into a string
const str = 'A' as string;

// wrapping a value to any
const hmm = 'A' as any;

// unwrapping an any value
const chr = hmm as char;

// when using null.
// null is of type "any", so you will always need to cast null to use properly!
const hm = null as array(int);
```
---
### Any/Typeof
The any type acts as a wrapper for all data types.

The way it works under the hood is zero cost in most cases, so use freely if needed!

As mentioned above, every value can be wrapped like so
```
value as any
```

To check the type of a value you can use the `typeof` instruction
```
const add = L. any x, any y: (
   if x typeof int && y typeof int then
      x as int + y as int
   else
      0
);
```

> [!CAUTION]
> Unwrapping an any value as the incorrect type leads to a runtime error, so use `typeof` before unwrapping!
---
### Include
You can very easily include other scripts into your own with a single statement!
```
include "<relative_path>";
```

Here is an example
```
include "time.txt";

// time_now is a constant defined inside the "time.txt" script!
const now = time_now();
```

---
## Slightly more advanced

### Flags
Currently there are two flags that can be assigned to constants and typedefs.

* `@private` means that the constant/typedef its used on is only visible to that script.
* `@exposed` is used to pass additional information about the constant/typedef to the vm (runtime).

Here is a simple example
```
@private
const api_version = "0.0.1b";
const api_getv    = L: api_version;
// every script that includes this will only have access to the api_getv function
```

Another example
```
@exposed
const scriptver = "0.2.0";
// now the generated bytecode contains one crucial piece of information, where scriptver is on the stack!
// this is important if we want to get the value of a constant at runtime since their names are lost after compilation and their position on the stack depends on many factors!

@exposed
typedef (int, int) vec;
// this exposes the id of the type so the vm can use it if needed to generate it's own values!
```
---
### External functions
Here is one of the most important features of the language, external functions!

Externals are regular C functions, defined by the VM that can be invoked like regular functions from the language!

Here is a simple example
```
// since functions in this language CANNOT return void, it is common to use an int set to 0 or as a error status. 
external printstr: string -> int;

const test = printstr("Hello, World!");
const main = body {
   printstr("Hello, World!"),
   printstr("Hello, mom!"),
   printstr("Hello, dad!")
};
```

> [!CAUTION]
> Since external functions are defined in the VM, this means that some implementations may not have the same functions available!
>
> Some VM implementation may support file operations, some other may have advanced math operations, some other may have ffi bindings etc.
>
> Trying to call a missing external leads to a runtime error.

( the files include a simple ffi implementation using libffi that is commented out, take a look if you are curious )

---
### Variable Arguments
This feature is currently reserved for external functions.

An external function can be given a special type that supports variable arguments!

Here is an example ( these functions are not actually defined in the VM, just an example )
```
// variable number of arguments, but their type is restricted to int
external sum: [..int] -> int;

// variable number AND type of arguments!
external printf: [string, ..] -> int;

const v0 = sum();
const v1 = sum(30);
const v2 = sum(1, -1, 2, 3);

const main = printf("I got these (%d, %d, %d)\n", v0, v1, v2);
```

## Wrapping up

This language was made in 2 months as a passion project.

Realistically speaking, barely anyone will see this or even use this!

If anyone decides to play with this, make sure to look at the examples at `/compiler/examples`.

The header files are commented, the source not so much but I'll work on that!

Again, feel free to play around with the compiler and the VM!
