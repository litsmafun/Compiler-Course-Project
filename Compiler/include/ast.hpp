#ifndef SYSY_AST_HPP_
#define SYSY_AST_HPP_

#include <iosfwd>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sysy {

class IrContext;
class ExprAST;
class StmtAST;
class BlockAST;
class InitValAST;

class SemanticError : public std::runtime_error {
 public:
  explicit SemanticError(const std::string& message);
};

enum class BasicType {
  kVoid,
  kInt,
  kFloat,
};

struct Type {
  BasicType basic = BasicType::kInt;
  bool is_array = false;
  bool is_array_parameter = false;
  std::vector<int> array_dims;

  static Type Void();
  static Type Int();
  static Type Float();
  static Type IntArray(std::vector<int> dims);
  static Type FloatArray(std::vector<int> dims);
  static Type IntArrayParameter(std::vector<int> dims);
  static Type FloatArrayParameter(std::vector<int> dims);

  bool IsVoid() const;
  bool IsIntScalar() const;
  bool IsFloatScalar() const;
  bool IsNumericScalar() const;
  bool IsArray() const;
  bool IsArrayParameter() const;
  bool IsFloat() const;
  int ElementCount() const;
  std::string ToKoopaString() const;
  std::string ToKoopaStorageString() const;
  std::string ToDebugString() const;
};

struct ExprResult {
  Type type;
  std::string value_name;
  std::optional<int> const_value;
  std::optional<double> const_float_value;
  bool is_void = false;

  static ExprResult ConstInt(int value);
  static ExprResult ConstFloat(double value);
  static ExprResult RuntimeValue(Type type, std::string value_name);
  static ExprResult Void();

  std::string AsOperand() const;
  std::string AsCallOperand() const;
  void RequireValue(const std::string& usage) const;
  bool IsConstInt() const;
};

enum class SymbolKind {
  kVariable,
  kConstant,
  kFunction,
};

struct Symbol {
  SymbolKind kind = SymbolKind::kVariable;
  std::string source_name;
  std::string ir_name;
  Type type;
  std::optional<int> const_value;
  std::optional<double> const_float_value;
  std::vector<int> const_array_values;
  std::vector<double> const_float_array_values;
  std::vector<Type> param_types;
};

class SymbolTable {
 public:
  SymbolTable();

  void PushScope();
  void PopScope();
  bool IsGlobalScope() const;
  void Define(const Symbol& symbol);
  const Symbol* Lookup(const std::string& name) const;
  const Symbol& Require(const std::string& name) const;

 private:
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

struct LoopLabels {
  std::string break_label;
  std::string continue_label;
};

class IrContext {
 public:
  IrContext();

  SymbolTable& symbols();
  const SymbolTable& symbols() const;

  void EmitBuiltins();
  void EmitGlobalLine(const std::string& line);
  void EmitFunctionLine(const std::string& line);
  void EmitInstruction(const std::string& line);
  void EmitTerminator(const std::string& line);
  void EmitLabel(const std::string& label);
  void EnsureBlockOpen(const std::string& reason) const;

  std::string NewTemp();
  std::string NewLabel(const std::string& hint);
  std::string NewLocalName(const std::string& source_name);
  std::string NewGlobalName(const std::string& source_name);

  bool IsInGlobalScope() const;
  bool IsBlockTerminated() const;
  void MarkBlockTerminated();
  void MarkBlockOpen();

  void BeginFunction(const std::string& ir_name, const Type& return_type,
                     const std::vector<std::pair<std::string, Type>>& params);
  void EndFunction();
  const Type& CurrentFunctionReturnType() const;

  void PushLoop(const std::string& break_label,
                const std::string& continue_label);
  void PopLoop();
  const LoopLabels& CurrentLoop() const;

  ExprResult EmitToBool(const ExprResult& expr);
  ExprResult EmitBinaryRuntime(const std::string& op, const ExprResult& lhs,
                               const ExprResult& rhs);
  ExprResult EmitCall(const std::string& function_ir_name,
                      const Type& return_type,
                      const std::vector<ExprResult>& args);

  std::string GetIR() const;

 private:
  void CheckSupportedType(const Type& type, const std::string& where) const;

  SymbolTable symbols_;
  std::string ir_;
  std::string current_function_ir_name_;
  Type current_function_return_type_;
  bool block_terminated_ = false;
  bool builtins_emitted_ = false;
  int temp_id_ = 0;
  int label_id_ = 0;
  int local_id_ = 0;
  int global_id_ = 0;
  std::vector<LoopLabels> loop_stack_;
};

class BaseAST {
 public:
  virtual ~BaseAST() = default;
  virtual void GenerateKoopaIR(IrContext& context) const = 0;
};

class ExprAST : public BaseAST {
 public:
  void GenerateKoopaIR(IrContext& context) const override;
  virtual ExprResult GenerateExprIR(IrContext& context) const = 0;
  virtual ExprResult GenerateArgumentIR(IrContext& context,
                                        const Type& expected_type) const;
};

class NumberAST final : public ExprAST {
 public:
  explicit NumberAST(int value);
  ExprResult GenerateExprIR(IrContext& context) const override;

