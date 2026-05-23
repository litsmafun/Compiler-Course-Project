#include "ast.hpp"
#include "backend/riscv.hpp"
#include "sysy_parser.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

extern FILE* yyin;
extern void yyrestart(FILE* input_file);

namespace sysy {

int yyparse(Driver& driver) {
  Parser parser(driver);
  return parser.parse();
}

}  // namespace sysy

int main(int argc, char** argv) {
  bool emit_koopa = false;
  bool emit_riscv = false;
  std::string input_path;
  std::string output_path;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-koopa") {
      emit_koopa = true;
    } else if (arg == "-riscv") {
      emit_riscv = true;
    } else if (arg == "-o") {
      if (i + 1 >= argc) {
        std::cerr << "missing output file after -o\n";
        return 1;
      }
      output_path = argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << "\n";
      return 1;
    } else {
      input_path = std::move(arg);
    }
  }

  if (input_path.empty()) {
    std::cerr << "usage: compiler [-koopa|-riscv] <input.sy> -o <output>\n";
    return 1;
  }
  if (!emit_koopa && !emit_riscv) {
    emit_koopa = true;
  }

  FILE* input_file = std::fopen(input_path.c_str(), "r");
  if (input_file == nullptr) {
    std::cerr << "failed to open input file: " << input_path << "\n";
    return 1;
  }

  yyin = input_file;
  yyrestart(input_file);

  try {
    sysy::Driver driver;
    driver.filename = input_path;

    // yyparse 只构造 AST。Koopa IR 统一从 AST 递归生成，保持 parser 和 IR 解耦。
    if (sysy::yyparse(driver) != 0 || driver.ast_root == nullptr) {
      std::cerr << "parse failed\n";
      std::fclose(input_file);
      return 1;
    }

    std::string koopa_ir = driver.ast_root->GenerateKoopaIR();
    if (emit_riscv) {
      std::ofstream output;
      std::ostream* stream = &std::cout;
      if (!output_path.empty()) {
        output.open(output_path, std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
          std::cerr << "failed to open output file: " << output_path << "\n";
          std::fclose(input_file);
          return 1;
        }
        stream = &output;
      }
      std::string error;
      if (!sysy::GenerateRiscvFromKoopaIR(koopa_ir, *stream, &error)) {
        std::cerr << error << "\n";
        std::fclose(input_file);
        return 1;
      }
    } else if (emit_koopa) {
      if (output_path.empty()) {
        std::cout << koopa_ir;
      } else {
        std::ofstream output(output_path, std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
          std::cerr << "failed to open output file: " << output_path << "\n";
          std::fclose(input_file);
          return 1;
        }
        output << koopa_ir;
      }
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    std::fclose(input_file);
    return 1;
  }

  std::fclose(input_file);
  return 0;
}
