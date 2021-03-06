//===-- WebAssemblyUtilities.cpp - WebAssembly Utility Functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements several utility functions for WebAssembly.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyUtilities.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
using namespace llvm;

const char *const WebAssembly::ClangCallTerminateFn = "__clang_call_terminate";
const char *const WebAssembly::CxaBeginCatchFn = "__cxa_begin_catch";
const char *const WebAssembly::CxaRethrowFn = "__cxa_rethrow";
const char *const WebAssembly::StdTerminateFn = "_ZSt9terminatev";
const char *const WebAssembly::PersonalityWrapperFn =
    "_Unwind_Wasm_CallPersonality";

/// Test whether MI is a child of some other node in an expression tree.
bool WebAssembly::isChild(const MachineInstr &MI,
                          const WebAssemblyFunctionInfo &MFI) {
  if (MI.getNumOperands() == 0)
    return false;
  const MachineOperand &MO = MI.getOperand(0);
  if (!MO.isReg() || MO.isImplicit() || !MO.isDef())
    return false;
  Register Reg = MO.getReg();
  return Register::isVirtualRegister(Reg) && MFI.isVRegStackified(Reg);
}

bool WebAssembly::mayThrow(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::THROW:
  case WebAssembly::THROW_S:
  case WebAssembly::RETHROW:
  case WebAssembly::RETHROW_S:
    return true;
  }
  if (isCallIndirect(MI.getOpcode()))
    return true;
  if (!MI.isCall())
    return false;

  const MachineOperand &MO = getCalleeOp(MI);
  assert(MO.isGlobal() || MO.isSymbol());

  if (MO.isSymbol()) {
    // Some intrinsics are lowered to calls to external symbols, which are then
    // lowered to calls to library functions. Most of libcalls don't throw, but
    // we only list some of them here now.
    // TODO Consider adding 'nounwind' info in TargetLowering::CallLoweringInfo
    // instead for more accurate info.
    const char *Name = MO.getSymbolName();
    if (strcmp(Name, "memcpy") == 0 || strcmp(Name, "memmove") == 0 ||
        strcmp(Name, "memset") == 0)
      return false;
    return true;
  }

  const auto *F = dyn_cast<Function>(MO.getGlobal());
  if (!F)
    return true;
  if (F->doesNotThrow())
    return false;
  // These functions never throw
  if (F->getName() == CxaBeginCatchFn || F->getName() == PersonalityWrapperFn ||
      F->getName() == ClangCallTerminateFn || F->getName() == StdTerminateFn)
    return false;

  // TODO Can we exclude call instructions that are marked as 'nounwind' in the
  // original LLVm IR? (Even when the callee may throw)
  return true;
}

inline const MachineOperand &getCalleeOp(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case WebAssembly::CALL_VOID:
  case WebAssembly::CALL_VOID_S:
  case WebAssembly::CALL_INDIRECT_VOID:
  case WebAssembly::CALL_INDIRECT_VOID_S:
  case WebAssembly::RET_CALL:
  case WebAssembly::RET_CALL_S:
  case WebAssembly::RET_CALL_INDIRECT:
  case WebAssembly::RET_CALL_INDIRECT_S:
    return MI.getOperand(0);
  case WebAssembly::CALL_i32:
  case WebAssembly::CALL_i32_S:
  case WebAssembly::CALL_i64:
  case WebAssembly::CALL_i64_S:
  case WebAssembly::CALL_f32:
  case WebAssembly::CALL_f32_S:
  case WebAssembly::CALL_f64:
  case WebAssembly::CALL_f64_S:
  case WebAssembly::CALL_v16i8:
  case WebAssembly::CALL_v16i8_S:
  case WebAssembly::CALL_v8i16:
  case WebAssembly::CALL_v8i16_S:
  case WebAssembly::CALL_v4i32:
  case WebAssembly::CALL_v4i32_S:
  case WebAssembly::CALL_v2i64:
  case WebAssembly::CALL_v2i64_S:
  case WebAssembly::CALL_v4f32:
  case WebAssembly::CALL_v4f32_S:
  case WebAssembly::CALL_v2f64:
  case WebAssembly::CALL_v2f64_S:
  case WebAssembly::CALL_exnref:
  case WebAssembly::CALL_exnref_S:
  case WebAssembly::CALL_INDIRECT_i32:
  case WebAssembly::CALL_INDIRECT_i32_S:
  case WebAssembly::CALL_INDIRECT_i64:
  case WebAssembly::CALL_INDIRECT_i64_S:
  case WebAssembly::CALL_INDIRECT_f32:
  case WebAssembly::CALL_INDIRECT_f32_S:
  case WebAssembly::CALL_INDIRECT_f64:
  case WebAssembly::CALL_INDIRECT_f64_S:
  case WebAssembly::CALL_INDIRECT_v16i8:
  case WebAssembly::CALL_INDIRECT_v16i8_S:
  case WebAssembly::CALL_INDIRECT_v8i16:
  case WebAssembly::CALL_INDIRECT_v8i16_S:
  case WebAssembly::CALL_INDIRECT_v4i32:
  case WebAssembly::CALL_INDIRECT_v4i32_S:
  case WebAssembly::CALL_INDIRECT_v2i64:
  case WebAssembly::CALL_INDIRECT_v2i64_S:
  case WebAssembly::CALL_INDIRECT_v4f32:
  case WebAssembly::CALL_INDIRECT_v4f32_S:
  case WebAssembly::CALL_INDIRECT_v2f64:
  case WebAssembly::CALL_INDIRECT_v2f64_S:
  case WebAssembly::CALL_INDIRECT_exnref:
  case WebAssembly::CALL_INDIRECT_exnref_S:
    return MI.getOperand(1);
  case WebAssembly::CALL:
  case WebAssembly::CALL_S:
    return MI.getOperand(MI.getNumDefs());
  default:
    llvm_unreachable("Not a call instruction");
  }
}