 private:
  int value_;
};

class FloatNumberAST final : public ExprAST {
 public:
  explicit FloatNumberAST(double value);
  ExprResult GenerateExprIR(IrContext& context) const override;

 private:
  double value_;
};

class LValAST final : public ExprAST {
 public:
  explicit LValAST(std::string name);
  LValAST(std::string name, std::vector<std::unique_ptr<ExprAST>> indices);
  const std::string& name() const;

  ExprResult GenerateExprIR(IrContext& context) const override;
  ExprResult GenerateArgumentIR(IrContext& context,
                                const Type& expected_type) const override;
  ExprResult GenerateAddressIR(IrContext& context) const;

 private:
  ExprResult GenerateAddressIR(IrContext& context, bool allow_array_result) const;
  std::optional<int> TryConstArrayElement(const Symbol& symbol,
                                          IrContext& context) const;

  std::string name_;
  std::vector<std::unique_ptr<ExprAST>> indices_;
};

enum class UnaryOp {
  kPlus,
  kMinus,
  kNot,
};

class UnaryExpAST final : public ExprAST {
 public:
  UnaryExpAST(UnaryOp op, std::unique_ptr<ExprAST> operand);
  ExprResult GenerateExprIR(IrContext& context) const override;

 private:
  UnaryOp op_;
  std::unique_ptr<ExprAST> operand_;
};

enum class BinaryOp {
  kAdd,
  kSub,
  kMul,
  kDiv,
  kMod,
  kLt,
  kGt,
  kLe,
  kGe,
  kEq,
  kNe,
  kAnd,
  kOr,
};

class BinaryExpAST final : public ExprAST {
 public:
  BinaryExpAST(BinaryOp op, std::unique_ptr<ExprAST> lhs,
               std::unique_ptr<ExprAST> rhs);
  ExprResult GenerateExprIR(IrContext& context) const override;

 private:
  ExprResult GenerateLogicalAndIR(IrContext& context) const;
  ExprResult GenerateLogicalOrIR(IrContext& context) const;

  BinaryOp op_;
  std::unique_ptr<ExprAST> lhs_;
  std::unique_ptr<ExprAST> rhs_;
};

class FuncCallAST final : public ExprAST {
 public:
  FuncCallAST(std::string name, std::vector<std::unique_ptr<ExprAST>> args);
  ExprResult GenerateExprIR(IrContext& context) const override;

 private:
  std::string name_;
  std::vector<std::unique_ptr<ExprAST>> args_;
};

class FuncFParamAST final : public BaseAST {
 public:
  FuncFParamAST(Type type, std::string name);
  void GenerateKoopaIR(IrContext& context) const override;

  const Type& type() const;
  const std::string& name() const;

 private:
  Type type_;
  std::string name_;
};

class InitValAST final {
 public:
  explicit InitValAST(std::unique_ptr<ExprAST> expr);
  explicit InitValAST(std::vector<std::unique_ptr<InitValAST>> elements);

  static std::unique_ptr<InitValAST> MakeScalar(std::unique_ptr<ExprAST> expr);
  static std::unique_ptr<InitValAST> MakeAggregate(
      std::vector<std::unique_ptr<InitValAST>> elements);

  bool IsScalar() const;
  ExprResult GenerateScalarIR(IrContext& context) const;
  std::vector<int> FlattenConst(IrContext& context, int capacity) const;
  std::vector<double> FlattenConstFloat(IrContext& context, int capacity) const;

 private:
  void FlattenConstInto(IrContext& context, std::vector<int>* values) const;
  void FlattenConstFloatInto(IrContext& context,
                             std::vector<double>* values) const;

  std::unique_ptr<ExprAST> expr_;
  std::vector<std::unique_ptr<InitValAST>> elements_;
};

class VarDefAST final : public BaseAST {
 public:
  VarDefAST(Type type, std::string name, std::unique_ptr<InitValAST> init);
  static std::unique_ptr<VarDefAST> MakeInt(std::string name);
  static std::unique_ptr<VarDefAST> MakeIntInit(std::string name,
                                                std::unique_ptr<ExprAST> init);
  static std::unique_ptr<VarDefAST> MakeArray(
      std::string name, std::vector<int> dims, std::unique_ptr<InitValAST> init);

  void GenerateKoopaIR(IrContext& context) const override;
  void GenerateConstKoopaIR(IrContext& context) const;
  void ApplyBaseType(BasicType basic);

 private:
  int RequireConstInit(IrContext& context) const;

  Type type_;
  std::string name_;
  std::unique_ptr<InitValAST> init_;
};

class VarDeclAST final : public BaseAST {
 public:
  VarDeclAST(Type type, std::vector<std::unique_ptr<VarDefAST>> defs);
  static std::unique_ptr<VarDeclAST> MakeInt(
      std::vector<std::unique_ptr<VarDefAST>> defs);
  static std::unique_ptr<VarDeclAST> MakeFloat(
      std::vector<std::unique_ptr<VarDefAST>> defs);

