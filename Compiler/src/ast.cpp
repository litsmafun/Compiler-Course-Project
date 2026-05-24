#include "ast.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace sysy {
namespace {

std::string Indent(const std::string& line) { return "  " + line + "\n"; }

std::string MangleLocalHint(const std::string& name) {
  std::string result;
  result.reserve(name.size());
  for (char ch : name) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_') {
      result.push_back(ch);
    } else {
      result.push_back('_');
    }
  }
  if (result.empty()) {
    return "tmp";
  }
  return result;
}

int CalcConstBinary(BinaryOp op, int lhs, int rhs) {
  switch (op) {
    case BinaryOp::kAdd:
      return lhs + rhs;
    case BinaryOp::kSub:
      return lhs - rhs;
    case BinaryOp::kMul:
      return lhs * rhs;
    case BinaryOp::kDiv:
      if (rhs == 0) {
        throw SemanticError("常量表达式中出现除零");
      }
      return lhs / rhs;
    case BinaryOp::kMod:
      if (rhs == 0) {
        throw SemanticError("常量表达式中出现取模零");
      }
      return lhs % rhs;
    case BinaryOp::kLt:
      return lhs < rhs;
    case BinaryOp::kGt:
      return lhs > rhs;
    case BinaryOp::kLe:
      return lhs <= rhs;
    case BinaryOp::kGe:
      return lhs >= rhs;
    case BinaryOp::kEq:
      return lhs == rhs;
    case BinaryOp::kNe:
      return lhs != rhs;
    case BinaryOp::kAnd:
      return (lhs != 0) && (rhs != 0);
    case BinaryOp::kOr:
      return (lhs != 0) || (rhs != 0);
  }
  throw SemanticError("未知二元运算符");
}

std::string KoopaBinaryOp(BinaryOp op) {
  switch (op) {
    case BinaryOp::kAdd:
      return "add";
    case BinaryOp::kSub:
      return "sub";
    case BinaryOp::kMul:
      return "mul";
    case BinaryOp::kDiv:
      return "div";
    case BinaryOp::kMod:
      return "mod";
    case BinaryOp::kLt:
      return "lt";
    case BinaryOp::kGt:
      return "gt";
    case BinaryOp::kLe:
      return "le";
    case BinaryOp::kGe:
      return "ge";
    case BinaryOp::kEq:
      return "eq";
    case BinaryOp::kNe:
      return "ne";
    case BinaryOp::kAnd:
    case BinaryOp::kOr:
      break;
  }
  throw SemanticError("短路逻辑运算不应作为普通二元运算生成");
}

std::string SysyFloatBinaryRuntime(BinaryOp op) {
  switch (op) {
    case BinaryOp::kAdd:
      return "@__sysy_fadd";
    case BinaryOp::kSub:
      return "@__sysy_fsub";
    case BinaryOp::kMul:
      return "@__sysy_fmul";
    case BinaryOp::kDiv:
      return "@__sysy_fdiv";
    default:
      break;
  }
  throw SemanticError("该二元运算不支持 float 普通算术");
}

std::string SysyFloatCompareRuntime(BinaryOp op) {
  switch (op) {
    case BinaryOp::kLt:
      return "@__sysy_flt";
    case BinaryOp::kGt:
      return "@__sysy_fgt";
    case BinaryOp::kLe:
      return "@__sysy_fle";
    case BinaryOp::kGe:
      return "@__sysy_fge";
    case BinaryOp::kEq:
      return "@__sysy_feq";
    case BinaryOp::kNe:
      return "@__sysy_fne";
    default:
      break;
  }
  throw SemanticError("该比较运算不支持 float");
}

std::string FunctionTypeSuffix(const Type& type) {
  if (type.IsVoid()) {
    return "";
  }
  return ": " + type.ToKoopaString();
}

void RequireIntScalar(const Type& type, const std::string& where) {
  if (!type.IsIntScalar()) {
    throw SemanticError(where + " 目前只支持 int 标量，收到 " +
                        type.ToDebugString());
  }
}

void RequireNumericScalar(const Type& type, const std::string& where) {
  if (!type.IsNumericScalar()) {
    throw SemanticError(where + " 需要 int 或 float 标量，收到 " +
                        type.ToDebugString());
  }
}

int Product(const std::vector<int>& dims) {
  int result = 1;
  for (int dim : dims) {
    if (dim <= 0) {
      throw SemanticError("数组维度必须是正整数");
    }
    result *= dim;
  }
  return result;
}

std::string ScalarKoopaType(BasicType basic) {
  if (basic == BasicType::kFloat) {
    return "i32";
  }
  if (basic == BasicType::kInt) {
    return "i32";
  }
  throw SemanticError("void 不能作为对象类型");
}

int32_t FloatToI32Bits(double value) {
  float narrowed = static_cast<float>(value);
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(narrowed),
                "float must be 32 bits for SysY float lowering");
  std::memcpy(&bits, &narrowed, sizeof(bits));
  return static_cast<int32_t>(bits);
}

std::string FloatLiteral(double value) {
  return std::to_string(FloatToI32Bits(value));
}

std::string ArrayKoopaType(BasicType basic, const std::vector<int>& dims,
                           size_t index) {
  if (index == dims.size()) {
    return ScalarKoopaType(basic);
  }
  return "[" + ArrayKoopaType(basic, dims, index + 1) + ", " +
         std::to_string(dims[index]) + "]";
}

std::vector<int> FlatIndexToIndices(int flat, const std::vector<int>& dims) {
  std::vector<int> indices(dims.size(), 0);
  for (int i = static_cast<int>(dims.size()) - 1; i >= 0; --i) {
    indices[i] = flat % dims[i];
    flat /= dims[i];
  }
  return indices;
}

std::string AggregateString(const std::vector<int>& values,
                            const std::vector<int>& dims, size_t dim,
                            size_t* offset) {
  if (dim == dims.size()) {
    return std::to_string(values[(*offset)++]);
  }
  std::ostringstream oss;
  oss << "{";
  for (int i = 0; i < dims[dim]; ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << AggregateString(values, dims, dim + 1, offset);
  }
  oss << "}";
  return oss.str();
}

std::string AggregateFloatString(const std::vector<double>& values,
                                 const std::vector<int>& dims, size_t dim,
                                 size_t* offset) {
  if (dim == dims.size()) {
    return FloatLiteral(values[(*offset)++]);
  }
  std::ostringstream oss;
  oss << "{";
  for (int i = 0; i < dims[dim]; ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << AggregateFloatString(values, dims, dim + 1, offset);
  }
  oss << "}";
  return oss.str();
}

Type RemainingArrayType(const Type& base_type, size_t used_indices) {
  Type result;
  result.basic = base_type.basic;
  if (used_indices >= base_type.array_dims.size()) {
    return result;
  }
  result.is_array = true;
  result.array_dims.assign(base_type.array_dims.begin() + used_indices,
                           base_type.array_dims.end());
  return result;
}

bool HasConstFloatOrInt(const ExprResult& value) {
  return value.const_value.has_value() || value.const_float_value.has_value();
}

double ConstAsDouble(const ExprResult& value) {
  if (value.const_float_value.has_value()) {
    return *value.const_float_value;
  }
  if (value.const_value.has_value()) {
    return static_cast<double>(*value.const_value);
  }
  throw SemanticError("表达式不是编译期常量");
}

ExprResult CastTo(IrContext& context, ExprResult value, const Type& target,
                  const std::string& usage) {
  value.RequireValue(usage);
  RequireNumericScalar(target, usage + " 目标类型");
  if (value.type.basic == target.basic && value.type.IsNumericScalar() &&
      target.IsNumericScalar()) {
    return value;
  }
  if (target.IsIntScalar()) {
    if (value.const_value.has_value()) {
      return value;
    }
    if (value.const_float_value.has_value()) {
      return ExprResult::ConstInt(static_cast<int>(*value.const_float_value));
    }
    std::string temp = context.NewTemp();
    context.EmitInstruction(temp + " = call @__sysy_ftoi(" +
                            value.AsOperand() + ")");
    return ExprResult::RuntimeValue(Type::Int(), temp);
  }
  if (target.IsFloatScalar()) {
    if (value.const_float_value.has_value()) {
      return value;
    }
    if (value.const_value.has_value()) {
      return ExprResult::ConstFloat(static_cast<double>(*value.const_value));
    }
    std::string temp = context.NewTemp();
    context.EmitInstruction(temp + " = call @__sysy_itof(" +
                            value.AsOperand() + ")");
    return ExprResult::RuntimeValue(Type::Float(), temp);
  }
  throw SemanticError(usage + " 不支持该类型转换");
}

