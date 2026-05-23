# AI-Assisted Backend Implementation Notes

This document summarizes the compiler backend and optimization-related logic that was produced with AI assistance and can be referenced in a course report.

## AI-Assisted Module Scope

- Command line options for `-koopa`, `-riscv`, and `-o`.
- Koopa IR parsing via libkoopa and conversion to raw IR.
- RISC-V code generation for RV32I with optional F extension.
- Stack frame layout and basic calling convention handling.
- Global data emission for integers, floats, arrays, and zero-initialized data.
- Basic backend-side simplifications such as immediate loads for constants.

## Core Files

- `include/backend/riscv.hpp`
- `src/backend/riscv.cpp`
- `src/main.cpp`
- `CMakeLists.txt`

## Backend Design Summary

- The frontend still generates Koopa IR from the AST.
- The backend uses libkoopa to parse Koopa IR into raw form, then emits RISC-V assembly.
- Each function computes a stack frame with space for outgoing call arguments, local allocs, and temporary SSA values.
- The prologue saves `ra` and sets up a fixed-size stack frame; the epilogue restores `ra` and returns.
- Global variables are emitted into `.data`, using `.word` for integers and float bit patterns, and `.zero` for zero-initialized regions.
- Integer operations use `add/sub/mul/div/rem`, comparisons use `slt/xor/seqz/snez`, and control flow uses `bnez` and `j`.
- Float operations use `fadd.s/fsub.s/fmul.s/fdiv.s` and comparisons `feq.s/flt.s/fle.s`, with conversions via `fcvt`.
- Array addressing uses `getptr`/`getelemptr` element-size scaling to produce byte offsets.

## Current Limitations

- No advanced optimizations such as SSA-based passes or register allocation.
- All SSA values are stored in stack slots, which is correct but not performance-oriented.
- RISC-V codegen is tuned to the Koopa IR subset emitted by this frontend.
