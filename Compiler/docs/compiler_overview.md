# SysY2022 编译器整体介绍

本文档介绍本仓库中 SysY2022 编译器的整体设计与功能，覆盖从 SysY 源码到 Koopa IR 与 RISC-V 汇编的完整流程。

开发与测试环境统一以 https://github.com/pku-minic/compiler-dev 为准。

## 1. 目标与范围

- 输入语言：支持数组、控制流、函数与基础语义检查的 SysY2022 子集。
- 输出：Koopa IR 文本与 RISC-V RV32I 汇编。
- 工具链：Flex + Bison 前端，C++17 实现，libkoopa raw API 解析 IR。
- 浮点策略：float 统一降为 i32 位模式；浮点运算通过运行时调用完成（不使用 RISC-V 浮点指令）。

## 2. 高层流水线

```
SysY 源码 (.sy)
  -> 词法分析 (Flex)
  -> 语法分析 (Bison)
  -> AST + 语义检查
  -> Koopa IR 文本
  -> Koopa 原始 IR (libkoopa)
  -> RISC-V RV32I 汇编
```

## 3. 前端设计

### 3.1 词法与语法

- `src/sysy.l` 定义标识符、关键字、字面量与运算符等 token。
- `src/sysy.y` 构建语法规则并生成 AST 节点。

### 3.2 AST 与语义检查

- `include/ast.hpp` 定义 AST、符号表与 IR 上下文。
- `src/ast.cpp` 负责语义检查，例如：
  - 重复定义
  - 未定义标识符
  - `const` 非法赋值
  - 函数参数数量不匹配
  - 数组维度或初始化大小非法
  - `break`/`continue` 出现在循环外
- 完成检查后，AST 被降为 Koopa IR 文本。

### 3.3 Koopa IR 生成

- 生成与 libkoopa raw API 兼容的 Koopa 文本。
- 浮点以 i32 位模式表示，浮点运算被生成为运行时调用。

## 4. 后端设计（Koopa -> RISC-V）

### 4.1 IR 解析

- `src/backend/riscv.cpp` 解析 Koopa 原始 IR 并输出 RISC-V 汇编。
- 仅使用 RV32I 整数指令，不使用任何浮点寄存器或指令。

### 4.2 调用约定

- 参数：`a0`-`a7`，其余参数写入调用者栈区。
- 返回值：`a0`。
- 栈帧：16 字节对齐，必要时保存 `ra`。

### 4.3 全局与数据布局

- 全局对象在 `.data` 段输出，使用 `.word` 与 `.zero`。
- 数组按 i32 连续内存展平存储。

## 5. 已实现优化

后端包含一些轻量级局部优化：

- 二元运算常量折叠。
- 局部常量传播缓存。
- 二元运算局部公共子表达式消除。
- 未使用值的死代码删除。
- 局部变量的 load-store 前递。
- 常量条件的分支简化。

这些优化均为局部安全优化，不依赖完整的数据流分析。

## 6. 浮点降级策略

- 所有 float 统一降为 i32 位模式。
- 浮点算术与比较通过运行时调用完成（如 `__sysy_fadd`）。
- 后端将其视为普通整数调用，不做浮点特殊处理。

该策略保证后端仅依赖 RV32I，兼容较精简的工具链。

## 7. 运行时库约定

为支持浮点降级与 I/O，前端会在 IR 中生成运行时函数调用，后端只需按普通函数处理：

- 浮点算术与比较：`__sysy_fadd`、`__sysy_fsub`、`__sysy_fmul`、`__sysy_fdiv`、`__sysy_fmod`、`__sysy_fcmp`。
- 浮点与整数转换：`__sysy_fptosi`、`__sysy_sitofp`。
- 基本输入输出（若前端使用）：`getint`、`getch`、`putint`、`putch`、`getfloat`、`putfloat`。

所有参数与返回值均以 i32 位模式传递，调用约定与普通函数一致。

## 8. 构建与运行

在仓库根目录执行：

```bash
cmake -S Compiler -B Compiler/build
cmake --build Compiler/build
```

生成 Koopa IR：

```bash
Compiler/build/compiler -koopa Compiler/testcases/minimal.sy -o /tmp/minimal.koopa
```

生成 RISC-V 汇编：

```bash
Compiler/build/compiler -riscv Compiler/testcases/minimal.sy -o /tmp/minimal.s
```

## 9. 测试与示例

- `Compiler/testcases/` 含 SysY 测试输入与错误用例。
- `Compiler/examples/ast_ir_demo.cpp` 展示手工构造 AST 的示例。

## 10. 已知限制

- 后端仅面向 RV32I，浮点通过运行时调用实现。
- 不支持完整 Koopa 浮点 IR。
- 未实现全局级优化与 SSA 级分析。

## 11. 目录索引（核心文件）

```
include/ast.hpp
src/ast.cpp
src/sysy.l
src/sysy.y
src/main.cpp
src/backend/riscv.cpp
```