ExprResult ConstCast(ExprResult value, const Type& target,
                     const std::string& usage) {
  value.RequireValue(usage);
  RequireNumericScalar(target, usage + " 目标类型");
  if (!HasConstFloatOrInt(value)) {
    throw SemanticError(usage + " 不是编译期常量");
  }
  if (target.IsIntScalar()) {
    if (value.const_value.has_value()) {
      return value;
    }
    return ExprResult::ConstInt(static_cast<int>(*value.const_float_value));
  }
  if (target.IsFloatScalar()) {
    if (value.const_float_value.has_value()) {
      return value;
    }
    return ExprResult::ConstFloat(static_cast<double>(*value.const_value));
  }
  throw SemanticError(usage + " 不支持该常量类型转换");
}

std::string ConstExprText(const ExprResult& value) {
  if (value.const_value.has_value()) {
    return std::to_string(*value.const_value);
  }
  if (value.const_float_value.has_value()) {
    return FloatLiteral(*value.const_float_value);
  }
  throw SemanticError("缺少编译期常量值");
}

ExprResult EmitRuntimeCall(IrContext& context, const std::string& callee,
                           const Type& return_type,
                           const std::vector<ExprResult>& args) {
  std::ostringstream call;
  call << "call " << callee << "(";
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      call << ", ";
    }
    call << args[i].AsOperand();
  }
  call << ")";
  std::string temp = context.NewTemp();
  context.EmitInstruction(temp + " = " + call.str());
  return ExprResult::RuntimeValue(return_type, temp);
}

ExprResult EmitNumericBinary(IrContext& context, BinaryOp op, ExprResult lhs,
                             ExprResult rhs) {
  lhs.RequireValue("二元表达式左操作数");
  rhs.RequireValue("二元表达式右操作数");
  if (op == BinaryOp::kMod) {
    if (!lhs.type.IsIntScalar() || !rhs.type.IsIntScalar()) {
      throw SemanticError("% 运算只支持 int 操作数");
    }
    if (lhs.const_value.has_value() && rhs.const_value.has_value()) {
      return ExprResult::ConstInt(
          CalcConstBinary(op, *lhs.const_value, *rhs.const_value));
    }
    return context.EmitBinaryRuntime("mod", lhs, rhs);
  }

  const bool is_compare = op == BinaryOp::kLt || op == BinaryOp::kGt ||
                          op == BinaryOp::kLe || op == BinaryOp::kGe ||
                          op == BinaryOp::kEq || op == BinaryOp::kNe;
  const bool use_float = lhs.type.IsFloatScalar() || rhs.type.IsFloatScalar();
  Type operand_type = use_float ? Type::Float() : Type::Int();
  lhs = CastTo(context, std::move(lhs), operand_type, "二元表达式左操作数转换");
  rhs = CastTo(context, std::move(rhs), operand_type, "二元表达式右操作数转换");

  if (HasConstFloatOrInt(lhs) && HasConstFloatOrInt(rhs)) {
    if (!use_float) {
      return ExprResult::ConstInt(
          CalcConstBinary(op, *lhs.const_value, *rhs.const_value));
    }
    double l = ConstAsDouble(lhs);
    double r = ConstAsDouble(rhs);
    if (is_compare) {
      switch (op) {
        case BinaryOp::kLt:
          return ExprResult::ConstInt(l < r);
        case BinaryOp::kGt:
          return ExprResult::ConstInt(l > r);
        case BinaryOp::kLe:
          return ExprResult::ConstInt(l <= r);
        case BinaryOp::kGe:
          return ExprResult::ConstInt(l >= r);
        case BinaryOp::kEq:
          return ExprResult::ConstInt(l == r);
        case BinaryOp::kNe:
          return ExprResult::ConstInt(l != r);
        default:
          break;
      }
    }
    switch (op) {
      case BinaryOp::kAdd:
        return ExprResult::ConstFloat(l + r);
      case BinaryOp::kSub:
        return ExprResult::ConstFloat(l - r);
      case BinaryOp::kMul:
        return ExprResult::ConstFloat(l * r);
      case BinaryOp::kDiv:
        return ExprResult::ConstFloat(l / r);
      default:
        break;
    }
  }

  if (use_float) {
    return EmitRuntimeCall(
        context,
        is_compare ? SysyFloatCompareRuntime(op) : SysyFloatBinaryRuntime(op),
        is_compare ? Type::Int() : Type::Float(), {lhs, rhs});
  }
  return context.EmitBinaryRuntime(KoopaBinaryOp(op), lhs, rhs);
}

}  // namespace

SemanticError::SemanticError(const std::string& message)
    : std::runtime_error("SemanticError: " + message) {}

Type Type::Void() {
  Type type;
  type.basic = BasicType::kVoid;
  return type;
}

Type Type::Int() {
  Type type;
  type.basic = BasicType::kInt;
  return type;
}

Type Type::Float() {
  Type type;
  type.basic = BasicType::kFloat;
  return type;
}

Type Type::IntArray(std::vector<int> dims) {
  Type type;
  type.basic = BasicType::kInt;
  type.is_array = true;
  type.array_dims = std::move(dims);
  Product(type.array_dims);
  return type;
}

Type Type::FloatArray(std::vector<int> dims) {
  Type type;
  type.basic = BasicType::kFloat;
  type.is_array = true;
  type.array_dims = std::move(dims);
  Product(type.array_dims);
  return type;
}

Type Type::IntArrayParameter(std::vector<int> dims) {
  Type type;
  type.basic = BasicType::kInt;
  type.is_array_parameter = true;
  type.array_dims = std::move(dims);
  Product(type.array_dims);
  return type;
}

Type Type::FloatArrayParameter(std::vector<int> dims) {
  Type type;
  type.basic = BasicType::kFloat;
  type.is_array_parameter = true;
  type.array_dims = std::move(dims);
  Product(type.array_dims);
  return type;
}

bool Type::IsVoid() const {
  return basic == BasicType::kVoid && !is_array && !is_array_parameter;
}

bool Type::IsIntScalar() const {
  return basic == BasicType::kInt && !is_array && !is_array_parameter;
}

bool Type::IsFloatScalar() const {
  return basic == BasicType::kFloat && !is_array && !is_array_parameter;
}

bool Type::IsNumericScalar() const {
  return IsIntScalar() || IsFloatScalar();
}

bool Type::IsArray() const { return is_array; }

bool Type::IsArrayParameter() const {
  return is_array_parameter;
}

bool Type::IsFloat() const { return basic == BasicType::kFloat; }

int Type::ElementCount() const { return Product(array_dims); }

std::string Type::ToKoopaString() const {
  return ToKoopaStorageString();
}

std::string Type::ToKoopaStorageString() const {
  if (basic == BasicType::kVoid) {
    return "";
  }
  if (is_array_parameter) {
    return "*" + ArrayKoopaType(basic, array_dims, 0);
  }
  if (is_array) {
    return ArrayKoopaType(basic, array_dims, 0);
  }
  return ScalarKoopaType(basic);
}

std::string Type::ToDebugString() const {
  std::string result;
  switch (basic) {
    case BasicType::kVoid:
      result = "void";
      break;
    case BasicType::kInt:
      result = "int";
      break;
    case BasicType::kFloat:
      result = "float";
      break;
  }
  if (is_array) {
    result += " array";
  }
  if (is_array_parameter) {
    result += " array parameter";
  }
  return result;
}

ExprResult ExprResult::ConstInt(int value) {
  ExprResult result;
  result.type = Type::Int();
  result.value_name = std::to_string(value);
  result.const_value = value;
  return result;
}

ExprResult ExprResult::ConstFloat(double value) {
  ExprResult result;
  result.type = Type::Float();
  result.value_name = FloatLiteral(value);
  result.const_float_value = value;
  return result;
}

ExprResult ExprResult::RuntimeValue(Type type, std::string value_name) {
  ExprResult result;
  result.type = std::move(type);
  result.value_name = std::move(value_name);
  return result;
}

ExprResult ExprResult::Void() {
  ExprResult result;
  result.type = Type::Void();
  result.is_void = true;
  return result;
}

std::string ExprResult::AsOperand() const {
  RequireValue("表达式取值");
  if (const_value.has_value()) {
    return std::to_string(*const_value);
  }
  if (const_float_value.has_value()) {
    return FloatLiteral(*const_float_value);
  }
  return value_name;
}

std::string ExprResult::AsCallOperand() const {
  if (type.IsArrayParameter()) {
    return value_name;
  }
  return AsOperand();
}

void ExprResult::RequireValue(const std::string& usage) const {
  if (is_void || type.IsVoid()) {
    throw SemanticError(usage + " 需要普通值，但得到了 void 表达式");
  }
  if (type.IsArray() || type.IsArrayParameter()) {
    throw SemanticError(usage + " 需要标量，但得到了数组");
  }
  RequireNumericScalar(type, usage);
}

bool ExprResult::IsConstInt() const {
  return type.IsIntScalar() && const_value.has_value();
}

SymbolTable::SymbolTable() { PushScope(); }

