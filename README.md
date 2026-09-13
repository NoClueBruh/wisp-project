# Wisp -  Simple functional programming language!

#### WISP is a language that is compiled down to bytecode, easily evaluated by the runtime VM.

Having the Compiler separate from the VM means that the language can be compiled into many configurations!

Some examples include:

* Separate Compiler and VM standalones (current project setup).
* Compiled together into an interpreter.
* Compiled together and embedded into another application.

Having no external dependencies means that the compilation processes for both the Compiler and the VM are quite straight forward.

## How to build?

There are two Makefiles, one for the Compiler and one for the VM.

Both are very simple and can easily be configured or straight-up ignored!

# Explaining the language.

The language being functional has two side effects, one more exciting than the other for sure...
