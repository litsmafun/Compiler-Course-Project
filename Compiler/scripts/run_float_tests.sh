#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
TEST_DIR="${ROOT_DIR}/testcases"
OUTPUT_DIR="${ROOT_DIR}/outputs"
FORBIDDEN_KOOPA_TOKEN_REGEX='(^|[[:space:]])f32([[:space:],)]|$)|=[[:space:]]+(fadd|fsub|fmul|fdiv)[[:space:]]|(^|[[:space:]])(sitofp|fptosi|fcmp)([[:space:],)]|$)'

find_compiler() {
  local candidates=(
    "${BUILD_DIR}/compiler"
    "${BUILD_DIR}/Debug/compiler"
    "${BUILD_DIR}/Release/compiler"
    "${BUILD_DIR}/Debug/compiler.exe"
    "${BUILD_DIR}/Release/compiler.exe"
  )
  for candidate in "${candidates[@]}"; do
    if [[ -x "${candidate}" || -f "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "[BUILD] Configuring project..."
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
fi

echo "[BUILD] Building compiler..."
cmake --build "${BUILD_DIR}"

if ! COMPILER="$(find_compiler)"; then
  echo "[FAIL] compiler executable not found under ${BUILD_DIR}" >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"
had_failure=0

normal_tests=(
  float_basic.sy
  float_var.sy
  float_int_convert.sy
  float_func.sy
  float_compare.sy
  float_array.sy
  float_array_sum.sy
  global_float.sy
  global_float_array.sy
  const_float.sy
  float_array_param.sy
  float_short_circuit.sy
  float_comprehensive.sy
  float_io.sy
  float_get_put.sy
)

error_tests=(
  error_float_mod.sy
  error_float_array_index.sy
  error_assign_const_float.sy
  error_assign_const_float_array.sy
  error_array_param_type.sy
  error_float_missing_return.sy
  error_float_comprehensive.sy
  error_putfloat_arg_count.sy
  error_getfloat_arg_count.sy
)

for test_name in "${normal_tests[@]}"; do
  test_file="${TEST_DIR}/${test_name}"
  if [[ ! -f "${test_file}" ]]; then
    echo "[SKIP] testcases/${test_name}"
    continue
  fi
  base_name="${test_name%.sy}"
  normal_output="${OUTPUT_DIR}/${base_name}.koopa"
  stderr_output="${OUTPUT_DIR}/${base_name}.stderr.txt"
  echo "[RUN] testcases/${test_name}"
  if "${COMPILER}" "${test_file}" >"${normal_output}" 2>"${stderr_output}"; then
    rm -f "${stderr_output}"
    if grep -En "${FORBIDDEN_KOOPA_TOKEN_REGEX}" "${normal_output}"; then
      echo "[FAIL] ${test_name} emitted native float Koopa token"
      had_failure=1
    else
      echo "[OK] ${test_name}"
    fi
  else
    echo "[FAIL] ${test_name}"
    [[ -s "${normal_output}" ]] && cat "${normal_output}"
    [[ -s "${stderr_output}" ]] && cat "${stderr_output}"
    had_failure=1
  fi
done

for test_name in "${error_tests[@]}"; do
  test_file="${TEST_DIR}/${test_name}"
  if [[ ! -f "${test_file}" ]]; then
    echo "[SKIP] testcases/${test_name}"
    continue
  fi
  base_name="${test_name%.sy}"
  error_output="${OUTPUT_DIR}/${base_name}.error.txt"
  echo "[ERROR TEST] testcases/${test_name} expected to fail"
  if "${COMPILER}" "${test_file}" >"${error_output}" 2>&1; then
    echo "[FAIL] ${test_name} expected failure but succeeded"
    [[ -s "${error_output}" ]] && cat "${error_output}"
    had_failure=1
  else
    [[ -s "${error_output}" ]] && cat "${error_output}"
    echo "[OK] ${test_name} expected failure"
  fi
done

if [[ "${had_failure}" -ne 0 ]]; then
  echo "[DONE] Some float tests failed."
  exit 1
fi

echo "[DONE] All float tests completed."