void SymbolTable::PushScope() { scopes_.push_back({}); }

void SymbolTable::PopScope() {
  if (scopes_.size() <= 1) {
    throw SemanticError("试图弹出全局作用域");
  }
  scopes_.pop_back();
}

bool SymbolTable::IsGlobalScope() const { return scopes_.size() == 1; }

void SymbolTable::Define(const Symbol& symbol) {
  assert(!scopes_.empty());
  auto& scope = scopes_.back();
  if (scope.find(symbol.source_name) != scope.end()) {
    throw SemanticError("同一作用域重复定义: " + symbol.source_name);
  }
  scope.emplace(symbol.source_name, symbol);
}

const Symbol* SymbolTable::Lookup(const std::string& name) const {
  for (auto iter = scopes_.rbegin(); iter != scopes_.rend(); ++iter) {
    auto found = iter->find(name);
    if (found != iter->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

const Symbol& SymbolTable::Require(const std::string& name) const {
  const Symbol* symbol = Lookup(name);
  if (symbol == nullptr) {
    throw SemanticError("未定义的标识符: " + name);
  }
  return *symbol;
}

IrContext::IrContext() = default;

SymbolTable& IrContext::symbols() { return symbols_; }

const SymbolTable& IrContext::symbols() const { return symbols_; }

void IrContext::EmitBuiltins() {
  if (builtins_emitted_) {
    return;
  }
  builtins_emitted_ = true;
  EmitGlobalLine("decl @getint(): i32");
  EmitGlobalLine("decl @getch(): i32");
  EmitGlobalLine("decl @putint(i32)");
  EmitGlobalLine("decl @putch(i32)");
  EmitGlobalLine("decl @__sysy_fadd(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fsub(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fmul(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fdiv(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fneg(i32): i32");
  EmitGlobalLine("decl @__sysy_itof(i32): i32");
  EmitGlobalLine("decl @__sysy_ftoi(i32): i32");
  EmitGlobalLine("decl @__sysy_feq(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fne(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_flt(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fle(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fgt(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fge(i32, i32): i32");
  EmitGlobalLine("decl @__sysy_fiszero(i32): i32");
  EmitGlobalLine("decl @__sysy_fisnonzero(i32): i32");
  EmitGlobalLine("");

  auto define_builtin = [this](const std::string& name, Type return_type,
                               std::vector<Type> param_types) {
    Symbol symbol;
    symbol.kind = SymbolKind::kFunction;
    symbol.source_name = name;
    symbol.ir_name = "@" + name;
    symbol.type = std::move(return_type);
    symbol.param_types = std::move(param_types);
    symbols_.Define(symbol);
  };
  define_builtin("getint", Type::Int(), {});
  define_builtin("getch", Type::Int(), {});
  define_builtin("putint", Type::Void(), {Type::Int()});
  define_builtin("putch", Type::Void(), {Type::Int()});
}

void IrContext::EmitGlobalLine(const std::string& line) {
  ir_ += line;
  ir_ += "\n";
}

void IrContext::EmitFunctionLine(const std::string& line) {
  ir_ += line;
  ir_ += "\n";
}

void IrContext::EmitInstruction(const std::string& line) {
  EnsureBlockOpen("生成普通指令");
  ir_ += Indent(line);
}

void IrContext::EmitTerminator(const std::string& line) {
  EnsureBlockOpen("生成基本块终结指令");
  ir_ += Indent(line);
  MarkBlockTerminated();
}

void IrContext::EmitLabel(const std::string& label) {
  ir_ += label + ":\n";
  MarkBlockOpen();
}

void IrContext::EnsureBlockOpen(const std::string& reason) const {
  if (block_terminated_) {
    // Koopa IR 的一个基本块只能以 ret/jump/br 作为最后一条指令。
    // 如果终结后继续输出普通指令，后端会看到不可达但仍挂在同一块里的错误 IR。
    throw SemanticError(reason + " 失败：当前基本块已经终结");
  }
}

std::string IrContext::NewTemp() { return "%" + std::to_string(temp_id_++); }

std::string IrContext::NewLabel(const std::string& hint) {
  return "%" + MangleLocalHint(hint) + "_" + std::to_string(label_id_++);
}

std::string IrContext::NewLocalName(const std::string& source_name) {
  return "%" + MangleLocalHint(source_name) + "_" + std::to_string(local_id_++);
}

std::string IrContext::NewGlobalName(const std::string& source_name) {
  (void)global_id_;
  return "@" + MangleLocalHint(source_name);
}

bool IrContext::IsInGlobalScope() const { return symbols_.IsGlobalScope(); }

bool IrContext::IsBlockTerminated() const { return block_terminated_; }

void IrContext::MarkBlockTerminated() { block_terminated_ = true; }

void IrContext::MarkBlockOpen() { block_terminated_ = false; }

void IrContext::BeginFunction(
    const std::string& ir_name, const Type& return_type,
    const std::vector<std::pair<std::string, Type>>& params) {
  CheckSupportedType(return_type, "函数返回类型");
  current_function_ir_name_ = ir_name;
  current_function_return_type_ = return_type;
  temp_id_ = 0;
  local_id_ = 0;
  block_terminated_ = false;

  std::ostringstream line;
  line << "fun " << ir_name << "(";
  for (size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      line << ", ";
    }
    CheckSupportedType(params[i].second, "函数形参类型");
    line << "@" << MangleLocalHint(params[i].first) << ": "
         << params[i].second.ToKoopaString();
  }
  line << ")" << FunctionTypeSuffix(return_type) << " {";
  EmitFunctionLine(line.str());
  EmitLabel("%entry");

  // 函数体需要独立作用域栈帧。作用域栈能让局部变量遮蔽外层名字，
  // 也能在同一层及时检查重复定义，后续接入 Bison 时语义动作会更简单。
  symbols_.PushScope();
}

void IrContext::EndFunction() {
  if (!block_terminated_) {
    if (current_function_return_type_.IsVoid()) {
      EmitTerminator("ret");
    } else if (current_function_return_type_.IsFloatScalar()) {
      EmitTerminator("ret 0");
    } else {
      // 课程项目早期常用兜底返回，避免后续后端处理未终结函数块。
      EmitTerminator("ret 0");
    }
  }
  symbols_.PopScope();
  EmitFunctionLine("}");
  EmitFunctionLine("");
  current_function_ir_name_.clear();
  block_terminated_ = false;
}

const Type& IrContext::CurrentFunctionReturnType() const {
  return current_function_return_type_;
}

void IrContext::PushLoop(const std::string& break_label,
                         const std::string& continue_label) {
  loop_stack_.push_back({break_label, continue_label});
}

void IrContext::PopLoop() {
  if (loop_stack_.empty()) {
    throw SemanticError("循环 label 栈为空，无法弹出");
  }
  loop_stack_.pop_back();
}

const LoopLabels& IrContext::CurrentLoop() const {
  if (loop_stack_.empty()) {
    throw SemanticError("break / continue 出现在循环外");
  }
  return loop_stack_.back();
}

ExprResult IrContext::EmitToBool(const ExprResult& expr) {
  expr.RequireValue("条件表达式");
  if (expr.const_value.has_value()) {
    return ExprResult::ConstInt(*expr.const_value != 0);
  }
  if (expr.const_float_value.has_value()) {
    return ExprResult::ConstInt(*expr.const_float_value != 0.0);
  }
  std::string temp = NewTemp();
  if (expr.type.IsFloatScalar()) {
    EmitInstruction(temp + " = call @__sysy_fisnonzero(" + expr.AsOperand() +
                    ")");
  } else {
    EmitInstruction(temp + " = ne " + expr.AsOperand() + ", 0");
  }
  return ExprResult::RuntimeValue(Type::Int(), temp);
}

ExprResult IrContext::EmitBinaryRuntime(const std::string& op,
                                        const ExprResult& lhs,
                                        const ExprResult& rhs) {
  lhs.RequireValue("二元表达式左操作数");
  rhs.RequireValue("二元表达式右操作数");
  std::string temp = NewTemp();
  EmitInstruction(temp + " = " + op + " " + lhs.AsOperand() + ", " +
                  rhs.AsOperand());
  return ExprResult::RuntimeValue(Type::Int(), temp);
}

ExprResult IrContext::EmitCall(const std::string& function_ir_name,
                               const Type& return_type,
                               const std::vector<ExprResult>& args) {
  std::ostringstream call;
  call << "call " << function_ir_name << "(";
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      call << ", ";
    }
    call << args[i].AsCallOperand();
  }
  call << ")";
  if (return_type.IsVoid()) {
    EmitInstruction(call.str());
    return ExprResult::Void();
  }
  std::string temp = NewTemp();
  EmitInstruction(temp + " = " + call.str());
  return ExprResult::RuntimeValue(return_type, temp);
}

std::string IrContext::GetIR() const { return ir_; }

