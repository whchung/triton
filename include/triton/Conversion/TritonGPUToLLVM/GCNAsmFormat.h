#ifndef TRITON_CONVERSION_TRITON_GPU_TO_LLVM_GCN_FORMAT_H_
#define TRITON_CONVERSION_TRITON_GPU_TO_LLVM_GCN_FORMAT_H_

#include "mlir/IR/Value.h"
#include "triton/Dialect/Triton/IR/Dialect.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>

namespace mlir {
class ConversionPatternRewriter;
class Location;

namespace triton {
using llvm::StringRef;

class GCNInstr;
class GCNInstrCommon;
class GCNInstrExecution;

struct GCNBuilder {
  struct Operand {
    std::string constraint;
    Value value;
    int idx{-1};
    llvm::SmallVector<Operand *> list;
    std::function<std::string(int idx)> repr;

    // for list
    Operand() = default;
    Operand(const Operation &) = delete;
    Operand(Value value, StringRef constraint)
        : value(value), constraint(constraint) {}

    bool isList() const { return !value && constraint.empty(); }

    Operand *listAppend(Operand *arg) {
      list.push_back(arg);
      return this;
    }

    Operand *listGet(size_t nth) const {
      assert(nth < list.size());
      return list[nth];
    }

    std::string dump() const;
  };

struct Modifier {
    Value value;
    std::string modifier;
    std::string arg;
    llvm::SmallVector<Modifier *> list;

    Modifier() = default;
    Modifier(const Operation &) = delete;
    Modifier(Value value, StringRef arg) : value(value), arg(arg) {}

    bool isList() const { return !value && modifier.empty(); }

    Modifier *listAppend(Modifier *arg) {
      list.push_back(arg);
      return this;
    }

    Modifier *listGet(size_t index) const {
      assert(index < list.size());
      return list[index];
    }

    std::string to_str() const {
      std::string str = modifier;
      if (!arg.empty()) {
        str += ":" + arg;
      }
      return str;
    }

    std::string dump() const;
  };

  template <typename INSTR = GCNInstr, typename... Args>
  INSTR *create(Args &&...args) {
    instrs.emplace_back(std::make_unique<INSTR>(this, args...));
    return static_cast<INSTR *>(instrs.back().get());
  }

  // Create a list of operands.
  Operand *newListOperand() { return newOperand(); }

  Operand *newListOperand(ArrayRef<std::pair<mlir::Value, std::string>> items) {
    auto *list = newOperand();
    for (auto &item : items) {
      list->listAppend(newOperand(item.first, item.second));
    }
    return list;
  }

  Operand *newListOperand(unsigned count, mlir::Value val,
                          const std::string &constraint) {
    auto *list = newOperand();
    for (int i = 0; i < count; ++i) {
      list->listAppend(newOperand(val, constraint));
    }
    return list;
  }

  Operand *newListOperand(unsigned count, const std::string &constraint) {
    auto *list = newOperand();
    for (int i = 0; i < count; ++i) {
      list->listAppend(newOperand(constraint));
    }
    return list;
  }

  // Create a new operand. It will not add to operand list.
  // @value: the MLIR value bind to this operand.
  // @constraint: ASM operand constraint, .e.g. "=r"
  // @formatter: extra format to represent this operand in ASM code, default is
  //             "%{0}".format(operand.idx).
  Operand *newOperand(mlir::Value value, StringRef constraint,
                      std::function<std::string(int idx)> formatter = nullptr);

  // Create a new operand which is written to, that is, the constraint starts
  // with "=", e.g. "=r".
  Operand *newOperand(StringRef constraint);

  // Create a constant integer operand.
  Operand *newConstantOperand(int v);
  // Create a constant operand with explicit code specified.
  Operand *newConstantOperand(const std::string &v);

  Operand *newAddrOperand(mlir::Value addr, StringRef constraint);

  Modifier *newModifier(StringRef modifier, StringRef arg);

  llvm::SmallVector<Operand *, 4> getAllArgs() const;

  llvm::SmallVector<Value, 4> getAllMLIRArgs() const;

  std::string getConstraints() const;

  std::string dump() const;

  mlir::Value launch(ConversionPatternRewriter &rewriter, Location loc,
                     Type resTy, bool hasSideEffect = true,
                     bool isAlignStack = false,
                     ArrayRef<Attribute> attrs = {}) const;

private:
  Operand *newOperand() {
    argArchive.emplace_back(std::make_unique<Operand>());
    return argArchive.back().get();
  }

  Modifier *newModifier() {
    modArchive.emplace_back(std::make_unique<Modifier>());
    return modArchive.back().get();
  }

