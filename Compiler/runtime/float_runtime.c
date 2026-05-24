#include <stdint.h>
#include <string.h>

static float bits_to_float(int32_t bits) {
  float value;
  uint32_t raw = (uint32_t)bits;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

static int32_t float_to_bits(float value) {
  uint32_t raw = 0;
  memcpy(&raw, &value, sizeof(raw));
  return (int32_t)raw;
}

int32_t __sysy_fadd(int32_t a, int32_t b) {
  return float_to_bits(bits_to_float(a) + bits_to_float(b));
}

int32_t __sysy_fsub(int32_t a, int32_t b) {
  return float_to_bits(bits_to_float(a) - bits_to_float(b));
}

int32_t __sysy_fmul(int32_t a, int32_t b) {
  return float_to_bits(bits_to_float(a) * bits_to_float(b));
}

int32_t __sysy_fdiv(int32_t a, int32_t b) {
  return float_to_bits(bits_to_float(a) / bits_to_float(b));
}

int32_t __sysy_fneg(int32_t x) {
  return float_to_bits(-bits_to_float(x));
}

int32_t __sysy_itof(int32_t x) {
  return float_to_bits((float)x);
}

int32_t __sysy_ftoi(int32_t x) {
  return (int32_t)bits_to_float(x);
}

int32_t __sysy_feq(int32_t a, int32_t b) {
  return bits_to_float(a) == bits_to_float(b);
}

int32_t __sysy_fne(int32_t a, int32_t b) {
  return bits_to_float(a) != bits_to_float(b);
}

int32_t __sysy_flt(int32_t a, int32_t b) {
  return bits_to_float(a) < bits_to_float(b);
}

int32_t __sysy_fle(int32_t a, int32_t b) {
  return bits_to_float(a) <= bits_to_float(b);
}

int32_t __sysy_fgt(int32_t a, int32_t b) {
  return bits_to_float(a) > bits_to_float(b);
}

int32_t __sysy_fge(int32_t a, int32_t b) {
  return bits_to_float(a) >= bits_to_float(b);
}

int32_t __sysy_fiszero(int32_t x) {
  return bits_to_float(x) == 0.0f;
}

int32_t __sysy_fisnonzero(int32_t x) {
  return bits_to_float(x) != 0.0f;
}