void IrContext::CheckSupportedType(const Type& type,
                                   const std::string& where) const {
  (void)type;
  (void)where;
}

void ExprAST::GenerateKoopaIR(IrContext& context) const {
  GenerateExprIR(context);
}

ExprResult ExprAST::GenerateArgumentIR(IrContext& context,
                                       const Type& expected_type) const {
  ExprResult result = GenerateExprIR(context);
  if (expected_type.IsArrayParameter()) {
    throw SemanticError("函数实参需要数组地址");
  }
  result.RequireValue("函数调用实参");
  return result;
}

NumberAST::NumberAST(int value) : value_(value) {}

ExprResult NumberAST::GenerateExprIR(IrContext& context) const {
  (void)context;
  return ExprResult::ConstInt(value_);
}

FloatNumberAST::FloatNumberAST(double value) : value_(value) {}

ExprResult FloatNumberAST::GenerateExprIR(IrContext& context) const {
  (void)context;
  return ExprResult::ConstFloat(value_);
}

LValAST::LValAST(std::string name) : name_(std::move(name)) {}

LValAST::LValAST(std::string name, std::vector<std::unique_ptr<ExprAST>> indices)
    : name_(std::move(name)), indices_(std::move(indices)) {}

const std::string& LValAST::name() const { return name_; }

ExprResult LValAST::GenerateExprIR(IrContext& context) const {
  const Symbol& symbol = context.symbols().Require(name_);
  if (symbol.kind == SymbolKind::kFunction) {
    throw SemanticError("函数名被当作变量使用: " + name_);
  }
  if (symbol.type.IsArray() || symbol.type.IsArrayParameter()) {
    std::optional<int> const_value = TryConstArrayElement(symbol, context);
    if (const_value.has_value()) {
      return ExprResult::ConstInt(*const_value);
    }
    ExprResult addr = GenerateAddressIR(context, true);
    if (!addr.type.IsNumericScalar()) {
      throw SemanticError("数组名不能直接作为标量使用: " + name_);
    }
    std::string temp = context.NewTemp();
    context.EmitInstruction(temp + " = load " + addr.value_name);
    return ExprResult::RuntimeValue(addr.type, temp);
  }
  if (symbol.kind == SymbolKind::kConstant) {
    // const int 是编译期常量：记录在符号表里即可，使用时直接替换成字面量。
    // 这样不会为常量生成多余 alloc/load，也自然支持 const int b = a + 2。
    if (symbol.const_value.has_value()) {
      return ExprResult::ConstInt(*symbol.const_value);
    }
    if (symbol.const_float_value.has_value()) {
      return ExprResult::ConstFloat(*symbol.const_float_value);
    }
    throw SemanticError("常量缺少编译期值: " + name_);
  }
  if (context.IsInGlobalScope()) {
    throw SemanticError("全局初始化中引用了非常量变量: " + name_);
  }
  RequireNumericScalar(symbol.type, "变量读取");
  std::string temp = context.NewTemp();
  context.EmitInstruction(temp + " = load " + symbol.ir_name);
  return ExprResult::RuntimeValue(symbol.type, temp);
}

ExprResult LValAST::GenerateArgumentIR(IrContext& context,
                                       const Type& expected_type) const {
  if (!expected_type.IsArrayParameter()) {
    return GenerateExprIR(context);
  }
  ExprResult addr = GenerateAddressIR(context, true);
  Type addr_type = addr.type;
  if (addr_type.basic != expected_type.basic) {
    throw SemanticError("数组实参与形参基础类型不匹配: " + name_);
  }
  if (addr_type.IsArray()) {
    if (addr_type.array_dims.size() != expected_type.array_dims.size() + 1) {
      throw SemanticError("数组实参与形参数组维度不匹配: " + name_);
    }
    for (size_t i = 0; i < expected_type.array_dims.size(); ++i) {
      if (addr_type.array_dims[i + 1] != expected_type.array_dims[i]) {
        throw SemanticError("数组实参与形参数组维度不匹配: " + name_);
      }
    }
    std::string temp = context.NewTemp();
    context.EmitInstruction(temp + " = getelemptr " + addr.value_name + ", 0");
    return ExprResult::RuntimeValue(expected_type, temp);
  }
  if (addr_type.IsArrayParameter()) {
    if (addr_type.array_dims != expected_type.array_dims) {
      throw SemanticError("数组实参与形参数组维度不匹配: " + name_);
    }
    return ExprResult::RuntimeValue(expected_type, addr.value_name);
  }
  throw SemanticError("函数实参需要数组: " + name_);
}

ExprResult LValAST::GenerateAddressIR(IrContext& context) const {
  return GenerateAddressIR(context, false);
}

ExprResult LValAST::GenerateAddressIR(IrContext& context,
                                      bool allow_array_result) const {
  const Symbol& symbol = context.symbols().Require(name_);
  if (symbol.kind == SymbolKind::kFunction) {
    throw SemanticError("函数名不能作为赋值左值: " + name_);
  }
  if (symbol.kind == SymbolKind::kConstant && !symbol.type.IsArray() &&
      !symbol.type.IsArrayParameter()) {
    throw SemanticError("不能给 const 赋值: " + name_);
  }
  if (symbol.kind == SymbolKind::kConstant && symbol.type.IsArray() &&
      !allow_array_result) {
    throw SemanticError("不能给 const 数组元素赋值: " + name_);
  }
  if (symbol.type.IsNumericScalar()) {
    if (!indices_.empty()) {
      throw SemanticError("标量变量不能使用数组下标: " + name_);
    }
    return ExprResult::RuntimeValue(symbol.type, symbol.ir_name);
  }

  if (indices_.size() > symbol.type.array_dims.size() +
                            (symbol.type.IsArrayParameter() ? 1 : 0)) {
    throw SemanticError("数组下标数量超过维度: " + name_);
  }

  std::string current_addr = symbol.ir_name;
  Type current_type = symbol.type;
  size_t used_indices = 0;
  if (symbol.type.IsArrayParameter()) {
    current_addr = context.NewTemp();
    context.EmitInstruction(current_addr + " = load " + symbol.ir_name);
    current_type = symbol.type.basic == BasicType::kFloat
                       ? Type::FloatArray(symbol.type.array_dims)
                       : Type::IntArray(symbol.type.array_dims);
    current_type.is_array_parameter = true;
    current_type.is_array = false;
  }

  for (const auto& index_expr : indices_) {
    ExprResult index = index_expr->GenerateExprIR(context);
    index.RequireValue("数组下标");
    if (!index.type.IsIntScalar()) {
      throw SemanticError("数组下标必须是 int");
    }
    std::string temp = context.NewTemp();
    if (symbol.type.IsArrayParameter() && used_indices == 0) {
      // 数组形参本质是指向首元素或首个子数组的指针，第一维寻址使用 getptr。
      context.EmitInstruction(temp + " = getptr " + current_addr + ", " +
                              index.AsOperand());
    } else {
      // 局部/全局数组的 alloc 结果是数组对象地址，逐维使用 getelemptr。
      context.EmitInstruction(temp + " = getelemptr " + current_addr + ", " +
                              index.AsOperand());
    }
    current_addr = temp;
    ++used_indices;
  }

  Type result_type;
  if (symbol.type.IsArrayParameter()) {
    if (used_indices == 0) {
      result_type = symbol.type.basic == BasicType::kFloat
                        ? Type::FloatArrayParameter(symbol.type.array_dims)
                        : Type::IntArrayParameter(symbol.type.array_dims);
    } else {
      result_type = RemainingArrayType(symbol.type, used_indices - 1);
    }
  } else {
    result_type = RemainingArrayType(symbol.type, used_indices);
  }
  if (result_type.IsArray() && !allow_array_result) {
    return ExprResult::RuntimeValue(result_type, current_addr);
  }
  if (result_type.IsArray()) {
    if (!symbol.type.IsArrayParameter()) {
      return ExprResult::RuntimeValue(result_type, current_addr);
    }
    Type ptr_type = result_type.basic == BasicType::kFloat
                        ? Type::FloatArrayParameter(result_type.array_dims)
                        : Type::IntArrayParameter(result_type.array_dims);
    return ExprResult::RuntimeValue(ptr_type, current_addr);
  }
  return ExprResult::RuntimeValue(
      symbol.type.basic == BasicType::kFloat ? Type::Float() : Type::Int(),
      current_addr);
}

std::optional<int> LValAST::TryConstArrayElement(const Symbol& symbol,
                                                IrContext& context) const {
  if (symbol.kind != SymbolKind::kConstant || !symbol.type.IsArray() ||
      symbol.const_array_values.empty() ||
      indices_.size() != symbol.type.array_dims.size()) {
    return std::nullopt;
  }
  int flat = 0;
  for (size_t i = 0; i < indices_.size(); ++i) {
    ExprResult index = indices_[i]->GenerateExprIR(context);
    if (!index.const_value.has_value()) {
      return std::nullopt;
    }
    int idx = *index.const_value;
    if (idx < 0 || idx >= symbol.type.array_dims[i]) {
      throw SemanticError("数组常量下标越界: " + name_);
    }
    flat = flat * symbol.type.array_dims[i] + idx;
  }
  return symbol.const_array_values[flat];
}

