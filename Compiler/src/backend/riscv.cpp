#include "backend/riscv.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "koopa.h"

namespace sysy {
namespace {

int AlignTo(int value, int align) {
  if (align <= 0) {
    return value;
  }
  return (value + align - 1) / align * align;
}

std::string SanitizeName(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return "";
  }
  if (name[0] == '%' || name[0] == '@') {
    return std::string(name + 1);
  }
  return std::string(name);
}

std::string BlockLabel(const char* name) {
  std::string base = SanitizeName(name);
  if (base.empty()) {
    return ".Lanon";
  }
  return ".L" + base;
}

int TypeSize(koopa_raw_type_t type) {
  if (type == nullptr) {
    return 0;
  }
  switch (type->tag) {
    case KOOPA_RTT_INT32:
    case KOOPA_RTT_POINTER:
      return 4;
    case KOOPA_RTT_ARRAY:
      return static_cast<int>(type->data.array.len) *
             TypeSize(type->data.array.base);
    case KOOPA_RTT_UNIT:
      return 0;
    default:
      return 4;
  }
}

bool FitsImm12(int value) { return value >= -2048 && value <= 2047; }

struct FunctionFrame {
  int outgoing_arg_size = 0;
  int frame_size = 0;
  int ra_offset = 0;
  std::unordered_map<koopa_raw_value_t, int> offsets;
  std::unordered_map<koopa_raw_value_t, int> alloc_sizes;
};

class RiscvEmitter {
 public:
  explicit RiscvEmitter(std::ostream& out) : out_(out) {}

  void EmitProgram(const koopa_raw_program_t& program) {
    EmitGlobals(program);
    EmitFunctions(program);
  }

 private:
  struct ConstEvalResult {
    bool ok = false;
    int32_t value = 0;
  };

  struct BinaryKey {
    koopa_raw_binary_op_t op;
    koopa_raw_value_t lhs;
    koopa_raw_value_t rhs;
  };

  struct BinaryKeyHash {
    size_t operator()(const BinaryKey& key) const {
      size_t h1 = std::hash<const void*>{}(static_cast<const void*>(key.lhs));
      size_t h2 = std::hash<const void*>{}(static_cast<const void*>(key.rhs));
      size_t h3 = std::hash<int>{}(static_cast<int>(key.op));
      return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
  };

  struct BinaryKeyEq {
    bool operator()(const BinaryKey& a, const BinaryKey& b) const {
      return a.op == b.op && a.lhs == b.lhs && a.rhs == b.rhs;
    }
  };

  void EmitGlobals(const koopa_raw_program_t& program) {
    const auto& values = program.values;
    if (values.len == 0) {
      return;
    }
    out_ << ".data\n";
    for (size_t i = 0; i < values.len; ++i) {
      auto value = static_cast<koopa_raw_value_t>(values.buffer[i]);
      if (value->kind.tag != KOOPA_RVT_GLOBAL_ALLOC) {
        continue;
      }
      EmitGlobalAlloc(value);
    }
    out_ << "\n";
  }

  void EmitGlobalAlloc(koopa_raw_value_t value) {
    const auto& global = value->kind.data.global_alloc;
    std::string name = SanitizeName(value->name);
    out_ << ".globl " << name << "\n";
    out_ << ".align 2\n";
    out_ << name << ":\n";

    koopa_raw_type_t base_type = value->ty->data.pointer.base;
    EmitGlobalInit(global.init, base_type);
  }

  void EmitGlobalInit(koopa_raw_value_t init, koopa_raw_type_t type) {
    switch (init->kind.tag) {
      case KOOPA_RVT_ZERO_INIT:
        out_ << "  .zero " << TypeSize(type) << "\n";
        return;
      case KOOPA_RVT_INTEGER:
        out_ << "  .word " << init->kind.data.integer.value << "\n";
        return;
      case KOOPA_RVT_AGGREGATE:
        EmitAggregate(init, type);
        return;
      default:
        out_ << "  .zero " << TypeSize(type) << "\n";
        return;
    }
  }

  void EmitAggregate(koopa_raw_value_t init, koopa_raw_type_t type) {
    const auto& aggregate = init->kind.data.aggregate;
    koopa_raw_type_t elem_type = type->data.array.base;
    for (size_t i = 0; i < aggregate.elems.len; ++i) {
      auto elem = static_cast<koopa_raw_value_t>(aggregate.elems.buffer[i]);
      EmitGlobalInit(elem, elem_type);
    }
  }

