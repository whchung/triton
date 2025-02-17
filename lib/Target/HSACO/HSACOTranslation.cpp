#include "triton/Target/HSACO/HSACOTranslation.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/ExecutionEngine/OptUtils.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dialect.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "mlir/Target/LLVMIR/LLVMTranslationInterface.h"
#include "triton/Target/LLVMIR/LLVMIRTranslation.h"
#include "triton/Tools/Sys/GetEnv.hpp"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <regex>
#include <cstdio>
#include <iostream>
#include <memory>

namespace {

void init_llvm() {
  LLVMInitializeAMDGPUTarget();
  LLVMInitializeAMDGPUTargetInfo();
  LLVMInitializeAMDGPUTargetMC();
  LLVMInitializeAMDGPUAsmParser();
  LLVMInitializeAMDGPUAsmPrinter();
}

std::unique_ptr<llvm::TargetMachine> initialize_module(llvm::Module* module,
                                                       const std::string& triple,
                                                       const std::string& proc,
                                                       const std::string& features) {
  // verify and store llvm
  llvm::legacy::PassManager pm;
  pm.add(llvm::createVerifierPass());
  pm.run(*module);

  module->setTargetTriple(triple);

  std::string error;
  auto target =
      llvm::TargetRegistry::lookupTarget(module->getTargetTriple(), error);
  llvm::TargetOptions opt;
  opt.AllowFPOpFusion = llvm::FPOpFusion::Fast;
  opt.UnsafeFPMath = false;
  opt.NoInfsFPMath = false;
  opt.NoNaNsFPMath = true;
  llvm::TargetMachine *machine = target->createTargetMachine(
      module->getTargetTriple(), proc, features, opt, llvm::Reloc::PIC_,
      llvm::None, llvm::CodeGenOpt::Aggressive);

  module->setDataLayout(machine->createDataLayout());

  for (llvm::Function &f : module->functions())
    f.addFnAttr(llvm::Attribute::AlwaysInline);

  return std::unique_ptr<llvm::TargetMachine>(machine);
}

std::string generate_amdgcn_assembly(llvm::Module* module,
                                     const std::string& triple,
                                     const std::string& proc,
                                     const std::string& features) {
  auto machine = initialize_module(module, triple, proc, features);

  llvm::SmallVector<char, 0> buffer;
  llvm::legacy::PassManager pass;
  llvm::raw_svector_ostream stream(buffer);

  // emit
  machine->addPassesToEmitFile(pass, stream, nullptr,
                               llvm::CodeGenFileType::CGFT_AssemblyFile);
  pass.run(*module);

  std::string amdgcn(buffer.begin(), buffer.end());
  if (::triton::tools::getBoolEnv("AMDGCN_ENABLE_DUMP")) {
    std::cout << "// -----// AMDGCN Dump //----- //\n" << amdgcn << std::endl;
  }

  return amdgcn;
}

std::string generate_hsaco(llvm::Module* module,
                           const std::string& triple,
                           const std::string& proc,
                           const std::string& features) {
  auto machine = initialize_module(module, triple, proc, features);

  std::string kernel_name = std::string(std::tmpnam(nullptr)) + "_" + module->getModuleIdentifier();

  // create dump files
  std::error_code ec;
  // Save GCN ISA binary.
  std::string isabin_path = kernel_name + std::string(".o");
  std::unique_ptr<llvm::raw_fd_ostream> isabin_fs(
      new llvm::raw_fd_ostream(isabin_path, ec, llvm::sys::fs::OF_Text));
  if (ec)
  {
    std::cout << isabin_path << " was not created. error code: " << ec << std::endl;
  }
  // emit
  llvm::legacy::PassManager pass;
  machine->addPassesToEmitFile(pass, *isabin_fs, nullptr, llvm::CGFT_ObjectFile);
  pass.run(*module);

  // generate HASCO file
  std::string hsaco_path = kernel_name + std::string(".hsaco");

  std::string error_message;
  int lld_result =
      llvm::sys::ExecuteAndWait("/opt/rocm/llvm/bin/ld.lld",
                                {"/opt/rocm/llvm/bin/ld.lld", "-flavor", "gnu", "-shared", "-o", hsaco_path, isabin_path},
                                llvm::None, {}, 0, 0, &error_message);
  if (lld_result)
  {
    std::cout << "ld.lld execute fail: " << std::endl;
    std::cout << error_message << std::endl;
    std::cout << lld_result << std::endl;
  }

  return hsaco_path;
}

std::tuple<std::string, std::string> llir_to_amdgcn_and_hsaco(llvm::Module* module,
                                                   std::string cc) {
  // create
  std::string triple = "amdgcn-amd-amdhsa";
  std::string proc = cc;
  std::string features = "+sramecc,-xnack";

  init_llvm();

  // verify and store llvm
  auto module_obj = llvm::CloneModule(*module);
  auto amdgcn = generate_amdgcn_assembly(module, triple, proc, features);
  auto hsaco_path = generate_hsaco(module_obj.get(), triple, proc, features);

  return std::make_tuple(amdgcn, hsaco_path);
}

}

namespace triton {

std::tuple<std::string, std::string> translateLLVMIRToHSACO(llvm::Module& module,
                                                            std::string cc) {
  auto hsacoCode = llir_to_amdgcn_and_hsaco(&module, cc);
  return hsacoCode;
}

} // namespace triton