UnaryExpAST::UnaryExpAST(UnaryOp op, std::unique_ptr<ExprAST> operand)
    : op_(op), operand_(std::move(operand)) {}

ExprResult UnaryExpAST::GenerateExprIR(IrContext& context) const {
  ExprResult operand = operand_->GenerateExprIR(context);
  operand.RequireValue("一元表达式操作数");
  if (operand.const_value.has_value()) {
    int value = *operand.const_value;
    switch (op_) {
      case UnaryOp::kPlus:
        return ExprResult::ConstInt(value);
      case UnaryOp::kMinus:
        return ExprResult::ConstInt(-value);
      case UnaryOp::kNot:
        return ExprResult::ConstInt(value == 0);
    }
  }
  if (operand.const_float_value.has_value()) {
    double value = *operand.const_float_value;
    switch (op_) {
      case UnaryOp::kPlus:
        return ExprResult::ConstFloat(value);
      case UnaryOp::kMinus:
        return ExprResult::ConstFloat(-value);
      case UnaryOp::kNot:
        return ExprResult::ConstInt(value == 0.0);
    }
  }
  switch (op_) {
    case UnaryOp::kPlus:
      return operand;
    case UnaryOp::kMinus:
      if (operand.type.IsFloatScalar()) {
        std::string temp = context.NewTemp();
        context.EmitInstruction(temp + " = call @__sysy_fneg(" +
                                operand.AsOperand() + ")");
        return ExprResult::RuntimeValue(Type::Float(), temp);
      }
      return context.EmitBinaryRuntime("sub", ExprResult::ConstInt(0),
                                       operand);
    case UnaryOp::kNot:
      if (operand.type.IsFloatScalar()) {
        std::string temp = context.NewTemp();
        context.EmitInstruction(temp + " = call @__sysy_fiszero(" +
                                operand.AsOperand() + ")");
        return ExprResult::RuntimeValue(Type::Int(), temp);
      }
      return context.EmitBinaryRuntime("eq", operand, ExprResult::ConstInt(0));
  }
  throw SemanticError("未知一元运算符");
}

BinaryExpAST::BinaryExpAST(BinaryOp op, std::unique_ptr<ExprAST> lhs,
                           std::unique_ptr<ExprAST> rhs)
    : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

ExprResult BinaryExpAST::GenerateExprIR(IrContext& context) const {
  if (op_ == BinaryOp::kAnd) {
    return GenerateLogicalAndIR(context);
  }
  if (op_ == BinaryOp::kOr) {
    return GenerateLogicalOrIR(context);
  }

  ExprResult lhs = lhs_->GenerateExprIR(context);
  ExprResult rhs = rhs_->GenerateExprIR(context);
  return EmitNumericBinary(context, op_, std::move(lhs), std::move(rhs));
}

ExprResult BinaryExpAST::GenerateLogicalAndIR(IrContext& context) const {
  ExprResult lhs = lhs_->GenerateExprIR(context);
  lhs.RequireValue("&& 左操作数");
  if (lhs.const_float_value.has_value() && *lhs.const_float_value == 0.0) {
    return ExprResult::ConstInt(0);
  }
  if (lhs.const_float_value.has_value()) {
    ExprResult rhs = rhs_->GenerateExprIR(context);
    return context.EmitToBool(rhs);
  }
  if (lhs.const_value.has_value() && *lhs.const_value == 0) {
    return ExprResult::ConstInt(0);
  }
  if (lhs.const_value.has_value()) {
    ExprResult rhs = rhs_->GenerateExprIR(context);
    return context.EmitToBool(rhs);
  }

  // && 必须短路：左边为假时右边不能求值。因此这里用控制流和一个栈槽
  // 保存结果，而不是直接生成普通的 and 二元运算。
  std::string result_addr = context.NewTemp();
  context.EmitInstruction(result_addr + " = alloc i32");
  context.EmitInstruction("store 0, " + result_addr);
  ExprResult lhs_bool = context.EmitToBool(lhs);

  std::string rhs_label = context.NewLabel("land_rhs");
  std::string end_label = context.NewLabel("land_end");
  context.EmitTerminator("br " + lhs_bool.AsOperand() + ", " + rhs_label +
                         ", " + end_label);

  context.EmitLabel(rhs_label);
  ExprResult rhs = rhs_->GenerateExprIR(context);
  ExprResult rhs_bool = context.EmitToBool(rhs);
  context.EmitInstruction("store " + rhs_bool.AsOperand() + ", " +
                          result_addr);
  context.EmitTerminator("jump " + end_label);

  context.EmitLabel(end_label);
  std::string temp = context.NewTemp();
  context.EmitInstruction(temp + " = load " + result_addr);
  return ExprResult::RuntimeValue(Type::Int(), temp);
}

ExprResult BinaryExpAST::GenerateLogicalOrIR(IrContext& context) const {
  ExprResult lhs = lhs_->GenerateExprIR(context);
  lhs.RequireValue("|| 左操作数");
  if (lhs.const_float_value.has_value() && *lhs.const_float_value != 0.0) {
    return ExprResult::ConstInt(1);
  }
  if (lhs.const_float_value.has_value()) {
    ExprResult rhs = rhs_->GenerateExprIR(context);
    return context.EmitToBool(rhs);
  }
  if (lhs.const_value.has_value() && *lhs.const_value != 0) {
    return ExprResult::ConstInt(1);
  }
  if (lhs.const_value.has_value()) {
    ExprResult rhs = rhs_->GenerateExprIR(context);
    return context.EmitToBool(rhs);
  }

  // || 也必须短路：左边为真时右边不能求值。结果槽先写 1，
  // 只有左边为假才进入右侧表达式并覆盖成右侧布尔值。
  std::string result_addr = context.NewTemp();
  context.EmitInstruction(result_addr + " = alloc i32");
  context.EmitInstruction("store 1, " + result_addr);
  ExprResult lhs_bool = context.EmitToBool(lhs);

  std::string rhs_label = context.NewLabel("lor_rhs");
  std::string end_label = context.NewLabel("lor_end");
  context.EmitTerminator("br " + lhs_bool.AsOperand() + ", " + end_label +
                         ", " + rhs_label);

  context.EmitLabel(rhs_label);
  ExprResult rhs = rhs_->GenerateExprIR(context);
  ExprResult rhs_bool = context.EmitToBool(rhs);
  context.EmitInstruction("store " + rhs_bool.AsOperand() + ", " +
                          result_addr);
  context.EmitTerminator("jump " + end_label);

  context.EmitLabel(end_label);
  std::string temp = context.NewTemp();
  context.EmitInstruction(temp + " = load " + result_addr);
  return ExprResult::RuntimeValue(Type::Int(), temp);
}

FuncCallAST::FuncCallAST(std::string name,
                         std::vector<std::unique_ptr<ExprAST>> args)
    : name_(std::move(name)), args_(std::move(args)) {}

ExprResult FuncCallAST::GenerateExprIR(IrContext& context) const {
  if (context.IsInGlobalScope()) {
    throw SemanticError("全局初始化中不能调用函数: " + name_);
  }
  const Symbol& symbol = context.symbols().Require(name_);
  if (symbol.kind != SymbolKind::kFunction) {
    throw SemanticError("变量名被当作函数调用: " + name_);
  }
  if (symbol.param_types.size() != args_.size()) {
    throw SemanticError("函数调用参数数量不匹配: " + name_);
  }
  std::vector<ExprResult> args;
  args.reserve(args_.size());
  for (size_t i = 0; i < args_.size(); ++i) {
    ExprResult arg = args_[i]->GenerateArgumentIR(context, symbol.param_types[i]);
    if (symbol.param_types[i].IsArrayParameter()) {
      if (!arg.type.IsArrayParameter()) {
        throw SemanticError("函数调用实参类型不匹配: " + name_);
      }
      if (arg.type.basic != symbol.param_types[i].basic) {
        throw SemanticError("数组实参与形参基础类型不匹配: " + name_);
      }
    } else {
      RequireNumericScalar(symbol.param_types[i], "函数形参类型");
      arg = CastTo(context, std::move(arg), symbol.param_types[i],
                   "函数调用实参");
    }
    args.push_back(std::move(arg));
  }
  return context.EmitCall(symbol.ir_name, symbol.type, args);
}

FuncFParamAST::FuncFParamAST(Type type, std::string name)
    : type_(std::move(type)), name_(std::move(name)) {}

void FuncFParamAST::GenerateKoopaIR(IrContext& context) const {
  (void)context;
}

