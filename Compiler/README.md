# SysY2022 to Koopa IR Frontend

## Project Overview

This project is a teaching-oriented SysY2022 compiler frontend. It uses Flex for lexical analysis, Bison for parsing, C++17 AST classes for semantic structure, and emits textual Koopa IR from the AST.

The current compiler focuses on the SysY int/float scalar and array frontend subset that is useful for a compiler course checkpoint. It does not include a RISC-V backend.

## Supported SysY Subset

- `int`, `float`, and `void` functions
- Multiple function definitions
- Function parameters and calls
- Builtin declarations for `getint`, `getch`, `putint`, and `putch`
- Global `int` variables and `const int` constants
- Local `int` variables and `const int` constants
- Global/local `float` variables and `const float` constants
- `int` arrays, including local arrays, global arrays, and `const int` arrays
- `float` arrays, including local arrays, global arrays, and `const float` arrays
- Array element access with one or more indices
- Constant aggregate initialization for arrays
- Array parameters such as `int a[]`, `int a[][3]`, and `float a[]`
- Implicit scalar conversion between `int` and `float` in assignment, return, function arguments, and numeric expressions
- Float conditions, using `x != 0.0` semantics
- Assignment statements
- Empty statements and expression statements
- Block scope with shadowing
- Arithmetic, relational, equality, unary, and short-circuit logical expressions
- `if`, `if else`, `while`, `break`, and `continue`
- Basic semantic checks, including duplicate definitions, undefined symbols, assigning to const, argument count mismatch, and invalid use of void values

## Unsupported Features

- Non-constant local array initialization expressions
- Hexadecimal floating-point literals
- Full SysY2022 float edge cases, such as every hexadecimal-float spelling and target-specific backend validation
- More complex array-to-pointer conversions and pointer arithmetic
- Function declarations without definitions
- Koopa IR to RISC-V backend
- Advanced optimization
- Full parser error recovery

## Directory Structure

```text
include/ast.hpp              AST, symbol table, IR context declarations
src/ast.cpp                  AST semantic checks and Koopa IR generation
src/sysy.l                   Flex lexer
src/sysy.y                   Bison parser
src/main.cpp                 Compiler command-line entry
examples/ast_ir_demo.cpp     Manual AST construction demo
testcases/                   Normal and error SysY test inputs
scripts/run_tests.ps1        Windows PowerShell test runner
docs/                        Course report support documents
CMakeLists.txt               Build configuration
```

## Build On Windows

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Run On Windows

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy
```

The compiler prints Koopa IR to standard output.

## Test On Windows

```powershell
.\scripts\run_tests.ps1
```

If PowerShell execution policy blocks the script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_tests.ps1
```

Normal test outputs are written to `outputs/<test-name>.koopa`. Files whose names start with `error_` are expected-failure tests and are reported separately.

## Build And Test On Linux

Install the required toolchain on Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake flex bison
```

Configure and build:

```bash
cmake -S . -B build
cmake --build build
```

Run one input:

```bash
./build/compiler testcases/minimal.sy
```

Run the full regression suite:

```bash
bash scripts/run_tests.sh
```

The Linux script locates `build/compiler`, `build/Debug/compiler`, or `build/Release/compiler`, writes normal Koopa IR outputs to `outputs/*.koopa`, and treats `error_*.sy` files as expected-failure tests.

## Docker

Docker provides a clean Linux validation environment before submission:

```bash
docker build -t sysy-compiler .
docker run --rm sysy-compiler
```

The image is based on Ubuntu, installs CMake/Flex/Bison/build-essential, builds the project, and runs `bash scripts/run_tests.sh`. Windows remains convenient for local MSVC development; Linux or Docker is recommended as a final acceptance check.

## Example

Input:

```c
int add(int a, int b) {
  return a + b;
}

int main() {
  return add(1, 2);
}
```

Array example:

```c
int get(int a[][3]) {
  return a[1][2];
}

int main() {
  int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
  return get(a);
}
```

The compiler emits nested Koopa array types such as `[[i32, 3], 2]`.

Float example:

```c
float addf(float a, float b) {
  return a + b;
}

int main() {
  float a[2] = {1.0, 2};
  return addf(a[0], 1);
}
```

The compiler emits `f32`, float arithmetic such as `fadd`, scalar casts such as `sitofp`/`fptosi`, and nested float arrays such as `[f32, 2]`. Float literal formatting is centralized in the AST implementation; decimal and exponent literals are supported, while hexadecimal float literals are currently rejected by the lexer.

Representative output:

```koopa
decl @getint(): i32
decl @getch(): i32
decl @putint(i32)
decl @putch(i32)

fun @add(@a: i32, @b: i32): i32 {
%entry:
  %a_0 = alloc i32
  store @a, %a_0
  %b_1 = alloc i32
  store @b, %b_1
  %0 = load %a_0
  %1 = load %b_1
  %2 = add %0, %1
  ret %2
}
```

## Windows Notes

- The tested Windows setup uses MSVC, CMake, winflexbison, Bison 3.8.2, and Flex 2.6.4.
- Visual Studio generators place executables under `build\Debug\`.
- The CMake file enables `/utf-8` for MSVC targets.
- winflexbison generated code may warn about `INT*_MIN/MAX` macro redefinitions. These warnings are currently harmless.
- `src/sysy.l` contains MSVC compatibility mappings for `isatty` and `fileno`.
