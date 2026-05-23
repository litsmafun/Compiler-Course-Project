# SysY2022 编译器前端：Flex/Bison 到 Koopa IR

## 1. 项目简介

本项目是编译原理课程设计中的 **SysY2022 编译器前端模块**。前端部分使用 Flex 完成词法分析，使用 Bison 完成语法分析，并通过 C++17 编写的 AST、符号表和语义检查逻辑生成 Koopa IR 文本。

当前模块负责的主线流程为：

```text
SysY 源码 .sy
→ Flex 词法分析
→ Bison 语法分析
→ AST 构建
→ 语义检查
→ Koopa IR 文本输出
```

本仓库当前不包含完整 RISC-V 后端。后端同学可以基于本前端输出的 Koopa IR 继续完成中间代码优化和 RISC-V 汇编生成。

---

## 2. 关于 float 支持的重要说明

SysY2022 源语言本身包含 `float` 类型、`float` 数组以及 `int` / `float` 隐式转换。因此，当前前端代码中可能保留了部分 `float` 相关实现，例如：

- `float` 关键字和浮点字面量识别；
- `float` 类型信息；
- `float` 变量、常量、函数参数和返回值；
- `int` / `float` 隐式转换；
- 可能的 `f32`、`fadd`、`sitofp`、`fptosi` 等浮点 IR 文本生成逻辑。

但是需要注意：**课程参考的北大 MiniC / Koopa 工具链不一定支持 `f32` 类型和浮点 Koopa IR 指令**。因此，为了保证与后端模块和最终验收环境的兼容性，本项目将 float 视为扩展功能，而不是默认验收路径的一部分。

换句话说：

```text
默认验收路径：int / 数组 / 函数 / 控制流 / Koopa 兼容 IR
float 相关内容：扩展功能，单独测试，不参与默认后端对接和最终验收
```

默认测试脚本应只覆盖官方 Koopa 更可能支持的主线功能，不依赖 float 测试。若仓库中保留了 `float_*.sy`、`global_float*.sy`、`const_float.sy`、`error_float*.sy` 等测试文件，它们应通过单独的 float 扩展测试脚本运行，或仅作为前端扩展能力展示。

建议约定：

```text
后端同学默认只对接非 float 的 Koopa IR；
测试同学默认只将非 float 测试作为主线测试；
float 测试仅在确认工具链支持或作为扩展功能展示时单独运行。
```

---

## 3. 默认主线支持的 SysY 子集

默认主线功能不依赖 float，重点保证可生成较稳定的 Koopa IR。当前前端主线支持：

- `int` 和 `void` 函数；
- 多函数定义；
- 函数参数与函数调用；
- 内置函数声明：`getint`、`getch`、`putint`、`putch`；
- 全局 `int` 变量与 `const int` 常量；
- 局部 `int` 变量与 `const int` 常量；
- 一维和多维 `int` 数组；
- 全局数组、局部数组和 `const int` 数组；
- 数组元素访问；
- 数组常量聚合初始化；
- 数组参数，例如 `int a[]`、`int a[][3]`；
- 赋值语句；
- 空语句和表达式语句；
- 支持变量遮蔽的块级作用域；
- 算术表达式、关系表达式、相等表达式、一元表达式；
- `&&` 和 `||` 的短路求值；
- `if`、`if else`、`while`、`break`、`continue`；
- 基础语义错误检查。

基础语义错误检查包括：

- 重复定义；
- 未定义标识符；
- 对 `const` 变量赋值；
- 对 `const` 数组元素赋值；
- 函数参数数量不匹配；
- 非法使用 `void` 表达式；
- `break` / `continue` 出现在循环外；
- 数组维度非法；
- 数组初始化元素数量超过容量；
- 数组名直接作为标量使用。

---

## 4. float 扩展功能说明

如果启用或手动运行 float 相关测试，前端可能支持以下扩展功能：

- `float` 标量变量；
- `const float` 常量；
- `float` 函数参数和返回值；
- `float` 数组；
- `float` 数组聚合初始化；
- `int` / `float` 隐式转换；
- `float` 条件判断；
- `float` 表达式。

但这些功能不保证被北大官方 `koopac` / `libkoopa` 接受。原因是本项目的 Koopa IR 是文本生成，如果直接输出 `f32` 或浮点指令，只有在目标 Koopa 工具链支持这些语法时才能继续被解析和后端处理。

因此，float 建议作为：

```text
扩展功能
实验功能
报告中的额外尝试
```

而不作为：

```text
默认测试要求
默认后端输入
最终验收必须依赖的功能
```

