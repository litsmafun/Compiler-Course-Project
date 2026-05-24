/*
 * float_runtime.c — SysY 软浮点运行时
 *
 * 无 libc 依赖，直接用 Linux ecall。
 * 编译命令：
 *   clang -c runtime/float_runtime.c -o /tmp/float_runtime.o \
 *     -target riscv32-unknown-linux-elf -march=rv32imaf -mabi=ilp32 \
 *     -ffreestanding -nostdinc \
 *     -isystem /usr/lib/llvm-21/lib/clang/21/include
 */

typedef int          int32_t;
typedef unsigned int uint32_t;

/* ── bit cast：i32 位模式 <-> float ── */

static float bits_to_float(int32_t bits) {
  float value;
  __builtin_memcpy(&value, &bits, 4);
  return value;
}

static int32_t float_to_bits(float value) {
  int32_t bits;
  __builtin_memcpy(&bits, &value, 4);
  return bits;
}

/* ── Linux syscall 封装（RV32） ── */

#ifdef __riscv

static long sys_write(int fd, const void *buf, unsigned long len) {
  register long a0 __asm__("a0") = fd;
  register long a1 __asm__("a1") = (long)buf;
  register long a2 __asm__("a2") = (long)len;
  register long a7 __asm__("a7") = 64; /* SYS_write */
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static long sys_read(int fd, void *buf, unsigned long len) {
  register long a0 __asm__("a0") = fd;
  register long a1 __asm__("a1") = (long)buf;
  register long a2 __asm__("a2") = (long)len;
  register long a7 __asm__("a7") = 63; /* SYS_read */
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

#else  /* fallback for non-RISC-V (e.g. x86 host build / testing) */

#include <unistd.h>
static long sys_write(int fd, const void *buf, unsigned long len) {
  return (long)write(fd, buf, (size_t)len);
}
static long sys_read(int fd, void *buf, unsigned long len) {
  return (long)read(fd, buf, (size_t)len);
}

#endif /* __riscv */

/* ── putfloat：输出浮点，格式 [-]整数.六位小数 ── */

static void put_char(char c) { sys_write(1, &c, 1); }

static void put_uint(unsigned int x) {
  char buf[12];
  int n = 0;
  if (x == 0) { put_char('0'); return; }
  while (x > 0) { buf[n++] = (char)('0' + x % 10); x /= 10; }
  while (n > 0) put_char(buf[--n]);
}

void putfloat(int32_t bits) {
  float f = bits_to_float(bits);

  /* 符号 */
  if (bits < 0 && f != 0.0f) { put_char('-'); f = -f; }

  /* 整数部分 */
  unsigned int ipart = (unsigned int)(int)f;
  put_uint(ipart);
  put_char('.');

  /* 6 位小数 */
  float frac = f - (float)(int)f;
  if (frac < 0.0f) frac = -frac;
  for (int i = 0; i < 6; ++i) {
    frac *= 10.0f;
    int d = (int)frac;
    put_char((char)('0' + d));
    frac -= (float)d;
  }
}

/* ── getfloat：读入浮点，支持 [-][整数][.小数][e±指数] ── */

static int read_byte(void) {
  unsigned char c;
  long n = sys_read(0, &c, 1);
  return (n <= 0) ? -1 : (int)c;
}

static int is_space(int c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

int32_t getfloat(void) {
  int c;
  while ((c = read_byte()) != -1 && is_space(c)) {}
  if (c == -1) return 0;

  int sign = 1;
  if      (c == '-') { sign = -1; c = read_byte(); }
  else if (c == '+') {            c = read_byte(); }

  float val = 0.0f;

  /* 整数部分 */
  while (c >= '0' && c <= '9') {
    val = val * 10.0f + (float)(c - '0');
    c = read_byte();
  }

  /* 小数部分 */
  if (c == '.') {
    c = read_byte();
    float base = 0.1f;
    while (c >= '0' && c <= '9') {
      val += (float)(c - '0') * base;
      base *= 0.1f;
      c = read_byte();
    }
  }

  /* 指数部分 */
  if (c == 'e' || c == 'E') {
    c = read_byte();
    int esign = 1;
    if      (c == '-') { esign = -1; c = read_byte(); }
    else if (c == '+') {             c = read_byte(); }
    int exp = 0;
    while (c >= '0' && c <= '9') { exp = exp * 10 + (c - '0'); c = read_byte(); }
    for (int i = 0; i < exp; ++i)
      val = (esign > 0) ? val * 10.0f : val / 10.0f;
  }

  if (sign < 0) val = -val;
  return float_to_bits(val);
}

/* ── 算术运算 ── */

int32_t __sysy_fadd(int32_t a, int32_t b) { return float_to_bits(bits_to_float(a) + bits_to_float(b)); }
int32_t __sysy_fsub(int32_t a, int32_t b) { return float_to_bits(bits_to_float(a) - bits_to_float(b)); }
int32_t __sysy_fmul(int32_t a, int32_t b) { return float_to_bits(bits_to_float(a) * bits_to_float(b)); }
int32_t __sysy_fdiv(int32_t a, int32_t b) { return float_to_bits(bits_to_float(a) / bits_to_float(b)); }
int32_t __sysy_fneg(int32_t x)            { return float_to_bits(-bits_to_float(x)); }
int32_t __sysy_itof(int32_t x)            { return float_to_bits((float)x); }
int32_t __sysy_ftoi(int32_t x)            { return (int32_t)bits_to_float(x); }

/* ── 比较运算（返回 0 或 1） ── */

int32_t __sysy_feq(int32_t a, int32_t b)  { return bits_to_float(a) == bits_to_float(b); }
int32_t __sysy_fne(int32_t a, int32_t b)  { return bits_to_float(a) != bits_to_float(b); }
int32_t __sysy_flt(int32_t a, int32_t b)  { return bits_to_float(a) <  bits_to_float(b); }
int32_t __sysy_fle(int32_t a, int32_t b)  { return bits_to_float(a) <= bits_to_float(b); }
int32_t __sysy_fgt(int32_t a, int32_t b)  { return bits_to_float(a) >  bits_to_float(b); }
int32_t __sysy_fge(int32_t a, int32_t b)  { return bits_to_float(a) >= bits_to_float(b); }
int32_t __sysy_fiszero(int32_t x)         { return bits_to_float(x) == 0.0f; }
int32_t __sysy_fisnonzero(int32_t x)      { return bits_to_float(x) != 0.0f; }