  friend class GCNInstr;
  friend class GCNInstrCommon;

protected:
  llvm::SmallVector<std::unique_ptr<Operand>, 6> argArchive;
  llvm::SmallVector<std::unique_ptr<Modifier>, 2> modArchive;
  llvm::SmallVector<std::unique_ptr<GCNInstrCommon>, 2> instrs;
  llvm::SmallVector<std::unique_ptr<GCNInstrExecution>, 4> executions;
  int oprCounter{};
};

// GCN instruction common interface.
// Put the generic logic for all the instructions here.
struct GCNInstrCommon {
  explicit GCNInstrCommon(GCNBuilder *builder) : builder(builder) {}

  using Operand = GCNBuilder::Operand;
  using Modifier = GCNBuilder::Modifier;

  // clang-format off
  GCNInstrExecution& operator()() { return call({}, {}); }
  GCNInstrExecution& operator()(Operand* a) { return call({a}, {}); }
  GCNInstrExecution& operator()(Operand* a, Operand* b) { return call({a, b}, {}); }
  GCNInstrExecution& operator()(Operand* a, Operand* b, Operand* c) { return call({a, b, c}, {}); }
  GCNInstrExecution& operator()(Operand* a, Operand* b, Operand* c, Operand* d) { return call({a, b, c, d}, {}); }
  GCNInstrExecution& operator()(Operand* a, Operand* b, Operand* c, Operand* d, Operand * e) { return call({a, b, c, d, e}, {}); }
  GCNInstrExecution& operator()(Operand* a, Operand* b, Operand* c, Operand* d, Operand * e, Operand* f) { return call({a, b, c, d, e, f}, {}); }
  GCNInstrExecution& operator()(Operand* a, Operand* b, Operand* c, Operand* d, Operand * e, Operand* f, Operand* g) { return call({a, b, c, d, e, f, g}, {}); }
  // clang-format on

  // Set operands of this instruction.
  GCNInstrExecution &operator()(llvm::ArrayRef<Operand *> oprs, llvm::ArrayRef<Modifier*> mods);

protected:
  GCNInstrExecution &call(llvm::ArrayRef<Operand *> oprs, ArrayRef<Modifier *> mods);

  GCNBuilder *builder{};
  llvm::SmallVector<std::string, 4> instrParts;

  friend class GCNInstrExecution;
};

template <class ConcreteT> struct GCNInstrBase : public GCNInstrCommon {
  using Operand = GCNBuilder::Operand;
  using Modifier = GCNBuilder::Modifier;

  explicit GCNInstrBase(GCNBuilder *builder, const std::string &name)
      : GCNInstrCommon(builder) {
    o(name);
  }

  ConcreteT &o(const std::string &suffix, bool predicate = true) {
    if (predicate)
      instrParts.push_back(suffix);
    return *static_cast<ConcreteT *>(this);
  }
};

enum VectorWidth {
  Byte = 8,
  Short = 16,
  Dword = 32,
  Qword = 64
};

struct GCNInstr : public GCNInstrBase<GCNInstr> {
  using GCNInstrBase<GCNInstr>::GCNInstrBase;

   GCNInstr &float_op_type(int width) {
    switch (width) {
    case Byte:
      assert(Byte != width);
      break;
    case Short:
      o("f16");
      break;
    case Dword:
      o("f32");
      break;
    case Qword:
      o("f64");
      break;
    default:
      break;
    }
    return *this;
  }
};

struct GCNInstrExecution {
  using Operand = GCNBuilder::Operand;
  using Modifier = GCNBuilder::Modifier;

  llvm::SmallVector<Operand *> argsInOrder;
  llvm::SmallVector<Modifier *> mods;

  GCNInstrExecution() = default;
  explicit GCNInstrExecution(GCNInstrCommon *instr,
                             llvm::ArrayRef<Operand *> oprs, llvm::ArrayRef<Modifier *> modifiers)
      : instr(instr), argsInOrder(oprs.begin(), oprs.end()), mods(modifiers.begin(), modifiers.end()) {}

  std::string dump() const;

  SmallVector<Operand *> getArgList() const;

  GCNInstrCommon *instr{};
};



struct GCNMemInstr : public GCNInstrBase<GCNMemInstr> {
  using GCNInstrBase<GCNMemInstr>::GCNInstrBase;
  // Add specific type suffix to instruction

  GCNMemInstr &load_type(int width) {
    switch (width) {
    case Byte:
      o("ubyte");
      break;
    case Short:
      o("ushort");
      break;
    case Dword:
      o("dword");
      break;
    case Qword:
      o("dwordx2");
      break;
    default:
      break;
    }
    return *this;
  }

  GCNMemInstr &store_type(int width) {
    switch (width) {
    case Byte:
      o("byte");
      break;
    case Short:
      o("short");
      break;
    case Dword:
      o("dword");
      break;
    case Qword:
      o("dwordx2");
      break;
    default:
      break;
    }
    return *this;
  }
};

} // namespace triton
} // namespace mlir

#endif // TRITON_CONVERSION_TRITON_GPU_TO_LLVM_ASM_FORMAT_H_
