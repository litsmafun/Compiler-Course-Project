#include "backend/riscv.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
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

bool IsFloatType(koopa_raw_type_t type) {
  if (type == nullptr) {
    return false;
  }
  switch (type->tag) {
    case KOOPA_RTT_FLOAT:
      return true;
    case KOOPA_RTT_POINTER:
      return IsFloatType(type->data.pointer.base);
    case KOOPA_RTT_ARRAY:
      return IsFloatType(type->data.array.base);
    default:
      return false;
  }
}

int TypeSize(koopa_raw_type_t type) {
  if (type == nullptr) {
    return 0;
  }
  switch (type->tag) {
    case KOOPA_RTT_INT32:
    case KOOPA_RTT_FLOAT:
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

uint32_t FloatToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
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

  void EmitProgram(koopa_raw_program_t program) {
    EmitGlobals(program);
    EmitFunctions(program);
  }

 private:
  void EmitGlobals(koopa_raw_program_t program) {
    const auto& values = program->values;
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
      case KOOPA_RVT_FLOAT: {
        float value = init->kind.data.float_.value;
        out_ << "  .word " << FloatToBits(value) << "\n";
        return;
      }
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

  void EmitFunctions(koopa_raw_program_t program) {
    const auto& funcs = program->funcs;
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

    const auto& bbs = func->bbs;
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
          size = TypeSize(value->kind.data.alloc.ty);
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
        EmitLoad(value, frame);
        return;
      case KOOPA_RVT_STORE:
        EmitStore(value, frame);
        return;
      case KOOPA_RVT_BINARY:
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
      case KOOPA_RVT_CONV:
        EmitConversion(value, frame);
        return;
      case KOOPA_RVT_GET_PTR:
        EmitGetPtr(value, frame);
        return;
      case KOOPA_RVT_GET_ELEM_PTR:
        EmitGetElemPtr(value, frame);
        return;
      default:
        return;
    }
  }

  void StoreParams(koopa_raw_function_t func, const FunctionFrame& frame) {
    for (size_t i = 0; i < func->params.len; ++i) {
      auto param = static_cast<koopa_raw_value_t>(func->params.buffer[i]);
      bool is_float = IsFloatType(param->ty);
      if (i < 8) {
        if (is_float) {
          StoreFloatValue(param, "fa" + std::to_string(i), frame);
        } else {
          StoreIntValue(param, "a" + std::to_string(i), frame);
        }
        continue;
      }
      int arg_offset = frame.frame_size + static_cast<int>((i - 8) * 4);
      if (is_float) {
        EmitLoadFloatReg("ft0", arg_offset);
        StoreFloatValue(param, "ft0", frame);
      } else {
        EmitLoadIntReg("t0", arg_offset);
        StoreIntValue(param, "t0", frame);
      }
    }
  }

  void EmitLoad(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& load = value->kind.data.load;
    bool is_float = IsFloatType(value->ty);
    LoadAddress(load.src, "t0", frame);
    if (is_float) {
      out_ << "  flw ft0, 0(t0)\n";
      StoreFloatValue(value, "ft0", frame);
    } else {
      out_ << "  lw t1, 0(t0)\n";
      StoreIntValue(value, "t1", frame);
    }
  }

  void EmitStore(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& store = value->kind.data.store;
    bool is_float = IsFloatType(store.value->ty);
    LoadAddress(store.dest, "t0", frame);
    if (is_float) {
      LoadFloatValue(store.value, "ft0", frame);
      out_ << "  fsw ft0, 0(t0)\n";
    } else {
      LoadIntValue(store.value, "t1", frame);
      out_ << "  sw t1, 0(t0)\n";
    }
  }

  void EmitBinary(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& binary = value->kind.data.binary;
    if (IsFloatType(binary.lhs->ty) || IsFloatType(binary.rhs->ty)) {
      EmitFloatBinary(value, frame);
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
      case KOOPA_RBO_NE:
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
      default:
        out_ << "  add t2, t0, t1\n";
        break;
    }
    StoreIntValue(value, "t2", frame);
  }

  void EmitFloatBinary(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& binary = value->kind.data.binary;
    LoadFloatValue(binary.lhs, "ft0", frame);
    LoadFloatValue(binary.rhs, "ft1", frame);
    switch (binary.op) {
      case KOOPA_RBO_FADD:
        out_ << "  fadd.s ft2, ft0, ft1\n";
        StoreFloatValue(value, "ft2", frame);
        return;
      case KOOPA_RBO_FSUB:
        out_ << "  fsub.s ft2, ft0, ft1\n";
        StoreFloatValue(value, "ft2", frame);
        return;
      case KOOPA_RBO_FMUL:
        out_ << "  fmul.s ft2, ft0, ft1\n";
        StoreFloatValue(value, "ft2", frame);
        return;
      case KOOPA_RBO_FDIV:
        out_ << "  fdiv.s ft2, ft0, ft1\n";
        StoreFloatValue(value, "ft2", frame);
        return;
      case KOOPA_RBO_FEQ:
        out_ << "  feq.s t2, ft0, ft1\n";
        StoreIntValue(value, "t2", frame);
        return;
      case KOOPA_RBO_FNE:
        out_ << "  feq.s t2, ft0, ft1\n";
        out_ << "  xori t2, t2, 1\n";
        StoreIntValue(value, "t2", frame);
        return;
      case KOOPA_RBO_FLT:
        out_ << "  flt.s t2, ft0, ft1\n";
        StoreIntValue(value, "t2", frame);
        return;
      case KOOPA_RBO_FGT:
        out_ << "  flt.s t2, ft1, ft0\n";
        StoreIntValue(value, "t2", frame);
        return;
      case KOOPA_RBO_FLE:
        out_ << "  fle.s t2, ft0, ft1\n";
        StoreIntValue(value, "t2", frame);
        return;
      case KOOPA_RBO_FGE:
        out_ << "  fle.s t2, ft1, ft0\n";
        StoreIntValue(value, "t2", frame);
        return;
      default:
        out_ << "  fadd.s ft2, ft0, ft1\n";
        StoreFloatValue(value, "ft2", frame);
        return;
    }
  }

  void EmitReturn(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& ret = value->kind.data.ret;
    if (ret.value != nullptr) {
      if (IsFloatType(ret.value->ty)) {
        LoadFloatValue(ret.value, "ft0", frame);
        out_ << "  fsgnj.s fa0, ft0, ft0\n";
      } else {
        LoadIntValue(ret.value, "a0", frame);
      }
    }
    EmitEpilogue(frame);
  }

  void EmitJump(koopa_raw_value_t value) {
    const auto& jump = value->kind.data.jump;
    out_ << "  j " << BlockLabel(jump.target->name) << "\n";
  }

  void EmitBranch(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& branch = value->kind.data.branch;
    LoadIntValue(branch.cond, "t0", frame);
    out_ << "  bnez t0, " << BlockLabel(branch.true_bb->name) << "\n";
    out_ << "  j " << BlockLabel(branch.false_bb->name) << "\n";
  }

  void EmitCall(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& call = value->kind.data.call;

    for (size_t i = 0; i < call.args.len; ++i) {
      auto arg = static_cast<koopa_raw_value_t>(call.args.buffer[i]);
      if (i < 8) {
        if (IsFloatType(arg->ty)) {
          LoadFloatValue(arg, "ft0", frame);
          out_ << "  fsgnj.s fa" << i << ", ft0, ft0\n";
        } else {
          LoadIntValue(arg, "a" + std::to_string(i), frame);
        }
      } else {
        int offset = static_cast<int>((i - 8) * 4);
        if (IsFloatType(arg->ty)) {
          LoadFloatValue(arg, "ft0", frame);
          EmitStoreFloatReg("ft0", offset);
        } else {
          LoadIntValue(arg, "t0", frame);
          EmitStoreIntReg("t0", offset);
        }
      }
    }

    std::string callee = SanitizeName(call.callee->name);
    out_ << "  call " << callee << "\n";

    if (value->ty->tag == KOOPA_RTT_UNIT) {
      return;
    }

    if (IsFloatType(value->ty)) {
      StoreFloatValue(value, "fa0", frame);
    } else {
      StoreIntValue(value, "a0", frame);
    }
  }

  void EmitConversion(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& conv = value->kind.data.conv;
    if (conv.kind == KOOPA_RCK_SITOFP) {
      LoadIntValue(conv.value, "t0", frame);
      out_ << "  fcvt.s.w ft0, t0\n";
      StoreFloatValue(value, "ft0", frame);
      return;
    }
    if (conv.kind == KOOPA_RCK_FPTOSI) {
      LoadFloatValue(conv.value, "ft0", frame);
      out_ << "  fcvt.w.s t0, ft0\n";
      StoreIntValue(value, "t0", frame);
      return;
    }
  }

  void EmitGetPtr(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& get_ptr = value->kind.data.get_ptr;
    LoadAddress(get_ptr.src, "t0", frame);
    LoadIntValue(get_ptr.index, "t1", frame);
    int elem_size = TypeSize(get_ptr.src->ty->data.pointer.base);
    EmitScaledAdd("t0", "t1", elem_size, "t2");
    StoreIntValue(value, "t2", frame);
  }

  void EmitGetElemPtr(koopa_raw_value_t value, const FunctionFrame& frame) {
    const auto& get_elem_ptr = value->kind.data.get_elem_ptr;
    LoadAddress(get_elem_ptr.src, "t0", frame);
    LoadIntValue(get_elem_ptr.index, "t1", frame);
    koopa_raw_type_t array_type = get_elem_ptr.src->ty->data.pointer.base;
    int elem_size = TypeSize(array_type->data.array.base);
    EmitScaledAdd("t0", "t1", elem_size, "t2");
    StoreIntValue(value, "t2", frame);
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
    switch (value->kind.tag) {
      case KOOPA_RVT_INTEGER:
        out_ << "  li " << reg << ", " << value->kind.data.integer.value
             << "\n";
        return;
      case KOOPA_RVT_FLOAT: {
        float f = value->kind.data.float_.value;
        out_ << "  li " << reg << ", " << FloatToBits(f) << "\n";
        return;
      }
      case KOOPA_RVT_GLOBAL_ALLOC: {
        std::string name = SanitizeName(value->name);
        out_ << "  la " << reg << ", " << name << "\n";
        out_ << "  lw " << reg << ", 0(" << reg << ")\n";
        return;
      }
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

  void LoadFloatValue(koopa_raw_value_t value, const std::string& reg,
                      const FunctionFrame& frame) {
    switch (value->kind.tag) {
      case KOOPA_RVT_FLOAT: {
        float f = value->kind.data.float_.value;
        out_ << "  li t0, " << FloatToBits(f) << "\n";
        out_ << "  fmv.w.x " << reg << ", t0\n";
        return;
      }
      case KOOPA_RVT_INTEGER:
        out_ << "  li t0, " << value->kind.data.integer.value << "\n";
        out_ << "  fcvt.s.w " << reg << ", t0\n";
        return;
      default:
        break;
    }

    auto iter = frame.offsets.find(value);
    if (iter == frame.offsets.end()) {
      out_ << "  fmv.w.x " << reg << ", zero\n";
      return;
    }
    EmitLoadFloatReg(reg, iter->second);
  }

  void StoreIntValue(koopa_raw_value_t value, const std::string& reg,
                     const FunctionFrame& frame) {
    auto iter = frame.offsets.find(value);
    if (iter == frame.offsets.end()) {
      return;
    }
    EmitStoreIntReg(reg, iter->second);
  }

  void StoreFloatValue(koopa_raw_value_t value, const std::string& reg,
                       const FunctionFrame& frame) {
    auto iter = frame.offsets.find(value);
    if (iter == frame.offsets.end()) {
      return;
    }
    EmitStoreFloatReg(reg, iter->second);
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

  void EmitLoadFloatReg(const std::string& reg, int offset) {
    if (FitsImm12(offset)) {
      out_ << "  flw " << reg << ", " << offset << "(sp)\n";
      return;
    }
    out_ << "  li t6, " << offset << "\n";
    out_ << "  add t6, sp, t6\n";
    out_ << "  flw " << reg << ", 0(t6)\n";
  }

  void EmitStoreFloatReg(const std::string& reg, int offset) {
    if (FitsImm12(offset)) {
      out_ << "  fsw " << reg << ", " << offset << "(sp)\n";
      return;
    }
    out_ << "  li t6, " << offset << "\n";
    out_ << "  add t6, sp, t6\n";
    out_ << "  fsw " << reg << ", 0(t6)\n";
  }

  std::ostream& out_;
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