  void EmitFunctions(const koopa_raw_program_t& program) {
    const auto& funcs = program.funcs;
    if (funcs.len == 0) {
      return;
    }
    out_ << ".text\n";
    for (size_t i = 0; i < funcs.len; ++i) {
      auto func = static_cast<koopa_raw_function_t>(funcs.buffer[i]);
      if (func->bbs.len == 0) {
        continue;
      }
      EmitFunction(func);
    }
  }

  void EmitFunction(koopa_raw_function_t func) {
    std::string name = SanitizeName(func->name);
    out_ << ".globl " << name << "\n";
    out_ << ".align 2\n";
    out_ << name << ":\n";

    FunctionFrame frame = BuildFrame(func);
    EmitPrologue(frame);
    StoreParams(func, frame);
    EmitFunctionBody(func, frame);
  }

  FunctionFrame BuildFrame(koopa_raw_function_t func) {
    FunctionFrame frame;

    int max_out_args = 0;
    const auto& bbs = func->bbs;
    for (size_t i = 0; i < bbs.len; ++i) {
      auto bb = static_cast<koopa_raw_basic_block_t>(bbs.buffer[i]);
      const auto& insts = bb->insts;
      for (size_t j = 0; j < insts.len; ++j) {
        auto value = static_cast<koopa_raw_value_t>(insts.buffer[j]);
        if (value->kind.tag == KOOPA_RVT_CALL) {
          int argc = static_cast<int>(value->kind.data.call.args.len);
          if (argc > 8) {
            max_out_args = std::max(max_out_args, (argc - 8) * 4);
          }
        }
      }
    }

    frame.outgoing_arg_size = AlignTo(max_out_args, 4);
    int offset = frame.outgoing_arg_size;

    for (size_t i = 0; i < func->params.len; ++i) {
      auto param = static_cast<koopa_raw_value_t>(func->params.buffer[i]);
      offset = AlignTo(offset, 4);
      frame.offsets[param] = offset;
      offset += 4;
    }

    for (size_t i = 0; i < bbs.len; ++i) {
      auto bb = static_cast<koopa_raw_basic_block_t>(bbs.buffer[i]);
      const auto& insts = bb->insts;
      for (size_t j = 0; j < insts.len; ++j) {
        auto value = static_cast<koopa_raw_value_t>(insts.buffer[j]);
        if (!NeedsStackSlot(value)) {
          continue;
        }
        if (frame.offsets.find(value) != frame.offsets.end()) {
          continue;
        }
        int size = 4;
        if (value->kind.tag == KOOPA_RVT_ALLOC) {
          size = TypeSize(value->ty->data.pointer.base);
          frame.alloc_sizes[value] = size;
        }
        offset = AlignTo(offset, 4);
        frame.offsets[value] = offset;
        offset += size;
      }
    }

    frame.frame_size = AlignTo(offset + 4, 16);
    frame.ra_offset = frame.frame_size - 4;
    return frame;
  }

  bool NeedsStackSlot(koopa_raw_value_t value) const {
    if (!HasUses(value)) {
      return false;
    }
    switch (value->kind.tag) {
      case KOOPA_RVT_ALLOC:
      case KOOPA_RVT_BINARY:
      case KOOPA_RVT_LOAD:
      case KOOPA_RVT_GET_PTR:
      case KOOPA_RVT_GET_ELEM_PTR:
      case KOOPA_RVT_CALL:
        return true;
      default:
        return false;
    }
  }

  void EmitPrologue(const FunctionFrame& frame) {
    out_ << "  addi sp, sp, -" << frame.frame_size << "\n";
    EmitStoreIntReg("ra", frame.ra_offset);
  }

  void EmitEpilogue(const FunctionFrame& frame) {
    EmitLoadIntReg("ra", frame.ra_offset);
    out_ << "  addi sp, sp, " << frame.frame_size << "\n";
    out_ << "  ret\n";
  }

  void EmitFunctionBody(koopa_raw_function_t func, const FunctionFrame& frame) {
    const auto& bbs = func->bbs;
    for (size_t i = 0; i < bbs.len; ++i) {
      auto bb = static_cast<koopa_raw_basic_block_t>(bbs.buffer[i]);
      local_store_cache_.clear();
      binary_cse_cache_.clear();
      out_ << BlockLabel(bb->name) << ":\n";
      const auto& insts = bb->insts;
      for (size_t j = 0; j < insts.len; ++j) {
        auto value = static_cast<koopa_raw_value_t>(insts.buffer[j]);
        EmitValue(value, frame);
      }
    }
  }

