#ifndef SYSY_BACKEND_RISCV_HPP_
#define SYSY_BACKEND_RISCV_HPP_

#include <ostream>
#include <string>

namespace sysy {

bool GenerateRiscvFromKoopaIR(const std::string& koopa_ir, std::ostream& out,
                              std::string* error_message);

}  // namespace sysy

#endif  // SYSY_BACKEND_RISCV_HPP_
