#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OperationSupport.h"
#include "triton/Dialect.h"
#include "triton/Types.h"

#define GET_OP_CLASSES
#include "triton/Ops.cpp.inc"

// enum attribute definitions
#include "triton/OpsEnums.cpp.inc"

namespace mlir {
namespace triton {

//-- StoreOp --
// Default mask
void StoreOp::build(::mlir::OpBuilder &builder, ::mlir::OperationState &state, ::mlir::Value ptr, ::mlir::Value value) {
  TensorType ptrType = ptr.getType().dyn_cast<TensorType>();
  auto shape = ptrType.getShape();
  ::mlir::Value mask = builder.create<arith::ConstantOp>(
    ptr.getLoc(),
    RankedTensorType::get(shape, builder.getI1Type()),
    DenseIntElementsAttr::get(
      RankedTensorType::get(shape, builder.getI1Type()), true
    )
  );
  state.addOperands(ptr);
  state.addOperands(value);
  state.addOperands(mask);
}

//-- LoadOp --
void LoadOp::build(::mlir::OpBuilder &builder, ::mlir::OperationState &state, ::mlir::Value ptr) {
  TensorType ptrType = ptr.getType().dyn_cast<TensorType>();
  Type elementType = ptrType.getElementType().dyn_cast<PointerType>().getPointeeType();
  auto shape = ptrType.getShape();
  // mask
  ::mlir::Value mask = builder.create<arith::ConstantOp>(
    ptr.getLoc(),
    RankedTensorType::get(shape, builder.getI1Type()),
    DenseIntElementsAttr::get(
      RankedTensorType::get(shape, builder.getI1Type()), true
    )
  );
  // other
  Type resultType = RankedTensorType::get(shape, elementType);
  ::mlir::Value other = builder.create<arith::ConstantOp>(
    ptr.getLoc(),
    resultType,
    DenseElementsAttr::get(
      resultType, builder.getZeroAttr(elementType)
    )
  );
  state.addOperands(ptr);
  state.addOperands(mask);
  state.addOperands(other);
  state.addTypes({resultType});
}

} // namespace triton
} // namespace mlir