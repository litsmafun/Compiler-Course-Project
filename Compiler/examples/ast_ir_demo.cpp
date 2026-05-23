#include "ast.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using sysy::BaseAST;
using sysy::BinaryOp;
using sysy::BlockAST;
using sysy::CompUnitAST;
using sysy::ExprAST;
using sysy::FuncDefAST;
using sysy::FuncFParamAST;
using sysy::StmtAST;
using sysy::Type;
using sysy::VarDeclAST;
using sysy::VarDefAST;

std::vector<std::unique_ptr<VarDefAST>> OneDef(
    std::unique_ptr<VarDefAST> def) {
  std::vector<std::unique_ptr<VarDefAST>> defs;
  defs.push_back(std::move(def));
  return defs;
}

std::vector<std::unique_ptr<FuncFParamAST>> NoParams() { return {}; }

std::vector<std::unique_ptr<ExprAST>> CallArgs2(std::unique_ptr<ExprAST> lhs,
                                                std::unique_ptr<ExprAST> rhs) {
  std::vector<std::unique_ptr<ExprAST>> args;
  args.push_back(std::move(lhs));
  args.push_back(std::move(rhs));
  return args;
}

std::unique_ptr<CompUnitAST> BuildExample1() {
  auto comp_unit = std::unique_ptr<CompUnitAST>(new CompUnitAST());
  auto body = std::unique_ptr<BlockAST>(new BlockAST());
  body->AddItem(StmtAST::MakeReturn(sysy::MakeNumber(0)));

  comp_unit->AddUnit(std::unique_ptr<BaseAST>(
      new FuncDefAST(Type::Int(), "main", NoParams(), std::move(body))));
  return comp_unit;
}

std::unique_ptr<CompUnitAST> BuildExample2() {
  auto comp_unit = std::unique_ptr<CompUnitAST>(new CompUnitAST());
  auto body = std::unique_ptr<BlockAST>(new BlockAST());

  body->AddItem(VarDeclAST::MakeInt(
      OneDef(VarDefAST::MakeIntInit("a", sysy::MakeNumber(1)))));
  body->AddItem(VarDeclAST::MakeInt(
      OneDef(VarDefAST::MakeIntInit("b", sysy::MakeNumber(2)))));
  body->AddItem(StmtAST::MakeReturn(sysy::MakeBinary(
      BinaryOp::kAdd, sysy::MakeLVal("a"), sysy::MakeLVal("b"))));

  comp_unit->AddUnit(std::unique_ptr<BaseAST>(
      new FuncDefAST(Type::Int(), "main", NoParams(), std::move(body))));
  return comp_unit;
}

std::unique_ptr<CompUnitAST> BuildExample3() {
  auto comp_unit = std::unique_ptr<CompUnitAST>(new CompUnitAST());

  std::vector<std::unique_ptr<FuncFParamAST>> add_params;
  add_params.push_back(
      std::unique_ptr<FuncFParamAST>(new FuncFParamAST(Type::Int(), "x")));
  add_params.push_back(
      std::unique_ptr<FuncFParamAST>(new FuncFParamAST(Type::Int(), "y")));

  auto add_body = std::unique_ptr<BlockAST>(new BlockAST());
  add_body->AddItem(StmtAST::MakeReturn(sysy::MakeBinary(
      BinaryOp::kAdd, sysy::MakeLVal("x"), sysy::MakeLVal("y"))));
  comp_unit->AddUnit(std::unique_ptr<BaseAST>(new FuncDefAST(
      Type::Int(), "add", std::move(add_params), std::move(add_body))));

  auto main_body = std::unique_ptr<BlockAST>(new BlockAST());
  main_body->AddItem(VarDeclAST::MakeInt(OneDef(VarDefAST::MakeIntInit(
      "a", sysy::MakeCall("add",
                          CallArgs2(sysy::MakeNumber(1),
                                    sysy::MakeNumber(2)))))));

  auto then_block = std::unique_ptr<BlockAST>(new BlockAST());
  then_block->AddItem(StmtAST::MakeReturn(sysy::MakeLVal("a")));

  auto else_block = std::unique_ptr<BlockAST>(new BlockAST());
  else_block->AddItem(StmtAST::MakeReturn(sysy::MakeNumber(0)));

  main_body->AddItem(StmtAST::MakeIf(
      sysy::MakeBinary(BinaryOp::kGt, sysy::MakeLVal("a"),
                       sysy::MakeNumber(2)),
      StmtAST::MakeBlock(std::move(then_block)),
      StmtAST::MakeBlock(std::move(else_block))));

  comp_unit->AddUnit(std::unique_ptr<BaseAST>(
      new FuncDefAST(Type::Int(), "main", NoParams(), std::move(main_body))));
  return comp_unit;
}

void PrintExample(const std::string& title,
                  const std::unique_ptr<CompUnitAST>& comp_unit) {
  std::cout << "===== " << title << " =====\n";
  std::cout << comp_unit->GenerateKoopaIR() << "\n";
}

}  // namespace

int main() {
  try {
    PrintExample("example 1: return zero", BuildExample1());
    PrintExample("example 2: local variables", BuildExample2());
    PrintExample("example 3: call and if else", BuildExample3());
  } catch (const sysy::SemanticError& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
  return 0;
}