const Type& FuncFParamAST::type() const { return type_; }

const std::string& FuncFParamAST::name() const { return name_; }

InitValAST::InitValAST(std::unique_ptr<ExprAST> expr)
    : expr_(std::move(expr)) {}

InitValAST::InitValAST(std::vector<std::unique_ptr<InitValAST>> elements)
    : elements_(std::move(elements)) {}

std::unique_ptr<InitValAST> InitValAST::MakeScalar(
    std::unique_ptr<ExprAST> expr) {
  return std::unique_ptr<InitValAST>(new InitValAST(std::move(expr)));
}

std::unique_ptr<InitValAST> InitValAST::MakeAggregate(
    std::vector<std::unique_ptr<InitValAST>> elements) {
  return std::unique_ptr<InitValAST>(new InitValAST(std::move(elements)));
}

bool InitValAST::IsScalar() const { return expr_ != nullptr; }

ExprResult InitValAST::GenerateScalarIR(IrContext& context) const {
  if (!IsScalar()) {
    throw SemanticError("标量初始化不能使用聚合初始化");
  }
  return expr_->GenerateExprIR(context);
}

std::vector<int> InitValAST::FlattenConst(IrContext& context,
                                          int capacity) const {
  std::vector<int> values;
  FlattenConstInto(context, &values);
  if (static_cast<int>(values.size()) > capacity) {
    throw SemanticError("数组初始化元素数量超过数组容量");
  }
  values.resize(capacity, 0);
  return values;
}

std::vector<double> InitValAST::FlattenConstFloat(IrContext& context,
                                                  int capacity) const {
  std::vector<double> values;
  FlattenConstFloatInto(context, &values);
  if (static_cast<int>(values.size()) > capacity) {
    throw SemanticError("数组初始化元素数量超过数组容量");
  }
  values.resize(capacity, 0.0);
  return values;
}

void InitValAST::FlattenConstInto(IrContext& context,
                                  std::vector<int>* values) const {
  if (IsScalar()) {
    ExprResult result = expr_->GenerateExprIR(context);
    result.RequireValue("数组初始化表达式");
    if (!HasConstFloatOrInt(result)) {
      throw SemanticError("当前仅支持数组常量表达式初始化");
    }
    values->push_back(ConstCast(std::move(result), Type::Int(),
                                "int 数组初始化表达式")
                          .const_value.value());
    return;
  }
  for (const auto& element : elements_) {
    element->FlattenConstInto(context, values);
  }
}

void InitValAST::FlattenConstFloatInto(IrContext& context,
                                       std::vector<double>* values) const {
  if (IsScalar()) {
    ExprResult result = expr_->GenerateExprIR(context);
    result.RequireValue("数组初始化表达式");
    if (!HasConstFloatOrInt(result)) {
      throw SemanticError("当前仅支持数组常量表达式初始化");
    }
    values->push_back(ConstCast(std::move(result), Type::Float(),
                                "float 数组初始化表达式")
                          .const_float_value.value());
    return;
  }
  for (const auto& element : elements_) {
    element->FlattenConstFloatInto(context, values);
  }
}

VarDefAST::VarDefAST(Type type, std::string name, std::unique_ptr<InitValAST> init)
    : type_(std::move(type)), name_(std::move(name)), init_(std::move(init)) {}

std::unique_ptr<VarDefAST> VarDefAST::MakeInt(std::string name) {
  return std::unique_ptr<VarDefAST>(
      new VarDefAST(Type::Int(), std::move(name), nullptr));
}

std::unique_ptr<VarDefAST> VarDefAST::MakeIntInit(
    std::string name, std::unique_ptr<ExprAST> init) {
  return std::unique_ptr<VarDefAST>(
      new VarDefAST(Type::Int(), std::move(name),
                    InitValAST::MakeScalar(std::move(init))));
}

std::unique_ptr<VarDefAST> VarDefAST::MakeArray(
    std::string name, std::vector<int> dims, std::unique_ptr<InitValAST> init) {
  return std::unique_ptr<VarDefAST>(
      new VarDefAST(Type::IntArray(std::move(dims)), std::move(name),
                    std::move(init)));
}

void VarDefAST::ApplyBaseType(BasicType basic) {
  if (type_.basic == basic) {
    return;
  }
  if (basic != BasicType::kInt && basic != BasicType::kFloat) {
    throw SemanticError("变量或常量声明不能使用 void 类型");
  }
  type_.basic = basic;
}

void VarDefAST::GenerateKoopaIR(IrContext& context) const {
  if (!type_.IsNumericScalar() && !type_.IsArray()) {
    throw SemanticError("变量定义只支持 int/float 标量或数组");
  }
  if (context.IsInGlobalScope()) {
    std::string ir_name = context.NewGlobalName(name_);
    Symbol symbol;
    symbol.kind = SymbolKind::kVariable;
    symbol.source_name = name_;
    symbol.ir_name = ir_name;
    symbol.type = type_;
    context.symbols().Define(symbol);
    if (type_.IsArray()) {
      std::string init_text = "zeroinit";
      if (init_ != nullptr) {
        if (type_.basic == BasicType::kFloat) {
          std::vector<double> values =
              init_->FlattenConstFloat(context, type_.ElementCount());
          if (std::any_of(values.begin(), values.end(),
                          [](double value) { return value != 0.0; })) {
            size_t offset = 0;
            init_text =
                AggregateFloatString(values, type_.array_dims, 0, &offset);
          }
        } else {
          std::vector<int> values =
              init_->FlattenConst(context, type_.ElementCount());
          if (std::any_of(values.begin(), values.end(),
                          [](int value) { return value != 0; })) {
            size_t offset = 0;
            init_text = AggregateString(values, type_.array_dims, 0, &offset);
          }
        }
      }
      context.EmitGlobalLine("global " + ir_name + " = alloc " +
                             type_.ToKoopaString() + ", " + init_text);
      return;
    }
    ExprResult init_value =
        type_.IsFloatScalar() ? ExprResult::ConstFloat(0.0)
                              : ExprResult::ConstInt(0);
    if (init_ != nullptr) {
      ExprResult init = init_->GenerateScalarIR(context);
      if (!HasConstFloatOrInt(init)) {
        throw SemanticError("全局变量初始化不是常量表达式: " + name_);
      }
      init_value = ConstCast(std::move(init), type_, "全局变量初始化");
    }
    context.EmitGlobalLine("global " + ir_name + " = alloc " +
                           type_.ToKoopaString() + ", " + ConstExprText(init_value));
    return;
  }

  std::string ir_name = context.NewLocalName(name_);
  Symbol symbol;
  symbol.kind = SymbolKind::kVariable;
  symbol.source_name = name_;
  symbol.ir_name = ir_name;
  symbol.type = type_;
  context.symbols().Define(symbol);

  context.EmitInstruction(ir_name + " = alloc " + type_.ToKoopaString());
  if (init_ != nullptr) {
    if (type_.IsArray()) {
      std::vector<std::string> values_text;
      if (type_.basic == BasicType::kFloat) {
        std::vector<double> values =
            init_->FlattenConstFloat(context, type_.ElementCount());
        for (double value : values) {
          values_text.push_back(FloatLiteral(value));
        }
      } else {
        std::vector<int> values =
            init_->FlattenConst(context, type_.ElementCount());
        for (int value : values) {
          values_text.push_back(std::to_string(value));
        }
      }
      for (int flat = 0; flat < static_cast<int>(values_text.size()); ++flat) {
        std::string addr = ir_name;
        std::vector<int> indices = FlatIndexToIndices(flat, type_.array_dims);
        for (int index : indices) {
          std::string temp = context.NewTemp();
          context.EmitInstruction(temp + " = getelemptr " + addr + ", " +
                                  std::to_string(index));
          addr = temp;
        }
        context.EmitInstruction("store " + values_text[flat] + ", " + addr);
      }
      return;
    }
    ExprResult init = init_->GenerateScalarIR(context);
    init = CastTo(context, std::move(init), type_, "变量初始化");
    context.EmitInstruction("store " + init.AsOperand() + ", " + ir_name);
  }
}

