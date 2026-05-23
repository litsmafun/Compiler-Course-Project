#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
TEST_DIR="${ROOT_DIR}/testcases"
OUTPUT_DIR="${ROOT_DIR}/outputs"

find_compiler() {
  local candidates=(
    "${BUILD_DIR}/compiler"
    "${BUILD_DIR}/Debug/compiler"
    "${BUILD_DIR}/Release/compiler"
  )

  for candidate in "${candidates[@]}"; do
    if [[ -x "${candidate}" ]]; then
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

while IFS= read -r -d '' test_file; do
  test_name="$(basename "${test_file}")"
  base_name="${test_name%.sy}"

  if [[ "${test_name}" == error_* ]]; then
    echo "[ERROR TEST] testcases/${test_name} expected to fail"
    error_output="${OUTPUT_DIR}/${base_name}.error.txt"
    if "${COMPILER}" "${test_file}" >"${error_output}" 2>&1; then
      echo "[FAIL] ${test_name} expected failure but succeeded"
      if [[ -s "${error_output}" ]]; then
        cat "${error_output}"
      fi
      had_failure=1
    else
      if [[ -s "${error_output}" ]]; then
        cat "${error_output}"
      fi
      echo "[OK] ${test_name} expected failure"
    fi
    continue
  fi

  echo "[RUN] testcases/${test_name}"
  normal_output="${OUTPUT_DIR}/${base_name}.koopa"
  stderr_output="${OUTPUT_DIR}/${base_name}.stderr.txt"
  if "${COMPILER}" "${test_file}" >"${normal_output}" 2>"${stderr_output}"; then
    rm -f "${stderr_output}"
    echo "[OK] ${test_name}"
  else
    echo "[FAIL] ${test_name}"
    if [[ -s "${normal_output}" ]]; then
      cat "${normal_output}"
    fi
    if [[ -s "${stderr_output}" ]]; then
      cat "${stderr_output}"
    fi
    had_failure=1
  fi
done < <(find "${TEST_DIR}" -maxdepth 1 -type f -name '*.sy' -print0 | sort -z)

if [[ "${had_failure}" -ne 0 ]]; then
  echo "[DONE] Some tests failed."
  exit 1
fi

echo "All normal tests passed."
echo "All error tests behaved as expected."
echo "[DONE] All tests completed."