如果需要单独验证 float，可额外提供或使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_float_tests.ps1
```

或 Linux 下：

```bash
bash scripts/run_float_tests.sh
```

如果当前仓库尚未提供上述脚本，也可以手动运行 float 测试文件，但这些测试不应影响默认主线测试结果。

---

## 5. 当前不支持或不作为默认要求的内容

- 默认验收路径不使用 float；
- 十六进制浮点字面量不作为默认支持内容；
- 非常量局部数组初始化表达式可能不完全支持；
- 更复杂的数组到指针转换和指针运算；
- 只有函数声明而没有函数定义的情况；
- Koopa IR 到 RISC-V 后端；
- 高级优化；
- 完整的语法错误恢复；
- 与目标后端强绑定的浮点代码生成验证。

---

## 6. 目录结构

```text
include/ast.hpp              AST、符号表、IR 上下文声明
src/ast.cpp                  AST 语义检查与 Koopa IR 生成实现
src/sysy.l                   Flex 词法分析器
src/sysy.y                   Bison 语法分析器
src/main.cpp                 编译器命令行入口
examples/ast_ir_demo.cpp     手动构造 AST 的演示程序
testcases/                   正常和错误 SysY 测试输入
scripts/run_tests.ps1        Windows 默认测试脚本
scripts/run_tests.sh         Linux 默认测试脚本
docs/                        课程报告辅助文档
CMakeLists.txt               CMake 构建配置
Dockerfile                   Linux / Docker 测试环境配置
.github/workflows/           可选 GitHub Actions 配置
```

---

## 7. Windows 构建方式

在项目根目录执行：

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Visual Studio 生成器通常会将可执行文件放在：

```text
build\Debug\compiler.exe
```

运行单个 SysY 文件：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy
```

编译器会将 Koopa IR 输出到标准输出。

如需保存到文件：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy > minimal.koopa
```

---

## 8. Windows 默认测试方式

默认测试脚本：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_tests.ps1
```

默认测试应覆盖主线功能，包括：

- `int` 标量；
- `int` 数组；
- 函数；
- 表达式；
- 控制流；
- 语义错误检查。

默认测试不应依赖 float。建议默认脚本跳过以下文件：

```text
float_*.sy
global_float*.sy
const_float.sy
error_float*.sy
```

正常测试的 Koopa IR 输出会保存到：

```text
outputs/<test-name>.koopa
```

文件名以 `error_` 开头的测试为预期失败测试，会检查是否正确报错。

如果脚本最后输出：

```text
All normal tests passed.
All error tests behaved as expected.
[DONE] All tests completed.
```

说明默认主线测试通过。

---

## 9. Linux 构建与测试

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

运行默认回归测试：

```bash
bash scripts/run_tests.sh
```

Linux 测试脚本应自动定位：

```text
build/compiler
build/Debug/compiler
build/Release/compiler
```

正常测试输出保存到 `outputs/*.koopa`，`error_*.sy` 文件作为预期失败测试处理。

---

## 10. Docker 测试方式

Docker 可用于提交前的统一 Linux 环境验证：

```bash
docker build -t sysy-compiler .
docker run --rm sysy-compiler
```

Docker 镜像基于 Ubuntu，安装 CMake、Flex、Bison 和 build-essential，随后构建项目并运行默认测试脚本。

推荐使用方式：

```text
Windows：本地开发和快速调试
Linux/Docker：提交前最终测试
```

---

## 11. 示例

输入：

```c
int add(int a, int b) {
  return a + b;
}

int main() {
  return add(1, 2);
}
```

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

编译器会生成类似：

```text
[[i32, 3], 2]
```

的嵌套 Koopa 数组类型。

---

## 12. 与后端模块的对接方式

当前前端默认将 Koopa IR 输出到标准输出。后端同学可以通过重定向得到 `.koopa` 文件。

Linux：

```bash
./build/compiler testcases/minimal.sy > minimal.koopa
```

Windows：

```powershell
.\build\Debug\compiler.exe testcases\minimal.sy > minimal.koopa
```

建议小组内约定：

```text
输入：SysY 源码文件 .sy
输出：Koopa IR 文本
前端输出位置：stdout
后端输入方式：读取 .koopa 文件或从管道读取
```

后端同学默认只需要处理非 float 主线测试生成的 Koopa IR。float 相关 IR 只有在确认目标 Koopa 工具链和后端均支持浮点类型后再考虑接入。

---

## 13. Windows 环境说明

- 当前 Windows 测试环境使用 MSVC、CMake、winflexbison、Bison 3.8.2 和 Flex 2.6.4。
- Visual Studio 生成器通常将可执行文件放在 `build\Debug\`。
- CMake 配置中已为 MSVC 目标启用 `/utf-8`。
- winflexbison 生成代码可能出现 `INT*_MIN/MAX` 宏重定义 warning，该 warning 当前不影响构建和运行。
- `src/sysy.l` 中包含针对 MSVC 的 `isatty` 和 `fileno` 兼容处理。

---

## 14. 小组协作建议

- 前端同学负责维护 `src/sysy.l`、`src/sysy.y`、`include/ast.hpp`、`src/ast.cpp` 和相关测试。
- 后端同学默认对接非 float Koopa IR。
- 测试和报告同学默认使用 `scripts/run_tests.*` 的主线测试结果。
- float 相关测试和结果可作为扩展功能写入报告，但不应影响默认验收路径。
- 合并代码前建议在 Linux 或 Docker 下运行默认测试脚本。

---

## 15. 总结

本项目当前主要完成课程设计中的前端部分，即从 SysY2022 源码到 Koopa IR 文本生成。主线功能以官方 Koopa 兼容为目标，覆盖 `int`、数组、函数、表达式、控制流和基础语义检查。

虽然代码中可能包含 float 相关前端扩展，但由于目标 Koopa 工具链对浮点 IR 支持情况不确定，float 不纳入默认测试和后端对接路径。默认测试和最终验收建议以非 float 主线功能为准。
