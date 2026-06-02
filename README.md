# Compiler-Course-Project

SysY 编译器课程设计仓库。完整设计与功能说明见 [Compiler/docs/compiler_overview.md](Compiler/docs/compiler_overview.md)。

开发与测试环境以 [pku-minic/compiler-dev](https://github.com/pku-minic/compiler-dev) 为准（Docker 镜像 `maxxing/compiler-dev`）。

## 进入开发环境（Docker）

在 **Windows PowerShell** 中（将路径改为你本机仓库位置）：

```powershell
docker run -it --rm `
  -v "E:\Compiler-Course-Project:/workspace" `
  maxxing/compiler-dev bash
```

进入容器后，工作目录为挂载点下的 `Compiler`：

```bash
cd /workspace/Compiler
```

> Linux / macOS 将 `-v` 路径改为本机绝对路径即可，其余命令相同。

---

## 构建与运行流程

以下命令均在容器内、且当前目录为 `/workspace/Compiler` 时执行。

### 1. 编译 compiler（首次或修改源码后）

```bash
cmake -S . -B build && cmake --build build -j
```

### 2. 编译软浮点运行时（含 `float` 的程序需要；一般只需做一次）

编译器对 `float` 运算会生成对 `__sysy_*` 软浮点辅助函数的调用，需单独编译并链接 `runtime/float_runtime.c`：

```bash
clang -c runtime/float_runtime.c -o /tmp/float_runtime.o \
  -target riscv32-unknown-linux-elf \
  -march=rv32imaf -mabi=ilp32 \
  -ffreestanding -nostdinc \
  -isystem /usr/lib/llvm-21/lib/clang/21/include
```

纯 `int` 测例可跳过本步与链接时的 `float_runtime.o`。

### 3. 生成中间代码或 RISC-V 汇编

**RISC-V：**

```bash
./build/compiler -riscv testcases/comprehensive_demo.sy -o /tmp/t.s
```

**Koopa IR：**

```bash
./build/compiler -koopa testcases/comprehensive_demo.sy -o /tmp/t.koopa
less /tmp/t.koopa
```

### 4. 汇编为目标文件

```bash
clang /tmp/t.s -c -o /tmp/t.o \
  -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32
```

### 5. 链接并运行

```bash
ld.lld /tmp/t.o /tmp/float_runtime.o -L/opt/lib/riscv32 -lsysy -o /tmp/t
qemu-riscv32-static /tmp/t
echo $?
```

`$?` 为进程退出码（`main` 的返回值）。

---

## 常用测例

| 说明 | 命令中的源文件 |
|------|----------------|
| 综合演示 | `testcases/comprehensive_demo.sy` |
| 浮点 I/O | `testcases/float_io.sy`、`testcases/float_get_put.sy` |
| 函数调用 | `testcases/function_call.sy` |

将上面第 3 步中的 `testcases/comprehensive_demo.sy` 替换为对应文件即可。

批量浮点相关测试（容器内）：

```bash
bash scripts/run_float_tests.sh
```

---

## 目录说明

```text
Compiler-Course-Project/
  Compiler/              # 编译器源码、测例、文档
    build/               # cmake 构建输出（本地生成，勿提交）
    runtime/             # 软浮点运行时 float_runtime.c
    testcases/           # SysY 测例
    docs/                # 设计说明
  README.md
```