  void EmitValue(koopa_raw_value_t value, const FunctionFrame& frame) {
    switch (value->kind.tag) {
      case KOOPA_RVT_ALLOC:
        return;
      case KOOPA_RVT_LOAD:
        if (!HasUses(value)) {
          return;
        }
        EmitLoad(value, frame);
        return;
      case KOOPA_RVT_STORE:
        EmitStore(value, frame);
        return;
      case KOOPA_RVT_BINARY:
        if (!HasUses(value)) {
          return;
        }
        EmitBinary(value, frame);
        return;
      case KOOPA_RVT_RETURN:
        EmitReturn(value, frame);
        return;
      case KOOPA_RVT_JUMP:
        EmitJump(value);
        return;
      case KOOPA_RVT_BRANCH:
        EmitBranch(value, frame);
        return;
      case KOOPA_RVT_CALL:
        EmitCall(value, frame);
        return;
      case KOOPA_RVT_GET_PTR:
        if (!HasUses(value)) {
          return;
        }
        EmitGetPtr(value, frame);
        return;
      case KOOPA_RVT_GET_ELEM_PTR:
        if (!HasUses(value)) {
          return;
        }
        EmitGetElemPtr(value, frame);
        return;
      default:
        return;
    }
  }

  void StoreParams(koopa_raw_function_t func, const FunctionFrame& frame) {
    for (size_t i = 0; i < func->params.len; ++i) {
      auto param = static_cast<koopa_raw_value_t>(func->params.buffer[i]);
      if (i < 8) {
        StoreIntValue(param, "a" + std::to_string(i), frame);
        continue;
      }
      int arg_offset = frame.frame_size + static_cast<int>((i - 8) * 4);
      EmitLoadIntReg("t0", arg_offset);
      StoreIntValue(param, "t0", frame);
    }
  }

  void EmitLoad(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& load = value->kind.data.load;
    if (load.src->kind.tag == KOOPA_RVT_ALLOC) {
      auto iter = local_store_cache_.find(load.src);
      if (iter != local_store_cache_.end()) {
        LoadIntValue(iter->second, "t1", frame);
        StoreIntValue(value, "t1", frame);
        return;
      }
    }
    LoadAddress(load.src, "t0", frame);
    out_ << "  lw t1, 0(t0)\n";
    StoreIntValue(value, "t1", frame);
  }

  void EmitStore(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& store = value->kind.data.store;
    LoadAddress(store.dest, "t0", frame);
    LoadIntValue(store.value, "t1", frame);
    out_ << "  sw t1, 0(t0)\n";
    if (store.dest->kind.tag == KOOPA_RVT_ALLOC) {
      local_store_cache_[store.dest] = store.value;
    } else {
      local_store_cache_.clear();
    }
  }

