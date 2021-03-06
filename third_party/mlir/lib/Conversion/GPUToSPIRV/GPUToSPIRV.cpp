//===- GPUToSPIRV.cp - MLIR SPIR-V lowering passes ------------------------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================
//
// This file implements a pass to convert a kernel function in the GPU Dialect
// into a spv.module operation
//
//===----------------------------------------------------------------------===//
#include "mlir/Conversion/StandardToSPIRV/ConvertStandardToSPIRV.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/SPIRV/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/SPIRVOps.h"
#include "mlir/Pass/Pass.h"

using namespace mlir;

namespace {

/// Pattern to convert a kernel function in GPU dialect (a FuncOp with the
/// attribute gpu.kernel) within a spv.module.
class KernelFnConversion final : public SPIRVFnLowering {
public:
  using SPIRVFnLowering::SPIRVFnLowering;

  PatternMatchResult
  matchAndRewrite(Operation *op, ArrayRef<Value *> operands,
                  ConversionPatternRewriter &rewriter) const override;
};
} // namespace

PatternMatchResult
KernelFnConversion::matchAndRewrite(Operation *op, ArrayRef<Value *> operands,
                                    ConversionPatternRewriter &rewriter) const {
  auto funcOp = cast<FuncOp>(op);
  FuncOp newFuncOp;
  if (!gpu::GPUDialect::isKernel(funcOp)) {
    return succeeded(lowerFunction(funcOp, operands, rewriter, newFuncOp))
               ? matchSuccess()
               : matchFailure();
  }

  if (failed(lowerAsEntryFunction(funcOp, operands, rewriter, newFuncOp))) {
    return matchFailure();
  }
  newFuncOp.getOperation()->removeAttr(Identifier::get(
      gpu::GPUDialect::getKernelFuncAttrName(), op->getContext()));
  return matchSuccess();
}

namespace {
/// Pass to lower GPU Dialect to SPIR-V. The pass only converts those functions
/// that have the "gpu.kernel" attribute, i.e. those functions that are
/// referenced in gpu::LaunchKernelOp operations. For each such function
///
/// 1) Create a spirv::ModuleOp, and clone the function into spirv::ModuleOp
/// (the original function is still needed by the gpu::LaunchKernelOp, so cannot
/// replace it).
///
/// 2) Lower the body of the spirv::ModuleOp.
class GPUToSPIRVPass : public ModulePass<GPUToSPIRVPass> {
  void runOnModule() override;
};
} // namespace

void GPUToSPIRVPass::runOnModule() {
  auto context = &getContext();
  auto module = getModule();

  SmallVector<Operation *, 4> spirvModules;
  for (auto funcOp : module.getOps<FuncOp>()) {
    if (gpu::GPUDialect::isKernel(funcOp)) {
      OpBuilder builder(module.getBodyRegion());
      // Create a new spirv::ModuleOp for this function, and clone the
      // function into it.
      // TODO : Generalize this to account for different extensions,
      // capabilities, extended_instruction_sets, other addressing models
      // and memory models.
      auto spvModule = builder.create<spirv::ModuleOp>(
          funcOp.getLoc(),
          builder.getI32IntegerAttr(
              static_cast<int32_t>(spirv::AddressingModel::Logical)),
          builder.getI32IntegerAttr(
              static_cast<int32_t>(spirv::MemoryModel::VulkanKHR)));
      OpBuilder moduleBuilder(spvModule.getOperation()->getRegion(0));
      moduleBuilder.clone(*funcOp.getOperation());
      spirvModules.push_back(spvModule);
    }
  }

  /// Dialect conversion to lower the functions with the spirv::ModuleOps.
  SPIRVTypeConverter typeConverter(context);
  SPIRVEntryFnTypeConverter entryFnConverter(context);
  OwningRewritePatternList patterns;
  patterns.insert<KernelFnConversion>(context, typeConverter, entryFnConverter);
  populateStandardToSPIRVPatterns(context, patterns);

  ConversionTarget target(*context);
  target.addLegalDialect<spirv::SPIRVDialect>();
  target.addDynamicallyLegalOp<FuncOp>(
      [&](FuncOp Op) { return typeConverter.isSignatureLegal(Op.getType()); });

  if (failed(applyFullConversion(spirvModules, target, std::move(patterns),
                                 &typeConverter))) {
    return signalPassFailure();
  }
}

ModulePassBase *createGPUToSPIRVPass() { return new GPUToSPIRVPass(); }

static PassRegistration<GPUToSPIRVPass>
    pass("convert-gpu-to-spirv", "Convert GPU dialect to SPIR-V dialect");