  void GenerateKoopaIR(IrContext& context) const override;

 private:
  Type type_;
  std::vector<std::unique_ptr<VarDefAST>> defs_;
};

class ConstDeclAST final : public BaseAST {
 public:
  ConstDeclAST(Type type, std::vector<std::unique_ptr<VarDefAST>> defs);
  static std::unique_ptr<ConstDeclAST> MakeInt(
      std::vector<std::unique_ptr<VarDefAST>> defs);
  static std::unique_ptr<ConstDeclAST> MakeFloat(
      std::vector<std::unique_ptr<VarDefAST>> defs);

  void GenerateKoopaIR(IrContext& context) const override;

 private:
  Type type_;
  std::vector<std::unique_ptr<VarDefAST>> defs_;
};

enum class StmtKind {
  kExpr,
  kAssign,
  kReturn,
  kIf,
  kWhile,
  kBreak,
  kContinue,
  kBlock,
  kEmpty,
};

class StmtAST final : public BaseAST {
 public:
  static std::unique_ptr<StmtAST> MakeExpr(std::unique_ptr<ExprAST> expr);
  static std::unique_ptr<StmtAST> MakeAssign(std::unique_ptr<LValAST> lval,
                                             std::unique_ptr<ExprAST> expr);
  static std::unique_ptr<StmtAST> MakeReturn(std::unique_ptr<ExprAST> expr);
  static std::unique_ptr<StmtAST> MakeReturnVoid();
  static std::unique_ptr<StmtAST> MakeIf(std::unique_ptr<ExprAST> cond,
                                         std::unique_ptr<StmtAST> then_stmt,
                                         std::unique_ptr<StmtAST> else_stmt);
  static std::unique_ptr<StmtAST> MakeWhile(std::unique_ptr<ExprAST> cond,
                                            std::unique_ptr<StmtAST> body);
  static std::unique_ptr<StmtAST> MakeBreak();
  static std::unique_ptr<StmtAST> MakeContinue();
  static std::unique_ptr<StmtAST> MakeBlock(std::unique_ptr<BlockAST> block);
  static std::unique_ptr<StmtAST> MakeEmpty();

  void GenerateKoopaIR(IrContext& context) const override;

 private:
  explicit StmtAST(StmtKind kind);

  StmtKind kind_;
  std::unique_ptr<LValAST> lval_;
  std::unique_ptr<ExprAST> expr_;
  std::unique_ptr<ExprAST> cond_;
  std::unique_ptr<StmtAST> then_stmt_;
  std::unique_ptr<StmtAST> else_stmt_;
  std::unique_ptr<BlockAST> block_;
};

class BlockAST final : public BaseAST {
 public:
  BlockAST() = default;
  void AddItem(std::unique_ptr<BaseAST> item);
  void GenerateKoopaIR(IrContext& context) const override;

 private:
  std::vector<std::unique_ptr<BaseAST>> items_;
};

class FuncDefAST final : public BaseAST {
 public:
  FuncDefAST(Type return_type, std::string name,
             std::vector<std::unique_ptr<FuncFParamAST>> params,
             std::unique_ptr<BlockAST> body);
  void DeclareFunctionSymbol(IrContext& context) const;
  void GenerateKoopaIR(IrContext& context) const override;

 private:
  Symbol BuildFunctionSymbol() const;
  std::vector<std::pair<std::string, Type>> BuildParamList() const;

  Type return_type_;
  std::string name_;
  std::vector<std::unique_ptr<FuncFParamAST>> params_;
  std::unique_ptr<BlockAST> body_;
};

class CompUnitAST final : public BaseAST {
 public:
  CompUnitAST() = default;
  void AddUnit(std::unique_ptr<BaseAST> unit);
  void GenerateKoopaIR(IrContext& context) const override;
  std::string GenerateKoopaIR() const;

 private:
  std::vector<std::unique_ptr<BaseAST>> units_;
};

std::unique_ptr<NumberAST> MakeNumber(int value);
std::unique_ptr<FloatNumberAST> MakeFloatNumber(double value);
std::unique_ptr<LValAST> MakeLVal(std::string name);
std::unique_ptr<LValAST> MakeLVal(std::string name,
                                  std::vector<std::unique_ptr<ExprAST>> indices);
std::unique_ptr<UnaryExpAST> MakeUnary(UnaryOp op,
                                       std::unique_ptr<ExprAST> operand);
std::unique_ptr<BinaryExpAST> MakeBinary(BinaryOp op,
                                         std::unique_ptr<ExprAST> lhs,
                                         std::unique_ptr<ExprAST> rhs);
std::unique_ptr<FuncCallAST> MakeCall(
    std::string name, std::vector<std::unique_ptr<ExprAST>> args);

}  // namespace sysy

#endif  // SYSY_AST_HPP_