  void EmitBinary(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& binary = value->kind.data.binary;
    BinaryKey key = NormalizeBinaryKey(binary.op, binary.lhs, binary.rhs);
    auto cse_iter = binary_cse_cache_.find(key);
    if (cse_iter != binary_cse_cache_.end()) {
      LoadIntValue(cse_iter->second, "t2", frame);
      StoreIntValue(value, "t2", frame);
      CacheConstValueFrom(cse_iter->second, value);
      return;
    }
    ConstEvalResult folded = TryFoldBinary(binary.op, binary.lhs, binary.rhs);
    if (folded.ok) {
      out_ << "  li t2, " << folded.value << "\n";
      StoreIntValue(value, "t2", frame);
      const_value_cache_[value] = folded.value;
      return;
    }
    LoadIntValue(binary.lhs, "t0", frame);
    LoadIntValue(binary.rhs, "t1", frame);
    switch (binary.op) {
      case KOOPA_RBO_ADD:
        out_ << "  add t2, t0, t1\n";
        break;
      case KOOPA_RBO_SUB:
        out_ << "  sub t2, t0, t1\n";
        break;
      case KOOPA_RBO_MUL:
        out_ << "  mul t2, t0, t1\n";
        break;
      case KOOPA_RBO_DIV:
        out_ << "  div t2, t0, t1\n";
        break;
      case KOOPA_RBO_MOD:
        out_ << "  rem t2, t0, t1\n";
        break;
      case KOOPA_RBO_EQ:
        out_ << "  xor t2, t0, t1\n";
        out_ << "  seqz t2, t2\n";
        break;
      case KOOPA_RBO_NOT_EQ:
        out_ << "  xor t2, t0, t1\n";
        out_ << "  snez t2, t2\n";
        break;
      case KOOPA_RBO_LT:
        out_ << "  slt t2, t0, t1\n";
        break;
      case KOOPA_RBO_GT:
        out_ << "  slt t2, t1, t0\n";
        break;
      case KOOPA_RBO_LE:
        out_ << "  slt t2, t1, t0\n";
        out_ << "  xori t2, t2, 1\n";
        break;
      case KOOPA_RBO_GE:
        out_ << "  slt t2, t0, t1\n";
        out_ << "  xori t2, t2, 1\n";
        break;
      case KOOPA_RBO_AND:
        out_ << "  and t2, t0, t1\n";
        break;
      case KOOPA_RBO_OR:
        out_ << "  or t2, t0, t1\n";
        break;
      case KOOPA_RBO_XOR:
        out_ << "  xor t2, t0, t1\n";
        break;
      case KOOPA_RBO_SHL:
        out_ << "  sll t2, t0, t1\n";
        break;
      case KOOPA_RBO_SHR:
        out_ << "  srl t2, t0, t1\n";
        break;
      case KOOPA_RBO_SAR:
        out_ << "  sra t2, t0, t1\n";
        break;
      default:
        out_ << "  add t2, t0, t1\n";
        break;
    }
    StoreIntValue(value, "t2", frame);
    binary_cse_cache_[key] = value;
  }

  void EmitReturn(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& ret = value->kind.data.ret;
    if (ret.value != nullptr) {
      LoadIntValue(ret.value, "a0", frame);
    }
    EmitEpilogue(frame);
  }

  void EmitJump(koopa_raw_value_t value) {
    const auto& jump = value->kind.data.jump;
    out_ << "  j " << BlockLabel(jump.target->name) << "\n";
  }

  void EmitBranch(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& branch = value->kind.data.branch;
    auto cond_const = TryGetConstValue(branch.cond);
    if (cond_const.has_value()) {
      if (*cond_const != 0) {
        out_ << "  j " << BlockLabel(branch.true_bb->name) << "\n";
      } else {
        out_ << "  j " << BlockLabel(branch.false_bb->name) << "\n";
      }
      return;
    }
    LoadIntValue(branch.cond, "t0", frame);
    out_ << "  bnez t0, " << BlockLabel(branch.true_bb->name) << "\n";
    out_ << "  j " << BlockLabel(branch.false_bb->name) << "\n";
  }

  void EmitCall(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& call = value->kind.data.call;

    for (size_t i = 0; i < call.args.len; ++i) {
      auto arg = static_cast<koopa_raw_value_t>(call.args.buffer[i]);
      if (i < 8) {
        LoadIntValue(arg, "a" + std::to_string(i), frame);
      } else {
        int offset = static_cast<int>((i - 8) * 4);
        LoadIntValue(arg, "t0", frame);
        EmitStoreIntReg("t0", offset);
      }
    }

    std::string callee = SanitizeName(call.callee->name);
    out_ << "  call " << callee << "\n";

    if (value->ty->tag == KOOPA_RTT_UNIT) {
      return;
    }
    if (!HasUses(value)) {
      return;
    }
    StoreIntValue(value, "a0", frame);
  }

  void EmitGetPtr(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& get_ptr = value->kind.data.get_ptr;
    LoadAddress(get_ptr.src, "t0", frame);
    LoadIntValue(get_ptr.index, "t1", frame);
    int elem_size = TypeSize(get_ptr.src->ty->data.pointer.base);
    EmitScaledAdd("t0", "t1", elem_size, "t2");
    StoreIntValue(value, "t2", frame);
    local_store_cache_.clear();
    binary_cse_cache_.clear();
  }

  void EmitGetElemPtr(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& get_elem_ptr = value->kind.data.get_elem_ptr;
    LoadAddress(get_elem_ptr.src, "t0", frame);
    LoadIntValue(get_elem_ptr.index, "t1", frame);
    koopa_raw_type_t array_type = get_elem_ptr.src->ty->data.pointer.base;
    int elem_size = TypeSize(array_type->data.array.base);
    EmitScaledAdd("t0", "t1", elem_size, "t2");
    StoreIntValue(value, "t2", frame);
    local_store_cache_.clear();
    binary_cse_cache_.clear();
  }

