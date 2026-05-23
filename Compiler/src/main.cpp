#include "ast.hpp"
#include "sysy_parser.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

extern FILE* yyin;
extern void yyrestart(FILE* input_file);

namespace sysy {

int yyparse(Driver& driver) {
  Parser parser(driver);
  return parser.parse();
}

}  // namespace sysy

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: compiler <input.sy>\n";
    return 1;
  }

  const std::string input_path = argv[1];
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

    std::cout << driver.ast_root->GenerateKoopaIR();
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    std::fclose(input_file);
    return 1;
  }

  std::fclose(input_file);
  return 0;
}
