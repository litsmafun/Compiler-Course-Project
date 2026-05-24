# SysY2022 编译器课程设计

## 项目简介

本项目是 SysY2022 编译器课程设计项目，当前主要完成从 SysY2022 源程序到 Koopa IR 文本的前端流程：

```text
SysY2022 源码
-> Flex 词法分析
-> Bison 语法分析
-> AST 构建
-> 语义检查
-> Koopa IR 文本生成
```

前端输出的 Koopa IR 可作为后续中间代码优化、Koopa 工具链处理和 RISC-V 后端生成的输入。仓库中也包含后端相关目录，便于后续继续接入汇编生成与运行时链接流程。

## 当前支持的功能

当前实现和测试重点覆盖以下内容：

- `int`、`float`、`void` 类型。
- 函数定义、函数参数、函数调用。
- 全局变量和局部变量。
- `const` 常量。
- 一维数组和多维数组。
- 数组聚合初始化。
- 数组作为函数参数。
- 表达式优先级解析。
- 算术、关系、相等和逻辑表达式。
- `&&` 和 `||` 短路求值。
- `if` / `else` 条件语句。
- `while` 循环。
- `break` / `continue`。
- 块作用域和变量遮蔽。
- 基础语义错误检查。
- Koopa IR 文本生成。

基础语义检查包括重复定义、未定义标识符、对 `const` 对象赋值、函数参数数量或类型不匹配、非法使用 `void` 表达式、数组维度错误、数组下标类型错误、数组初始化元素过多，以及 `break` / `continue` 出现在循环外等情况。

## float 支持与 Koopa 兼容性说明

SysY2022 源语言层面支持 `float`。不过课程参考的北大 MiniC / Koopa 工具链可能不支持原生 `f32` 类型和浮点 Koopa IR 指令。为了提高生成 IR 的兼容性，本项目采用“用 `i32` 模拟 `float`”的 lowering 方案。

本项目不应在 Koopa IR 中输出以下原生浮点 token：

```text
f32
fadd
fsub
fmul
fdiv
sitofp
fptosi
fcmp
```

在语义层面，前端仍然识别并检查 `float` 类型；在 Koopa IR 层面，`float` 会被 lowering 成 `i32`：

- `float` 标量使用 `i32` 存储。
- `float` 数组使用 `i32` 数组存储。
- `float` 函数参数使用 `i32` 传递。
- `float` 函数返回值使用 `i32` 返回。
- `float` 常量使用 IEEE754 single-precision 的 32 位 bit pattern 表示。

例如，源程序：

```c
float addf(float a, float b) {
  return a + b;
}
```

对应的 Koopa IR 思路是：

```koopa
decl @__sysy_fadd(i32, i32): i32

fun @addf(@a: i32, @b: i32): i32 {
%entry:
  %0 = call @__sysy_fadd(@a, @b)
  ret %0
}
```

这不是 Koopa 原生 float IR，而是为了兼容工具链的 lowering 方案。前端通过运行时函数完成浮点运算、比较和类型转换，常见函数包括：

```text
@__sysy_fadd
@__sysy_fsub
@__sysy_fmul
@__sysy_fdiv
@__sysy_fneg
@__sysy_itof
@__sysy_ftoi
@__sysy_flt
@__sysy_fle
@__sysy_fgt
@__sysy_fge
@__sysy_feq
@__sysy_fne
@__sysy_fiszero
@__sysy_fisnonzero
```

后端同学只需要把这些 `@__sysy_*` 看作普通外部函数调用处理。最终链接阶段需要加入 `runtime/float_runtime.c`，由该文件提供实际的 C ABI 运行时实现。

## 目录结构

```text
include/                    头文件目录
include/ast.hpp             AST、类型系统、符号表和 IR 上下文声明
include/backend/            后端相关头文件
src/                        源码目录
src/sysy.l                  Flex 词法分析器
src/sysy.y                  Bison 语法分析器
src/ast.cpp                 AST 语义检查与 Koopa IR 生成实现
src/main.cpp                编译器命令行入口
src/backend/                后端相关实现
runtime/float_runtime.c     i32 模拟 float 所需的运行时函数实现
testcases/                  正常测试和 error_*.sy 预期失败测试
scripts/                    Windows / Linux 测试脚本
docs/                       项目文档和报告辅助材料
examples/                   示例程序或 AST/IR 演示代码
CMakeLists.txt              CMake 构建配置
Dockerfile                  Docker / Linux 测试环境配置
```

