# AI-Assisted AST and Koopa IR Implementation Notes

This document summarizes the parts of the compiler frontend that were produced with AI assistance and can be referenced in a course report.

## AI-Assisted Module Scope

- AST node definitions
- Symbol table
- Koopa IR generation context
- Expression IR generation
- Statement IR generation
- Control-flow IR generation
- Function-call IR generation
- Array declaration, indexing, initialization, and parameter passing
- Float type handling and int/float implicit conversion

## Core Files

- `include/ast.hpp`
- `src/ast.cpp`
- `src/sysy.y`
- `src/sysy.l`
- `src/main.cpp`

## Core Classes

- `BaseAST`: common abstract base for AST nodes.
- `ExprAST`: abstract base for expressions, returning `ExprResult`.
- `StmtAST`: statement node with factory functions for return, assign, if, while, break, continue, block, empty, and expression statements.
- `CompUnitAST`: compilation-unit root that owns top-level declarations and functions.
- `FuncDefAST`: function definition, including return type, name, parameters, and body.
- `BlockAST`: block item list and block-level scope boundary.
- `VarDeclAST`: variable declaration list.
- `ConstDeclAST`: constant declaration list.
- `InitValAST`: scalar and aggregate initializer representation.
- `SymbolTable`: scoped symbol table.
- `IrContext`: Koopa IR output state, temporary names, labels, loop label stack, and current function state.
- `ExprResult`: expression type, IR value name, optional int/float constant values, and void marker.
- `SemanticError`: exception type for semantic diagnostics.

## AST to Koopa IR Flow

1. Flex tokenizes SysY source.
2. Bison constructs AST nodes only; it does not emit IR.
3. `main.cpp` calls `yyparse` and obtains `CompUnitAST`.
4. `CompUnitAST::GenerateKoopaIR` emits builtin declarations, pre-registers function symbols, and then recursively generates global declarations and function bodies.
5. Expression nodes return `ExprResult`; statement and declaration nodes emit instructions through `IrContext`.

## Symbol Table Design

The symbol table uses a scope stack.

- The global scope stores global variables, global constants, and functions.
- Function bodies and blocks push nested scopes.
- Variables, constants, and functions share one namespace in each scope.
- `Define` checks duplicate definitions in the current scope.
- `Lookup` searches from innermost to outermost scope.
- `Require` reports a clear error for undefined identifiers.

This design supports local shadowing while rejecting duplicate definitions in the same block.

## const int / const float Handling

Scalar `const int` and `const float` values are treated as compile-time constants.

- The initializer must evaluate to a constant expression.
- The symbol stores either the constant integer value or the constant floating-point value.
- Uses of the constant are replaced directly by the literal value.
- No `alloc`, `load`, or `store` is generated for scalar constants.

This keeps generated Koopa IR smaller and matches SysY constant-expression requirements.

## Float and Conversion Design

`Type` uses `BasicType::kFloat` together with the existing array and array-parameter flags, so the same structure can represent `float`, `float[2][3]`, `float[]`, and `float[][3]`. `Type::ToKoopaString` maps scalar float to `f32` and nested float arrays to forms such as `[[f32, 3], 2]`.

`ExprResult` contains an optional `double` constant field for compile-time float values. Numeric helper functions centralize conversion and operation emission:

- int-to-float conversion emits `sitofp`.
- float-to-int conversion emits `fptosi`.
- mixed arithmetic promotes operands to float.
- `%` is restricted to int operands.
- float conditions compare against `0.0`.

Float arithmetic currently emits Koopa-style operations such as `fadd`, `fsub`, `fmul`, and `fdiv`; float comparisons use centralized helpers such as `flt`, `fgt`, `feq`, and `fne`. Float literal formatting is also centralized, which keeps future syntax adjustments local if a downstream Koopa tool requires a different spelling.

## Array Design

`Type` represents local/global arrays with dimension vectors such as `{2, 3}` for `int a[2][3]` or `float a[2][3]`. Array parameters are represented separately as array-parameter pointer types, where `int a[][3]` is a pointer to `[i32, 3]` and `float a[][3]` is a pointer to `[f32, 3]`.

Array declarations emit nested Koopa array types:

```koopa
%a = alloc [[i32, 3], 2]
global @g = alloc [i32, 3], {1, 2, 3}
global @fg = alloc [f32, 2], {1.000000000e+00, 2.000000000e+00}
```

`InitValAST` stores either a scalar expression or an aggregate initializer. The current implementation flattens aggregate initializers in row-major order and pads missing elements with zero. For float arrays, int initializer elements are converted to float; for int arrays, float constant elements are converted to int. Local array initialization is limited to constant expressions in this stage.

Array lvalues generate element addresses first. Local and global arrays use `getelemptr` from the array object address. Array parameters are pointer values, so the first dimension uses `getptr`; subsequent dimensions use `getelemptr`.

## Short-Circuit Evaluation

`&&` and `||` cannot be translated as ordinary binary arithmetic operations because the right-hand expression may not be evaluated.

- For `a && b`, if `a` is false, `b` must not run.
- For `a || b`, if `a` is true, `b` must not run.

The implementation emits control flow:

- allocate a temporary result slot,
- branch to the right-hand expression only when needed,
- store the final boolean value,
- join at an end label and load the result.

This preserves side-effect behavior for future expression forms.

## Control Flow Design

`if/else` generation creates unique labels for then, else, and end blocks. Branches that do not terminate explicitly jump to the end block.

`while` generation creates labels for condition, body, and end blocks. The body jumps back to the condition when it is not already terminated.

`break` and `continue` use a loop-label stack:

- `break` jumps to the current loop end label.
- `continue` jumps to the current loop condition label.
- Nested loops work because the innermost loop labels are on top of the stack.

`IrContext` tracks whether the current basic block is terminated. After `ret`, `jump`, or `br`, ordinary instructions must not be emitted into the same block.

## Function Design

Function symbols are pre-registered before function bodies are generated. This allows calls to functions defined later in the file.

Koopa function parameters are SSA values, not writable addresses. SysY parameters behave like ordinary local variables and may be assigned in later language stages. Therefore, each function parameter is copied into a local stack slot at function entry:

```koopa
%a_0 = alloc i32
store @a, %a_0
```

After that, parameter reads and writes use the same `load/store` path as local variables.

## Current Limitations

- Hexadecimal floating-point literals are not supported.
- Local aggregate initialization is currently limited to constant expressions.
- Koopa to RISC-V backend is not implemented.
- More complex array-to-pointer conversions and pointer arithmetic are not supported.
- Full SysY2022 float edge cases are not exhaustively covered.
- Parser error recovery is limited; most syntax errors stop immediately.
