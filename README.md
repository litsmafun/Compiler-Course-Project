# Compiler-Course-Project

## 编译器使用说明

如需了解完整设计与功能说明，请查看 [Compiler/docs/compiler_overview.md](Compiler/docs/compiler_overview.md)。

开发与测试环境统一以 https://github.com/pku-minic/compiler-dev 为准。

### 构建

```bash
cmake -S Compiler -B Compiler/build
cmake --build Compiler/build
```

### 运行

生成 Koopa IR:

```bash
Compiler/build/compiler -koopa Compiler/testcases/minimal.sy -o /tmp/minimal.koopa
```

生成 RISC-V 汇编:

```bash
Compiler/build/compiler -riscv Compiler/testcases/minimal.sy -o /tmp/minimal.s
```

### 常见用例

```bash
Compiler/build/compiler -riscv Compiler/testcases/function_call.sy -o /tmp/function_call.s
Compiler/build/compiler -riscv Compiler/testcases/if_else.sy -o /tmp/if_else.s
```