void VarDefAST::GenerateConstKoopaIR(IrContext& context) const {
  if (!type_.IsNumericScalar() && !type_.IsArray()) {
    throw SemanticError("常量定义只支持 int/float 标量或数组");
  }
  Symbol symbol;
  symbol.kind = SymbolKind::kConstant;
  symbol.source_name = name_;
  if (type_.IsArray()) {
    symbol.ir_name = context.IsInGlobalScope() ? context.NewGlobalName(name_)
                                               : context.NewLocalName(name_);
  } else {
    symbol.ir_name = "";
  }
  symbol.type = type_;
  if (type_.IsArray()) {
    if (init_ == nullptr) {
      throw SemanticError("const 数组必须有初始化值: " + name_);
    }
    if (type_.basic == BasicType::kFloat) {
      symbol.const_float_array_values =
          init_->FlattenConstFloat(context, type_.ElementCount());
    } else {
      symbol.const_array_values =
          init_->FlattenConst(context, type_.ElementCount());
    }
    context.symbols().Define(symbol);
    std::string init_text = "zeroinit";
    if (type_.basic == BasicType::kFloat) {
      if (std::any_of(symbol.const_float_array_values.begin(),
                      symbol.const_float_array_values.end(),
                      [](double value) { return value != 0.0; })) {
        size_t offset = 0;
        init_text = AggregateFloatString(symbol.const_float_array_values,
                                         type_.array_dims, 0, &offset);
      }
    } else {
      if (std::any_of(symbol.const_array_values.begin(),
                      symbol.const_array_values.end(),
                      [](int value) { return value != 0; })) {
        size_t offset = 0;
        init_text = AggregateString(symbol.const_array_values, type_.array_dims,
                                    0, &offset);
      }
    }
    if (context.IsInGlobalScope()) {
      context.EmitGlobalLine("global " + symbol.ir_name + " = alloc " +
                             type_.ToKoopaString() + ", " + init_text);
    } else {
      context.EmitInstruction(symbol.ir_name + " = alloc " +
                              type_.ToKoopaString());
      int count = type_.ElementCount();
      for (int flat = 0; flat < count; ++flat) {
        std::string addr = symbol.ir_name;
        for (int index : FlatIndexToIndices(flat, type_.array_dims)) {
          std::string temp = context.NewTemp();
          context.EmitInstruction(temp + " = getelemptr " + addr + ", " +
                                  std::to_string(index));
          addr = temp;
        }
        std::string value_text =
            type_.basic == BasicType::kFloat
                ? FloatLiteral(symbol.const_float_array_values[flat])
                : std::to_string(symbol.const_array_values[flat]);
        context.EmitInstruction("store " + value_text + ", " + addr);
      }
    }
    return;
  }
  if (init_ == nullptr) {
    throw SemanticError("const 必须有初始化值: " + name_);
  }
  ExprResult value = ConstCast(init_->GenerateScalarIR(context), type_,
                               "常量初始化");
  if (type_.IsFloatScalar()) {
    symbol.const_float_value = value.const_float_value;
  } else {
    symbol.const_value = value.const_value;
  }
  context.symbols().Define(symbol);
}

int VarDefAST::RequireConstInit(IrContext& context) const {
  if (init_ == nullptr) {
    throw SemanticError("const int 必须有初始化值: " + name_);
  }
  ExprResult init = init_->GenerateScalarIR(context);
  init.RequireValue("常量初始化");
  if (!init.const_value.has_value()) {
    throw SemanticError("const int 初始化不是编译期常量: " + name_);
  }
  return *init.const_value;
}

VarDeclAST::VarDeclAST(Type type, std::vector<std::unique_ptr<VarDefAST>> defs)
    : type_(std::move(type)), defs_(std::move(defs)) {
  RequireNumericScalar(type_, "变量声明");
  for (const auto& def : defs_) {
    def->ApplyBaseType(type_.basic);
  }
}

std::unique_ptr<VarDeclAST> VarDeclAST::MakeInt(
    std::vector<std::unique_ptr<VarDefAST>> defs) {
  return std::unique_ptr<VarDeclAST>(
      new VarDeclAST(Type::Int(), std::move(defs)));
}

std::unique_ptr<VarDeclAST> VarDeclAST::MakeFloat(
    std::vector<std::unique_ptr<VarDefAST>> defs) {
  return std::unique_ptr<VarDeclAST>(
      new VarDeclAST(Type::Float(), std::move(defs)));
}

void VarDeclAST::GenerateKoopaIR(IrContext& context) const {
  for (const auto& def : defs_) {
    def->GenerateKoopaIR(context);
  }
}

ConstDeclAST::ConstDeclAST(Type type,
                           std::vector<std::unique_ptr<VarDefAST>> defs)
    : type_(std::move(type)), defs_(std::move(defs)) {
  RequireNumericScalar(type_, "常量声明");
  for (const auto& def : defs_) {
    def->ApplyBaseType(type_.basic);
  }
}

std::unique_ptr<ConstDeclAST> ConstDeclAST::MakeInt(
    std::vector<std::unique_ptr<VarDefAST>> defs) {
  return std::unique_ptr<ConstDeclAST>(
      new ConstDeclAST(Type::Int(), std::move(defs)));
}

std::unique_ptr<ConstDeclAST> ConstDeclAST::MakeFloat(
    std::vector<std::unique_ptr<VarDefAST>> defs) {
  return std::unique_ptr<ConstDeclAST>(
      new ConstDeclAST(Type::Float(), std::move(defs)));
}

void ConstDeclAST::GenerateKoopaIR(IrContext& context) const {
  for (const auto& def : defs_) {
    def->GenerateConstKoopaIR(context);
  }
}

StmtAST::StmtAST(StmtKind kind) : kind_(kind) {}

std::unique_ptr<StmtAST> StmtAST::MakeExpr(std::unique_ptr<ExprAST> expr) {
  std::unique_ptr<StmtAST> stmt(new StmtAST(StmtKind::kExpr));
  stmt->expr_ = std::move(expr);
  return stmt;
}

std::unique_ptr<StmtAST> StmtAST::MakeAssign(std::unique_ptr<LValAST> lval,
                                             std::unique_ptr<ExprAST> expr) {
  std::unique_ptr<StmtAST> stmt(new StmtAST(StmtKind::kAssign));
  stmt->lval_ = std::move(lval);
  stmt->expr_ = std::move(expr);
  return stmt;
}

std::unique_ptr<StmtAST> StmtAST::MakeReturn(std::unique_ptr<ExprAST> expr) {
  std::unique_ptr<StmtAST> stmt(new StmtAST(StmtKind::kReturn));
  stmt->expr_ = std::move(expr);
  return stmt;
}

std::unique_ptr<StmtAST> StmtAST::MakeReturnVoid() {
  return std::unique_ptr<StmtAST>(new StmtAST(StmtKind::kReturn));
}

std::unique_ptr<StmtAST> StmtAST::MakeIf(std::unique_ptr<ExprAST> cond,
                                         std::unique_ptr<StmtAST> then_stmt,
                                         std::unique_ptr<StmtAST> else_stmt) {
  std::unique_ptr<StmtAST> stmt(new StmtAST(StmtKind::kIf));
  stmt->cond_ = std::move(cond);
  stmt->then_stmt_ = std::move(then_stmt);
  stmt->else_stmt_ = std::move(else_stmt);
  return stmt;
}

std::unique_ptr<StmtAST> StmtAST::MakeWhile(std::unique_ptr<ExprAST> cond,
                                            std::unique_ptr<StmtAST> body) {
  std::unique_ptr<StmtAST> stmt(new StmtAST(StmtKind::kWhile));
  stmt->cond_ = std::move(cond);
  stmt->then_stmt_ = std::move(body);
  return stmt;
}

std::unique_ptr<StmtAST> StmtAST::MakeBreak() {
  return std::unique_ptr<StmtAST>(new StmtAST(StmtKind::kBreak));
}

std::unique_ptr<StmtAST> StmtAST::MakeContinue() {
  return std::unique_ptr<StmtAST>(new StmtAST(StmtKind::kContinue));
}

std::unique_ptr<StmtAST> StmtAST::MakeBlock(std::unique_ptr<BlockAST> block) {
  std::unique_ptr<StmtAST> stmt(new StmtAST(StmtKind::kBlock));
  stmt->block_ = std::move(block);
  return stmt;
}

std::unique_ptr<StmtAST> StmtAST::MakeEmpty() {
  return std::unique_ptr<StmtAST>(new StmtAST(StmtKind::kEmpty));
}