  void EmitScaledAdd(const std::string& base, const std::string& index,
                     int scale, const std::string& dst) {
    if (scale == 1) {
      out_ << "  add " << dst << ", " << base << ", " << index << "\n";
      return;
    }
    out_ << "  li t3, " << scale << "\n";
    out_ << "  mul t3, " << index << ", t3\n";
    out_ << "  add " << dst << ", " << base << ", t3\n";
  }

  void LoadIntValue(koopa_raw_value_t value, const std::string& reg,
                    const FunctionFrame& frame) {
    auto const_iter = const_value_cache_.find(value);
    if (const_iter != const_value_cache_.end()) {
      out_ << "  li " << reg << ", " << const_iter->second << "\n";
      return;
    }
    switch (value->kind.tag) {
      case KOOPA_RVT_INTEGER:
        out_ << "  li " << reg << ", " << value->kind.data.integer.value
             << "\n";
        return;
      case KOOPA_RVT_GLOBAL_ALLOC: {
        std::string name = SanitizeName(value->name);
        out_ << "  la " << reg << ", " << name << "\n";
        out_ << "  lw " << reg << ", 0(" << reg << ")\n";
        return;
      }
      case KOOPA_RVT_UNDEF:
        out_ << "  li " << reg << ", 0\n";
        return;
      default:
        break;
    }

    auto iter = frame.offsets.find(value);
    if (iter == frame.offsets.end()) {
      out_ << "  li " << reg << ", 0\n";
      return;
    }
    EmitLoadIntReg(reg, iter->second);
  }

  void StoreIntValue(koopa_raw_value_t value, const std::string& reg,
                     const FunctionFrame& frame) {
    auto iter = frame.offsets.find(value);
    if (iter == frame.offsets.end()) {
      return;
    }
    EmitStoreIntReg(reg, iter->second);
  }

  ConstEvalResult TryFoldBinary(koopa_raw_binary_op_t op,
                                koopa_raw_value_t lhs,
                                koopa_raw_value_t rhs) const {
    ConstEvalResult result;
    if (lhs->kind.tag != KOOPA_RVT_INTEGER || rhs->kind.tag != KOOPA_RVT_INTEGER) {
      return result;
    }
    int32_t a = lhs->kind.data.integer.value;
    int32_t b = rhs->kind.data.integer.value;
    switch (op) {
      case KOOPA_RBO_ADD:
        result.ok = true;
        result.value = a + b;
        return result;
      case KOOPA_RBO_SUB:
        result.ok = true;
        result.value = a - b;
        return result;
      case KOOPA_RBO_MUL:
        result.ok = true;
        result.value = a * b;
        return result;
      case KOOPA_RBO_DIV:
        if (b == 0) {
          return result;
        }
        result.ok = true;
        result.value = a / b;
        return result;
      case KOOPA_RBO_MOD:
        if (b == 0) {
          return result;
        }
        result.ok = true;
        result.value = a % b;
        return result;
      case KOOPA_RBO_EQ:
        result.ok = true;
        result.value = (a == b) ? 1 : 0;
        return result;
      case KOOPA_RBO_NOT_EQ:
        result.ok = true;
        result.value = (a != b) ? 1 : 0;
        return result;
      case KOOPA_RBO_LT:
        result.ok = true;
        result.value = (a < b) ? 1 : 0;
        return result;
      case KOOPA_RBO_GT:
        result.ok = true;
        result.value = (a > b) ? 1 : 0;
        return result;
      case KOOPA_RBO_LE:
        result.ok = true;
        result.value = (a <= b) ? 1 : 0;
        return result;
      case KOOPA_RBO_GE:
        result.ok = true;
        result.value = (a >= b) ? 1 : 0;
        return result;
      case KOOPA_RBO_AND:
        result.ok = true;
        result.value = a & b;
        return result;
      case KOOPA_RBO_OR:
        result.ok = true;
        result.value = a | b;
        return result;
      case KOOPA_RBO_XOR:
        result.ok = true;
        result.value = a ^ b;
        return result;
      case KOOPA_RBO_SHL:
        result.ok = true;
        result.value = static_cast<int32_t>(static_cast<uint32_t>(a) << (b & 31));
        return result;
      case KOOPA_RBO_SHR:
        result.ok = true;
        result.value = static_cast<int32_t>(static_cast<uint32_t>(a) >> (b & 31));
        return result;
      case KOOPA_RBO_SAR:
        result.ok = true;
        result.value = static_cast<int32_t>(a >> (b & 31));
        return result;
      default:
        return result;
    }
  }