## 构建方式

### Windows

在项目根目录执行：

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

运行单个 SysY 文件：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy
```

### Linux

在 Ubuntu 环境中安装依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake flex bison
```

配置并构建：

```bash
cmake -S . -B build
cmake --build build
```

运行单个 SysY 文件：

```bash
./build/compiler testcases/minimal.sy
```

## 测试方式

### 默认测试

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_tests.ps1
```

Linux：

```bash
bash scripts/run_tests.sh
```

默认测试脚本会执行以下工作：

1. 构建项目。
2. 运行正常测试用例。
3. 将生成的 Koopa IR 保存到 `outputs/`。
4. 运行 `error_*.sy` 预期失败测试。
5. 检查语义错误是否被正确触发。

### float 测试

当前仓库包含若干 `float` 相关测试用例，用于验证 `i32` 模拟 `float` 的扩展路径。如果后续添加专用 float 测试脚本，可按以下方式运行：

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_float_tests.ps1
```

Linux：

```bash
bash scripts/run_float_tests.sh
```

float 测试应重点检查生成的 Koopa IR 中不出现 `f32`、`fadd`、`fsub`、`fmul`、`fdiv`、`sitofp`、`fptosi`、`fcmp` 等原生浮点 token，并确认浮点运算被 lowered 为 `@__sysy_*` runtime 调用。

## Docker 测试方式

仓库提供 `Dockerfile`。可以使用 Docker 在 Linux 环境中验证构建和测试流程：

```bash
docker build -t sysy-compiler .
docker run --rm sysy-compiler
```

Docker 方式适合作为提交前的统一环境检查。

## 前后端对接方式

当前前端默认将 Koopa IR 输出到标准输出。可以通过重定向保存为 `.koopa` 文件。

Windows：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy > minimal.koopa
```

Linux：

```bash
./build/compiler testcases/minimal.sy > minimal.koopa
```

后端同学可以读取 `.koopa` 文件继续进行中间代码优化、寄存器分配和 RISC-V 汇编生成。

涉及 `float` 时需要注意：

1. Koopa IR 中的 `float` 已经被 lowering 成 `i32`。
2. `@__sysy_fadd`、`@__sysy_ftoi` 等调用应按普通外部函数调用处理。
3. 生成 `.s` 后，最终链接阶段需要加入 `runtime/float_runtime.c`。

示例：

```bash
clang output.s runtime/float_runtime.c -o output
```

如果使用 RISC-V 交叉编译环境，需要根据实际后端工具链调整 `clang` target、ABI、运行库和链接命令。本文档不写死不确定的交叉编译 target。

## 当前限制

- 当前项目主要完成前端和 Koopa IR 文本生成，不应夸大为完整 SysY2022 编译器。
- 仓库中包含后端相关目录，但完整 RISC-V 后端和运行验证流程仍需根据课程要求继续完善。
- 不支持完整高级优化。
- 语法错误恢复能力有限，很多语法错误会直接停止分析。
- `float` 采用 `i32` bit pattern 加 runtime call 模拟，不是 Koopa 原生 float。
- 如果运行环境、ABI 或后端调用约定不同，`runtime/float_runtime.c` 的最终链接方式可能需要调整。
- 部分 SysY2022 边界语法和复杂初始化场景可能尚未完全覆盖，应以实际测试结果为准。

## 小组协作建议

- 前端维护重点是 `src/sysy.l`、`src/sysy.y`、`include/ast.hpp` 和 `src/ast.cpp`。
- 后端可以从前端输出的 Koopa IR 或 Koopa raw program 接入。
- 涉及 `float` 时，后端无需实现原生浮点 Koopa 指令，只需处理普通 `i32` 数据和普通外部函数调用。
- 提交前建议至少运行默认测试脚本；涉及 float 改动时，应额外检查 Koopa IR 中是否仍残留原生浮点 token。
