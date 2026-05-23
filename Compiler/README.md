# SysY2022 到 Koopa IR 前端

## 项目概述

本项目是一个面向编译原理课程设计的 SysY2022 编译器前端。项目使用 Flex 完成词法分析，使用 Bison 完成语法分析，使用 C++17 编写 AST 节点、符号表和语义结构，并从 AST 生成 Koopa IR 文本。

当前编译器重点实现 SysY 中 `int` / `float` 标量、数组以及常见控制流和函数调用相关的前端子集，适合作为课程阶段性检查和后续后端对接基础。本项目当前不包含 RISC-V 后端。

## 当前支持的 SysY 子集

- `int`、`float` 和 `void` 函数
- 多函数定义
- 函数参数与函数调用
- 内置函数声明：`getint`、`getch`、`putint`、`putch`
- 全局 `int` 变量与 `const int` 常量
- 局部 `int` 变量与 `const int` 常量
- 全局 / 局部 `float` 变量与 `const float` 常量
- `int` 数组，包括局部数组、全局数组和 `const int` 数组
- `float` 数组，包括局部数组、全局数组和 `const float` 数组
- 一维或多维数组元素访问
- 数组的常量聚合初始化
- 数组参数，例如 `int a[]`、`int a[][3]` 和 `float a[]`
- 在赋值、返回值、函数实参和数值表达式中的 `int` / `float` 标量隐式转换
- `float` 条件判断，语义为 `x != 0.0`
- 赋值语句
- 空语句与表达式语句
- 支持变量遮蔽的块级作用域
- 算术表达式、关系表达式、相等表达式、一元表达式和短路逻辑表达式
- `if`、`if else`、`while`、`break` 和 `continue`
- 基础语义检查，包括重复定义、未定义符号、对 `const` 赋值、函数参数数量不匹配、非法使用 `void` 值等

## 当前暂不支持的内容

- 局部数组的非常量初始化表达式
- 十六进制浮点字面量
- 完整 SysY2022 中所有浮点边界情况，例如全部十六进制浮点写法和目标后端相关验证
- 更复杂的数组到指针转换和指针运算
- 只有声明而没有定义的函数
- Koopa IR 到 RISC-V 的后端
- 高级优化
- 完整的语法错误恢复

## 目录结构

```text
include/ast.hpp              AST、符号表、IR 上下文声明
src/ast.cpp                  AST 语义检查与 Koopa IR 生成实现
src/sysy.l                   Flex 词法分析器
src/sysy.y                   Bison 语法分析器
src/main.cpp                 编译器命令行入口
examples/ast_ir_demo.cpp     手动构造 AST 的演示程序
testcases/                   正常和错误 SysY 测试输入
scripts/run_tests.ps1        Windows PowerShell 测试脚本
scripts/run_tests.sh         Linux Bash 测试脚本
docs/                        课程报告辅助文档
CMakeLists.txt               CMake 构建配置
Dockerfile                   Linux/Docker 测试环境配置
```

## Windows 构建方式

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Windows 运行方式

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy
```

编译器会将 Koopa IR 输出到标准输出。

如需保存到文件，可以使用重定向：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy > minimal.koopa
```

## Windows 测试方式

```powershell
.\scripts\run_tests.ps1
```

如果 PowerShell 执行策略阻止脚本运行，可以使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_tests.ps1
```

正常测试的输出会保存到：

```text
outputs/<test-name>.koopa
```

文件名以 `error_` 开头的测试为预期失败测试，脚本会单独检查它们是否按预期报错。

## Linux 构建与测试

在 Ubuntu 上安装依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake flex bison
```

配置并构建：

```bash
cmake -S . -B build
cmake --build build
```

运行单个输入：

```bash
./build/compiler testcases/minimal.sy
```

运行完整回归测试：

```bash
bash scripts/run_tests.sh
```

Linux 测试脚本会自动定位：

```text
build/compiler
build/Debug/compiler
build/Release/compiler
```

正常测试输出会保存到 `outputs/*.koopa`，`error_*.sy` 文件会作为预期失败测试处理。

## Docker 测试方式

Docker 可用于在提交前提供干净的 Linux 验证环境：

```bash
docker build -t sysy-compiler .
docker run --rm sysy-compiler
```

Docker 镜像基于 Ubuntu，安装 CMake、Flex、Bison 和 build-essential，随后构建项目并运行：

```bash
bash scripts/run_tests.sh
```

Windows 环境适合本地开发；Linux 或 Docker 环境更适合作为提交前的最终验收环境。

## 示例

输入：

```c
int add(int a, int b) {
  return a + b;
}

int main() {
  return add(1, 2);
}
```

数组示例：

```c
int get(int a[][3]) {
  return a[1][2];
}

int main() {
  int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
  return get(a);
}
```

编译器会生成类似 `[[i32, 3], 2]` 的嵌套 Koopa 数组类型。

浮点示例：

```c
float addf(float a, float b) {
  return a + b;
}

int main() {
  float a[2] = {1.0, 2};
  return addf(a[0], 1);
}
```

编译器会生成 `f32` 类型、`fadd` 等浮点运算指令、`sitofp` / `fptosi` 等标量转换指令，以及类似 `[f32, 2]` 的嵌套浮点数组类型。浮点字面量格式化逻辑集中在 AST 实现中；当前支持十进制和指数形式浮点字面量，暂不支持十六进制浮点字面量。

代表性 Koopa IR 输出：

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

## Windows 环境说明

- 当前 Windows 测试环境使用 MSVC、CMake、winflexbison、Bison 3.8.2 和 Flex 2.6.4。
- Visual Studio 生成器通常会将可执行文件放在 `build\Debug\` 下。
- CMake 配置中已为 MSVC 目标启用 `/utf-8`。
- winflexbison 生成代码可能出现 `INT*_MIN/MAX` 宏重定义 warning，这些 warning 当前不影响构建和运行。
- `src/sysy.l` 中包含针对 MSVC 的 `isatty` 和 `fileno` 兼容处理。

## 与后端模块的对接

当前前端默认将 Koopa IR 输出到标准输出。后端同学可以通过重定向生成 `.koopa` 文件：

```bash
./build/compiler testcases/minimal.sy > minimal.koopa
```

Windows 下：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy > minimal.koopa
```

建议小组内部约定：

```text
输入：SysY 源码文件 .sy
输出：Koopa IR 文本
前端输出位置：stdout
后端输入方式：读取 .koopa 文件或管道输入
```

后续如果需要统一命令行格式，可以扩展为：

```text
compiler input.sy -koopa -o output.koopa
```

## 说明

本项目当前主要完成课程设计中的前端部分，即从 SysY2022 源码到 Koopa IR 文本生成。RISC-V 后端、中间代码优化以及最终目标代码生成可在该前端输出的 Koopa IR 基础上继续实现。