  bool HasUses(koopa_raw_value_t value) const {
    return value->used_by.len > 0;
  }

  BinaryKey NormalizeBinaryKey(koopa_raw_binary_op_t op,
                               koopa_raw_value_t lhs,
                               koopa_raw_value_t rhs) const {
    bool commutative = op == KOOPA_RBO_ADD || op == KOOPA_RBO_MUL ||
                       op == KOOPA_RBO_AND || op == KOOPA_RBO_OR ||
                       op == KOOPA_RBO_XOR || op == KOOPA_RBO_EQ ||
                       op == KOOPA_RBO_NOT_EQ;
    if (commutative && rhs < lhs) {
      return {op, rhs, lhs};
    }
    return {op, lhs, rhs};
  }

  std::optional<int32_t> TryGetConstValue(koopa_raw_value_t value) const {
    if (value->kind.tag == KOOPA_RVT_INTEGER) {
      return value->kind.data.integer.value;
    }
    auto iter = const_value_cache_.find(value);
    if (iter != const_value_cache_.end()) {
      return iter->second;
    }
    return std::nullopt;
  }

  void CacheConstValueFrom(koopa_raw_value_t from, koopa_raw_value_t to) {
    auto iter = const_value_cache_.find(from);
    if (iter == const_value_cache_.end()) {
      return;
    }
    const_value_cache_[to] = iter->second;
  }

  void LoadAddress(koopa_raw_value_t value, const std::string& reg,
                   const FunctionFrame& frame) {
    if (value->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
      std::string name = SanitizeName(value->name);
      out_ << "  la " << reg << ", " << name << "\n";
      return;
    }
    if (value->kind.tag == KOOPA_RVT_ALLOC) {
      auto iter = frame.offsets.find(value);
      if (iter == frame.offsets.end()) {
        out_ << "  li " << reg << ", 0\n";
        return;
      }
      EmitAddressFromStack(reg, iter->second);
      return;
    }
    LoadIntValue(value, reg, frame);
  }

  void EmitAddressFromStack(const std::string& reg, int offset) {
    if (FitsImm12(offset)) {
      out_ << "  addi " << reg << ", sp, " << offset << "\n";
      return;
    }
    out_ << "  li " << reg << ", " << offset << "\n";
    out_ << "  add " << reg << ", sp, " << reg << "\n";
  }

  void EmitLoadIntReg(const std::string& reg, int offset) {
    if (FitsImm12(offset)) {
      out_ << "  lw " << reg << ", " << offset << "(sp)\n";
      return;
    }
    out_ << "  li t6, " << offset << "\n";
    out_ << "  add t6, sp, t6\n";
    out_ << "  lw " << reg << ", 0(t6)\n";
  }

  void EmitStoreIntReg(const std::string& reg, int offset) {
    if (FitsImm12(offset)) {
      out_ << "  sw " << reg << ", " << offset << "(sp)\n";
      return;
    }
    out_ << "  li t6, " << offset << "\n";
    out_ << "  add t6, sp, t6\n";
    out_ << "  sw " << reg << ", 0(t6)\n";
  }


  std::ostream& out_;
  std::unordered_map<koopa_raw_value_t, koopa_raw_value_t> local_store_cache_;
  std::unordered_map<BinaryKey, koopa_raw_value_t, BinaryKeyHash, BinaryKeyEq>
      binary_cse_cache_;
  std::unordered_map<koopa_raw_value_t, int32_t> const_value_cache_;
};

}  // namespace

bool GenerateRiscvFromKoopaIR(const std::string& koopa_ir, std::ostream& out,
                              std::string* error_message) {
  koopa_program_t program = nullptr;
  koopa_error_code_t error = koopa_parse_from_string(koopa_ir.c_str(), &program);
  if (error != KOOPA_EC_SUCCESS) {
    if (error_message != nullptr) {
      *error_message = "koopa parse failed";
    }
    return false;
  }

  koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
  koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
  koopa_delete_program(program);

  RiscvEmitter emitter(out);
  emitter.EmitProgram(raw);

  koopa_delete_raw_program_builder(builder);
  return true;
}

}  // namespace sysy