void StmtAST::GenerateKoopaIR(IrContext& context) const {
  if (context.IsBlockTerminated()) {
    return;
  }

  switch (kind_) {
    case StmtKind::kExpr:
      if (expr_ != nullptr) {
        expr_->GenerateExprIR(context);
      }
      return;

    case StmtKind::kAssign: {
      ExprResult addr = lval_->GenerateAddressIR(context);
      ExprResult value = expr_->GenerateExprIR(context);
      value = CastTo(context, std::move(value), addr.type, "赋值语句右侧");
      context.EmitInstruction("store " + value.AsOperand() + ", " +
                              addr.value_name);
      return;
    }

    case StmtKind::kReturn: {
      const Type& return_type = context.CurrentFunctionReturnType();
      if (return_type.IsVoid()) {
        if (expr_ != nullptr) {
          throw SemanticError("void 函数 return 不能带返回值");
        }
        context.EmitTerminator("ret");
        return;
      }
      if (expr_ == nullptr) {
        throw SemanticError("非 void 函数 return 缺少返回值");
      }
      ExprResult value = expr_->GenerateExprIR(context);
      value = CastTo(context, std::move(value), return_type, "return 表达式");
      context.EmitTerminator("ret " + value.AsOperand());
      return;
    }

    case StmtKind::kIf: {
      ExprResult cond = cond_->GenerateExprIR(context);
      ExprResult cond_bool = context.EmitToBool(cond);
      std::string then_label = context.NewLabel("if_then");
      std::string else_label = context.NewLabel("if_else");
      std::string end_label = context.NewLabel("if_end");
      context.EmitTerminator("br " + cond_bool.AsOperand() + ", " +
                             then_label + ", " +
                             (else_stmt_ ? else_label : end_label));

      context.EmitLabel(then_label);
      then_stmt_->GenerateKoopaIR(context);
      bool then_terminated = context.IsBlockTerminated();
      if (!then_terminated) {
        context.EmitTerminator("jump " + end_label);
      }

      bool else_terminated = false;
      if (else_stmt_ != nullptr) {
        context.EmitLabel(else_label);
        else_stmt_->GenerateKoopaIR(context);
        else_terminated = context.IsBlockTerminated();
        if (!else_terminated) {
          context.EmitTerminator("jump " + end_label);
        }
      }

      if (else_stmt_ != nullptr && then_terminated && else_terminated) {
        context.MarkBlockTerminated();
        return;
      }
      context.EmitLabel(end_label);
      return;
    }

    case StmtKind::kWhile: {
      std::string cond_label = context.NewLabel("while_cond");
      std::string body_label = context.NewLabel("while_body");
      std::string end_label = context.NewLabel("while_end");
      context.EmitTerminator("jump " + cond_label);

      context.EmitLabel(cond_label);
      ExprResult cond = cond_->GenerateExprIR(context);
      ExprResult cond_bool = context.EmitToBool(cond);
      context.EmitTerminator("br " + cond_bool.AsOperand() + ", " +
                             body_label + ", " + end_label);

      context.EmitLabel(body_label);
      context.PushLoop(end_label, cond_label);
      then_stmt_->GenerateKoopaIR(context);
      context.PopLoop();
      if (!context.IsBlockTerminated()) {
        context.EmitTerminator("jump " + cond_label);
      }

      context.EmitLabel(end_label);
      return;
    }

    case StmtKind::kBreak: {
      const LoopLabels& loop = context.CurrentLoop();
      context.EmitTerminator("jump " + loop.break_label);
      return;
    }

    case StmtKind::kContinue: {
      const LoopLabels& loop = context.CurrentLoop();
      context.EmitTerminator("jump " + loop.continue_label);
      return;
    }

    case StmtKind::kBlock:
      block_->GenerateKoopaIR(context);
      return;

    case StmtKind::kEmpty:
      return;
  }
}

void BlockAST::AddItem(std::unique_ptr<BaseAST> item) {
  items_.push_back(std::move(item));
}

void BlockAST::GenerateKoopaIR(IrContext& context) const {
  // Block 拥有自己的作用域，局部定义离开 block 后不可见；
  // 这就是需要作用域栈而不是单张符号表的原因。
  context.symbols().PushScope();
  for (const auto& item : items_) {
    if (context.IsBlockTerminated()) {
      break;
    }
    item->GenerateKoopaIR(context);
  }
  context.symbols().PopScope();
}

FuncDefAST::FuncDefAST(Type return_type, std::string name,
                       std::vector<std::unique_ptr<FuncFParamAST>> params,
                       std::unique_ptr<BlockAST> body)
    : return_type_(std::move(return_type)),
      name_(std::move(name)),
      params_(std::move(params)),
      body_(std::move(body)) {}

Symbol FuncDefAST::BuildFunctionSymbol() const {
  std::vector<Type> param_types;
  param_types.reserve(params_.size());
  for (const auto& param : params_) {
    if (!param->type().IsNumericScalar() && !param->type().IsArrayParameter()) {
      throw SemanticError("函数形参只支持 int/float 标量或数组参数");
    }
    param_types.push_back(param->type());
  }

  Symbol function_symbol;
  function_symbol.kind = SymbolKind::kFunction;
  function_symbol.source_name = name_;
  function_symbol.ir_name = "@" + MangleLocalHint(name_);
  function_symbol.type = return_type_;
  function_symbol.param_types = param_types;
  return function_symbol;
}

std::vector<std::pair<std::string, Type>> FuncDefAST::BuildParamList() const {
  std::vector<std::pair<std::string, Type>> params;
  params.reserve(params_.size());
  for (const auto& param : params_) {
    if (!param->type().IsNumericScalar() && !param->type().IsArrayParameter()) {
      throw SemanticError("函数形参只支持 int/float 标量或数组参数");
    }
    params.push_back({param->name(), param->type()});
  }
  return params;
}

void FuncDefAST::DeclareFunctionSymbol(IrContext& context) const {
  if (!context.IsInGlobalScope()) {
    throw SemanticError("函数定义只能出现在全局作用域");
  }
  context.symbols().Define(BuildFunctionSymbol());
}

void FuncDefAST::GenerateKoopaIR(IrContext& context) const {
  if (!context.IsInGlobalScope()) {
    throw SemanticError("函数定义只能出现在全局作用域");
  }

  Symbol function_symbol = BuildFunctionSymbol();
  if (context.symbols().Lookup(name_) == nullptr) {
    context.symbols().Define(function_symbol);
  }
  std::vector<std::pair<std::string, Type>> params = BuildParamList();

  context.BeginFunction(function_symbol.ir_name, return_type_, params);
  for (const auto& param : params_) {
    std::string param_addr = context.NewLocalName(param->name());
    Symbol param_symbol;
    param_symbol.kind = SymbolKind::kVariable;
    param_symbol.source_name = param->name();
    param_symbol.ir_name = param_addr;
    param_symbol.type = param->type();
    context.symbols().Define(param_symbol);

    // Koopa 函数参数本身是 SSA 值，不是可写内存地址。
    // SysY 参数在函数体里表现得像普通局部变量，后续可能被赋值，
    // 所以进入函数时必须 alloc 一个栈槽，再把形参值 store 进去。
    context.EmitInstruction(param_addr + " = alloc " +
                            param->type().ToKoopaString());
    context.EmitInstruction("store @" + MangleLocalHint(param->name()) + ", " +
                            param_addr);
  }
  body_->GenerateKoopaIR(context);
  context.EndFunction();
}

void CompUnitAST::AddUnit(std::unique_ptr<BaseAST> unit) {
  units_.push_back(std::move(unit));
}

void CompUnitAST::GenerateKoopaIR(IrContext& context) const {
  context.EmitBuiltins();
  // 先预注册所有函数签名，允许 main 调用后面才定义的函数。
  // TODO: 后续可以在这里强制检查 main 存在且返回 int。
  for (const auto& unit : units_) {
    const auto* function = dynamic_cast<const FuncDefAST*>(unit.get());
    if (function != nullptr) {
      function->DeclareFunctionSymbol(context);
    }
  }
  for (const auto& unit : units_) {
    unit->GenerateKoopaIR(context);
  }
}

std::string CompUnitAST::GenerateKoopaIR() const {
  IrContext context;
  GenerateKoopaIR(context);
  return context.GetIR();
}

std::unique_ptr<NumberAST> MakeNumber(int value) {
  return std::unique_ptr<NumberAST>(new NumberAST(value));
}

std::unique_ptr<FloatNumberAST> MakeFloatNumber(double value) {
  return std::unique_ptr<FloatNumberAST>(new FloatNumberAST(value));
}

std::unique_ptr<LValAST> MakeLVal(std::string name) {
  return std::unique_ptr<LValAST>(new LValAST(std::move(name)));
}

std::unique_ptr<LValAST> MakeLVal(
    std::string name, std::vector<std::unique_ptr<ExprAST>> indices) {
  return std::unique_ptr<LValAST>(
      new LValAST(std::move(name), std::move(indices)));
}

std::unique_ptr<UnaryExpAST> MakeUnary(UnaryOp op,
                                       std::unique_ptr<ExprAST> operand) {
  return std::unique_ptr<UnaryExpAST>(
      new UnaryExpAST(op, std::move(operand)));
}

std::unique_ptr<BinaryExpAST> MakeBinary(BinaryOp op,
                                         std::unique_ptr<ExprAST> lhs,
                                         std::unique_ptr<ExprAST> rhs) {
  return std::unique_ptr<BinaryExpAST>(
      new BinaryExpAST(op, std::move(lhs), std::move(rhs)));
}

std::unique_ptr<FuncCallAST> MakeCall(
    std::string name, std::vector<std::unique_ptr<ExprAST>> args) {
  return std::unique_ptr<FuncCallAST>(
      new FuncCallAST(std::move(name), std::move(args)));
}

}  // namespace sysy
