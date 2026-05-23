# AI 辅助生成的后端实现说明

本文档总结了在 AI 协助下实现的编译器后端与优化相关逻辑，可用于课程报告引用。

## AI 协助模块范围

- 命令行选项 `-koopa`、`-riscv` 与 `-o`。
- 使用 libkoopa 解析 Koopa IR，并转换为 raw IR。
- 面向 RV32I（可选 F 扩展）的 RISC-V 代码生成。
- 栈帧布局与基础调用约定处理。
- 全局数据的输出（int、float、数组与零初始化）。
- 后端侧的基础简化，例如常量的立即数加载。

## 核心文件

- `include/backend/riscv.hpp`
- `src/backend/riscv.cpp`
- `src/main.cpp`
- `CMakeLists.txt`

## 后端设计概要

- 前端仍从 AST 生成 Koopa IR。
- 后端使用 libkoopa 将 Koopa IR 解析为 raw 形式，再生成 RISC-V 汇编。
- 每个函数计算栈帧空间，包含传参溢出区、局部 alloc 与 SSA 临时值。
- 函数序言保存 `ra` 并建立固定大小的栈帧；尾声恢复 `ra` 并返回。
- 全局变量输出到 `.data`，整数用 `.word`，浮点用位模式，零初始化用 `.zero`。
- 整数运算使用 `add/sub/mul/div/rem`，比较使用 `slt/xor/seqz/snez`，控制流使用 `bnez` 与 `j`。
- 浮点运算使用 `fadd.s/fsub.s/fmul.s/fdiv.s`，比较使用 `feq.s/flt.s/fle.s`，转换使用 `fcvt`。
- 数组寻址使用 `getptr`/`getelemptr`，通过元素大小进行偏移计算。

## 当前限制

- 未实现 SSA 级优化或寄存器分配等高级优化。
- 所有 SSA 值都落在栈槽中，正确但性能较保守。
- RISC-V 后端针对当前前端输出的 Koopa IR 子集进行了适配。
