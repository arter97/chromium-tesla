// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_LOONG64

#include "src/api/api-arguments.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/logging/counters.h"
// For interpreter_entry_return_pc_offset. TODO(jkummerow): Drop.
#include "src/codegen/loong64/constants-loong64.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/heap/heap-inl.h"
#include "src/objects/cell.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/runtime/runtime.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/baseline/liftoff-assembler-defs.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void Builtins::Generate_Adaptor(MacroAssembler* masm, Address address) {
  __ li(kJavaScriptCallExtraArg1Register, ExternalReference::Create(address));
  __ TailCallBuiltin(Builtin::kAdaptorWithBuiltinExitFrame);
}

namespace {

enum class ArgumentsElementType {
  kRaw,    // Push arguments as they are.
  kHandle  // Dereference arguments before pushing.
};

void Generate_PushArguments(MacroAssembler* masm, Register array, Register argc,
                            Register scratch, Register scratch2,
                            ArgumentsElementType element_type) {
  DCHECK(!AreAliased(array, argc, scratch));
  Label loop, entry;
  __ Sub_d(scratch, argc, Operand(kJSArgcReceiverSlots));
  __ Branch(&entry);
  __ bind(&loop);
  __ Alsl_d(scratch2, scratch, array, kSystemPointerSizeLog2, t7);
  __ Ld_d(scratch2, MemOperand(scratch2, 0));
  if (element_type == ArgumentsElementType::kHandle) {
    __ Ld_d(scratch2, MemOperand(scratch2, 0));
  }
  __ Push(scratch2);
  __ bind(&entry);
  __ Add_d(scratch, scratch, Operand(-1));
  __ Branch(&loop, greater_equal, scratch, Operand(zero_reg));
}

void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0     : number of arguments
  //  -- a1     : constructor function
  //  -- a3     : new target
  //  -- cp     : context
  //  -- ra     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);

    // Preserve the incoming parameters on the stack.
    __ Push(cp, a0);

    // Set up pointer to first argument (skip receiver).
    __ Add_d(
        t2, fp,
        Operand(StandardFrameConstants::kCallerSPOffset + kSystemPointerSize));
    // Copy arguments and receiver to the expression stack.
    // t2: Pointer to start of arguments.
    // a0: Number of arguments.
    Generate_PushArguments(masm, t2, a0, t3, t0, ArgumentsElementType::kRaw);
    // The receiver for the builtin/api call.
    __ PushRoot(RootIndex::kTheHoleValue);

    // Call the function.
    // a0: number of arguments (untagged)
    // a1: constructor function
    // a3: new target
    __ InvokeFunctionWithNewTarget(a1, a3, a0, InvokeType::kCall);

    // Restore context from the frame.
    __ Ld_d(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
    // Restore arguments count from the frame.
    __ Ld_d(t3, MemOperand(fp, ConstructFrameConstants::kLengthOffset));
    // Leave construct frame.
  }

  // Remove caller arguments from the stack and return.
  __ DropArguments(t3, MacroAssembler::kCountIncludesReceiver);
  __ Ret();
}

}  // namespace

// The construct stub for ES5 constructor functions and ES6 class constructors.
void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  --      a0: number of arguments (untagged)
  //  --      a1: constructor function
  //  --      a3: new target
  //  --      cp: context
  //  --      ra: return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  // Enter a construct frame.
  FrameScope scope(masm, StackFrame::MANUAL);
  Label post_instantiation_deopt_entry, not_create_implicit_receiver;
  __ EnterFrame(StackFrame::CONSTRUCT);

  // Preserve the incoming parameters on the stack.
  __ Push(cp, a0, a1);
  __ PushRoot(RootIndex::kUndefinedValue);
  __ Push(a3);

  // ----------- S t a t e -------------
  //  --        sp[0*kSystemPointerSize]: new target
  //  --        sp[1*kSystemPointerSize]: padding
  //  -- a1 and sp[2*kSystemPointerSize]: constructor function
  //  --        sp[3*kSystemPointerSize]: number of arguments
  //  --        sp[4*kSystemPointerSize]: context
  // -----------------------------------

  __ LoadTaggedField(
      t2, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
  __ Ld_wu(t2, FieldMemOperand(t2, SharedFunctionInfo::kFlagsOffset));
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(t2);
  __ JumpIfIsInRange(
      t2, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver);

  // If not derived class constructor: Allocate the new receiver object.
  __ CallBuiltin(Builtin::kFastNewObject);
  __ Branch(&post_instantiation_deopt_entry);

  // Else: use TheHoleValue as receiver for constructor call
  __ bind(&not_create_implicit_receiver);
  __ LoadRoot(a0, RootIndex::kTheHoleValue);

  // ----------- S t a t e -------------
  //  --                          a0: receiver
  //  -- Slot 4 / sp[0*kSystemPointerSize]: new target
  //  -- Slot 3 / sp[1*kSystemPointerSize]: padding
  //  -- Slot 2 / sp[2*kSystemPointerSize]: constructor function
  //  -- Slot 1 / sp[3*kSystemPointerSize]: number of arguments
  //  -- Slot 0 / sp[4*kSystemPointerSize]: context
  // -----------------------------------
  // Deoptimizer enters here.
  masm->isolate()->heap()->SetConstructStubCreateDeoptPCOffset(
      masm->pc_offset());
  __ bind(&post_instantiation_deopt_entry);

  // Restore new target.
  __ Pop(a3);

  // Push the allocated receiver to the stack.
  __ Push(a0);

  // We need two copies because we may have to return the original one
  // and the calling conventions dictate that the called function pops the
  // receiver. The second copy is pushed after the arguments, we saved in a6
  // since a0 will store the return value of callRuntime.
  __ mov(a6, a0);

  // Set up pointer to last argument.
  __ Add_d(
      t2, fp,
      Operand(StandardFrameConstants::kCallerSPOffset + kSystemPointerSize));

  // ----------- S t a t e -------------
  //  --                 r3: new target
  //  -- sp[0*kSystemPointerSize]: implicit receiver
  //  -- sp[1*kSystemPointerSize]: implicit receiver
  //  -- sp[2*kSystemPointerSize]: padding
  //  -- sp[3*kSystemPointerSize]: constructor function
  //  -- sp[4*kSystemPointerSize]: number of arguments
  //  -- sp[5*kSystemPointerSize]: context
  // -----------------------------------

  // Restore constructor function and argument count.
  __ Ld_d(a1, MemOperand(fp, ConstructFrameConstants::kConstructorOffset));
  __ Ld_d(a0, MemOperand(fp, ConstructFrameConstants::kLengthOffset));

  Label stack_overflow;
  __ StackOverflowCheck(a0, t0, t1, &stack_overflow);

  // TODO(victorgomes): When the arguments adaptor is completely removed, we
  // should get the formal parameter count and copy the arguments in its
  // correct position (including any undefined), instead of delaying this to
  // InvokeFunction.

  // Copy arguments and receiver to the expression stack.
  // t2: Pointer to start of argument.
  // a0: Number of arguments.
  Generate_PushArguments(masm, t2, a0, t0, t1, ArgumentsElementType::kRaw);
  // We need two copies because we may have to return the original one
  // and the calling conventions dictate that the called function pops the
  // receiver. The second copy is pushed after the arguments,
  __ Push(a6);

  // Call the function.
  __ InvokeFunctionWithNewTarget(a1, a3, a0, InvokeType::kCall);

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, do_throw, leave_and_return, check_receiver;

  // If the result is undefined, we jump out to using the implicit receiver.
  __ JumpIfNotRoot(a0, RootIndex::kUndefinedValue, &check_receiver);

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ Ld_d(a0, MemOperand(sp, 0 * kSystemPointerSize));
  __ JumpIfRoot(a0, RootIndex::kTheHoleValue, &do_throw);

  __ bind(&leave_and_return);
  // Restore arguments count from the frame.
  __ Ld_d(a1, MemOperand(fp, ConstructFrameConstants::kLengthOffset));
  // Leave construct frame.
  __ LeaveFrame(StackFrame::CONSTRUCT);

  // Remove caller arguments from the stack and return.
  __ DropArguments(a1, MacroAssembler::kCountIncludesReceiver);
  __ Ret();

  __ bind(&check_receiver);
  __ JumpIfSmi(a0, &use_receiver);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(a0, t2, &leave_and_return);
  __ Branch(&use_receiver);

  __ bind(&do_throw);
  // Restore the context from the frame.
  __ Ld_d(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  __ break_(0xCC);

  __ bind(&stack_overflow);
  // Restore the context from the frame.
  __ Ld_d(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ break_(0xCC);
}

void Builtins::Generate_JSBuiltinsConstructStub(MacroAssembler* masm) {
  Generate_JSBuiltinsConstructStubHelper(masm);
}

static void AssertCodeIsBaseline(MacroAssembler* masm, Register code,
                                 Register scratch) {
  DCHECK(!AreAliased(code, scratch));
  // Verify that the code kind is baseline code via the CodeKind.
  __ Ld_d(scratch, FieldMemOperand(code, Code::kFlagsOffset));
  __ DecodeField<Code::KindField>(scratch);
  __ Assert(eq, AbortReason::kExpectedBaselineData, scratch,
            Operand(static_cast<int>(CodeKind::BASELINE)));
}

// Equivalent of SharedFunctionInfo::GetData
static void GetSharedFunctionInfoData(MacroAssembler* masm, Register data,
                                      Register sfi, Register scratch) {
#ifdef V8_ENABLE_SANDBOX
  DCHECK(!AreAliased(data, scratch));
  DCHECK(!AreAliased(sfi, scratch));
  // Use trusted_function_data if non-empy, otherwise the regular function_data.
  Label use_tagged_field, done;
  __ Ld_wu(scratch, FieldMemOperand(
                        sfi, SharedFunctionInfo::kTrustedFunctionDataOffset));

  __ Branch(&use_tagged_field, eq, scratch, Operand(zero_reg));
  __ ResolveIndirectPointerHandle(data, scratch, kUnknownIndirectPointerTag);
  __ Branch(&done);

  __ bind(&use_tagged_field);
  __ LoadTaggedField(
      data, FieldMemOperand(sfi, SharedFunctionInfo::kFunctionDataOffset));
  __ bind(&done);
#else
  __ LoadTaggedField(
      data, FieldMemOperand(sfi, SharedFunctionInfo::kFunctionDataOffset));
#endif  // V8_ENABLE_SANDBOX
}

// TODO(v8:11429): Add a path for "not_compiled" and unify the two uses under
// the more general dispatch.
static void GetSharedFunctionInfoBytecodeOrBaseline(MacroAssembler* masm,
                                                    Register sfi,
                                                    Register bytecode,
                                                    Register scratch1,
                                                    Label* is_baseline) {
  DCHECK(!AreAliased(bytecode, scratch1));
  ASM_CODE_COMMENT(masm);
  Label done;

  Register data = bytecode;
  GetSharedFunctionInfoData(masm, data, sfi, scratch1);
  __ GetObjectType(data, scratch1, scratch1);

#ifndef V8_JITLESS
  if (v8_flags.debug_code) {
    Label not_baseline;
    __ Branch(&not_baseline, ne, scratch1, Operand(CODE_TYPE));
    AssertCodeIsBaseline(masm, data, scratch1);
    __ Branch(is_baseline);
    __ bind(&not_baseline);
  } else {
    __ Branch(is_baseline, eq, scratch1, Operand(CODE_TYPE));
  }
#endif  // !V8_JITLESS

  __ Branch(&done, ne, scratch1, Operand(INTERPRETER_DATA_TYPE));
  __ LoadProtectedPointerField(
      bytecode, FieldMemOperand(data, InterpreterData::kBytecodeArrayOffset));

  __ bind(&done);
}

// static
void Builtins::Generate_ResumeGeneratorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0 : the value to pass to the generator
  //  -- a1 : the JSGeneratorObject to resume
  //  -- ra : return address
  // -----------------------------------
  // Store input value into generator object.
  __ StoreTaggedField(
      a0, FieldMemOperand(a1, JSGeneratorObject::kInputOrDebugPosOffset));
  __ RecordWriteField(a1, JSGeneratorObject::kInputOrDebugPosOffset, a0,
                      kRAHasNotBeenSaved, SaveFPRegsMode::kIgnore);
  // Check that a1 is still valid, RecordWrite might have clobbered it.
  __ AssertGeneratorObject(a1);

  // Load suspended function and context.
  __ LoadTaggedField(a4,
                     FieldMemOperand(a1, JSGeneratorObject::kFunctionOffset));
  __ LoadTaggedField(cp, FieldMemOperand(a4, JSFunction::kContextOffset));

  // Flood function if we are stepping.
  Label prepare_step_in_if_stepping, prepare_step_in_suspended_generator;
  Label stepping_prepared;
  ExternalReference debug_hook =
      ExternalReference::debug_hook_on_function_call_address(masm->isolate());
  __ li(a5, debug_hook);
  __ Ld_b(a5, MemOperand(a5, 0));
  __ Branch(&prepare_step_in_if_stepping, ne, a5, Operand(zero_reg));

  // Flood function if we need to continue stepping in the suspended generator.
  ExternalReference debug_suspended_generator =
      ExternalReference::debug_suspended_generator_address(masm->isolate());
  __ li(a5, debug_suspended_generator);
  __ Ld_d(a5, MemOperand(a5, 0));
  __ Branch(&prepare_step_in_suspended_generator, eq, a1, Operand(a5));
  __ bind(&stepping_prepared);

  // Check the stack for overflow. We are not trying to catch interruptions
  // (i.e. debug break and preemption) here, so check the "real stack limit".
  Label stack_overflow;
  __ LoadStackLimit(kScratchReg,
                    MacroAssembler::StackLimitKind::kRealStackLimit);
  __ Branch(&stack_overflow, lo, sp, Operand(kScratchReg));

  // ----------- S t a t e -------------
  //  -- a1    : the JSGeneratorObject to resume
  //  -- a4    : generator function
  //  -- cp    : generator context
  //  -- ra    : return address
  // -----------------------------------

  // Push holes for arguments to generator function. Since the parser forced
  // context allocation for any variables in generators, the actual argument
  // values have already been copied into the context and these dummy values
  // will never be used.
  __ LoadTaggedField(
      a3, FieldMemOperand(a4, JSFunction::kSharedFunctionInfoOffset));
  __ Ld_hu(
      a3, FieldMemOperand(a3, SharedFunctionInfo::kFormalParameterCountOffset));
  __ Sub_d(a3, a3, Operand(kJSArgcReceiverSlots));
  __ LoadTaggedField(
      t1,
      FieldMemOperand(a1, JSGeneratorObject::kParametersAndRegistersOffset));
  {
    Label done_loop, loop;
    __ bind(&loop);
    __ Sub_d(a3, a3, Operand(1));
    __ Branch(&done_loop, lt, a3, Operand(zero_reg));
    __ Alsl_d(kScratchReg, a3, t1, kTaggedSizeLog2, t7);
    __ LoadTaggedField(kScratchReg,
                       FieldMemOperand(kScratchReg, FixedArray::kHeaderSize));
    __ Push(kScratchReg);
    __ Branch(&loop);
    __ bind(&done_loop);
    // Push receiver.
    __ LoadTaggedField(kScratchReg,
                       FieldMemOperand(a1, JSGeneratorObject::kReceiverOffset));
    __ Push(kScratchReg);
  }

  // Underlying function needs to have bytecode available.
  if (v8_flags.debug_code) {
    Label is_baseline;
    Register sfi = a3;
    Register bytecode = a3;
    __ LoadTaggedField(
        sfi, FieldMemOperand(a4, JSFunction::kSharedFunctionInfoOffset));
    GetSharedFunctionInfoBytecodeOrBaseline(masm, sfi, bytecode, t5,
                                            &is_baseline);
    __ GetObjectType(a3, a3, bytecode);
    __ Assert(eq, AbortReason::kMissingBytecodeArray, bytecode,
              Operand(BYTECODE_ARRAY_TYPE));
    __ bind(&is_baseline);
  }

  // Resume (Ignition/TurboFan) generator object.
  {
    __ LoadTaggedField(
        a0, FieldMemOperand(a4, JSFunction::kSharedFunctionInfoOffset));
    __ Ld_hu(a0, FieldMemOperand(
                     a0, SharedFunctionInfo::kFormalParameterCountOffset));
    // We abuse new.target both to indicate that this is a resume call and to
    // pass in the generator object.  In ordinary calls, new.target is always
    // undefined because generator functions are non-constructable.
    __ Move(a3, a1);
    __ Move(a1, a4);
    __ JumpJSFunction(a1);
  }

  __ bind(&prepare_step_in_if_stepping);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(a1, a4);
    // Push hole as receiver since we do not use it for stepping.
    __ PushRoot(RootIndex::kTheHoleValue);
    __ CallRuntime(Runtime::kDebugOnFunctionCall);
    __ Pop(a1);
  }
  __ LoadTaggedField(a4,
                     FieldMemOperand(a1, JSGeneratorObject::kFunctionOffset));
  __ Branch(&stepping_prepared);

  __ bind(&prepare_step_in_suspended_generator);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(a1);
    __ CallRuntime(Runtime::kDebugPrepareStepInSuspendedGenerator);
    __ Pop(a1);
  }
  __ LoadTaggedField(a4,
                     FieldMemOperand(a1, JSGeneratorObject::kFunctionOffset));
  __ Branch(&stepping_prepared);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ break_(0xCC);  // This should be unreachable.
  }
}

void Builtins::Generate_ConstructedNonConstructable(MacroAssembler* masm) {
  FrameScope scope(masm, StackFrame::INTERNAL);
  __ Push(a1);
  __ CallRuntime(Runtime::kThrowConstructedNonConstructable);
}

// Clobbers scratch1 and scratch2; preserves all other registers.
static void Generate_CheckStackOverflow(MacroAssembler* masm, Register argc,
                                        Register scratch1, Register scratch2) {
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  Label okay;
  __ LoadStackLimit(scratch1, MacroAssembler::StackLimitKind::kRealStackLimit);
  // Make a2 the space we have left. The stack might already be overflowed
  // here which will cause r2 to become negative.
  __ sub_d(scratch1, sp, scratch1);
  // Check if the arguments will overflow the stack.
  __ slli_d(scratch2, argc, kSystemPointerSizeLog2);
  __ Branch(&okay, gt, scratch1, Operand(scratch2));  // Signed comparison.

  // Out of stack space.
  __ CallRuntime(Runtime::kThrowStackOverflow);

  __ bind(&okay);
}

namespace {

// Called with the native C calling convention. The corresponding function
// signature is either:
//
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, Address new_target, Address target,
//       Address receiver, intptr_t argc, Address** args)>;
// or
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, MicrotaskQueue* microtask_queue)>;
void Generate_JSEntryVariant(MacroAssembler* masm, StackFrame::Type type,
                             Builtin entry_trampoline) {
  Label invoke, handler_entry, exit;

  {
    NoRootArrayScope no_root_array(masm);

    // Registers:
    //  either
    //   a0: root register value
    //   a1: entry address
    //   a2: function
    //   a3: receiver
    //   a4: argc
    //   a5: argv
    //  or
    //   a0: root register value
    //   a1: microtask_queue

    // Save callee saved registers on the stack.
    __ MultiPush(kCalleeSaved | ra);

    // Save callee-saved FPU registers.
    __ MultiPushFPU(kCalleeSavedFPU);
    // Set up the reserved register for 0.0.
    __ Move(kDoubleRegZero, 0.0);

    // Initialize the root register.
    // C calling convention. The first argument is passed in a0.
    __ mov(kRootRegister, a0);

#ifdef V8_COMPRESS_POINTERS
    // Initialize the pointer cage base register.
    __ LoadRootRelative(kPtrComprCageBaseRegister,
                        IsolateData::cage_base_offset());
#endif
  }

  // a1: entry address
  // a2: function
  // a3: receiver
  // a4: argc
  // a5: argv

  // We build an EntryFrame.
  __ li(s1, Operand(-1));  // Push a bad frame pointer to fail if it is used.
  __ li(s2, Operand(StackFrame::TypeToMarker(type)));
  __ li(s3, Operand(StackFrame::TypeToMarker(type)));
  ExternalReference c_entry_fp = ExternalReference::Create(
      IsolateAddressId::kCEntryFPAddress, masm->isolate());
  __ li(s5, c_entry_fp);
  __ Ld_d(s4, MemOperand(s5, 0));
  __ Push(s1, s2, s3, s4);

  // Clear c_entry_fp, now we've pushed its previous value to the stack.
  // If the c_entry_fp is not already zero and we don't clear it, the
  // StackFrameIteratorForProfiler will assume we are executing C++ and miss the
  // JS frames on top.
  __ St_d(zero_reg, MemOperand(s5, 0));

  __ li(s1, ExternalReference::fast_c_call_caller_fp_address(masm->isolate()));
  __ Ld_d(s2, MemOperand(s1, 0));
  __ St_d(zero_reg, MemOperand(s1, 0));
  __ li(s1, ExternalReference::fast_c_call_caller_pc_address(masm->isolate()));
  __ Ld_d(s3, MemOperand(s1, 0));
  __ St_d(zero_reg, MemOperand(s1, 0));
  __ Push(s2, s3);

  // Set up frame pointer for the frame to be pushed.
  __ addi_d(fp, sp, -EntryFrameConstants::kNextFastCallFramePCOffset);

  // Registers:
  //  either
  //   a1: entry address
  //   a2: function
  //   a3: receiver
  //   a4: argc
  //   a5: argv
  //  or
  //   a1: microtask_queue
  //
  // Stack:
  // fast api call pc   |
  // fast api call fp   |
  // C entry FP         |
  // function slot      | entry frame
  // context slot       |
  // bad fp (0xFF...F)  |
  // callee saved registers + ra

  // If this is the outermost JS call, set js_entry_sp value.
  Label non_outermost_js;
  ExternalReference js_entry_sp = ExternalReference::Create(
      IsolateAddressId::kJSEntrySPAddress, masm->isolate());
  __ li(s1, js_entry_sp);
  __ Ld_d(s2, MemOperand(s1, 0));
  __ Branch(&non_outermost_js, ne, s2, Operand(zero_reg));
  __ St_d(fp, MemOperand(s1, 0));
  __ li(s3, Operand(StackFrame::OUTERMOST_JSENTRY_FRAME));
  Label cont;
  __ b(&cont);
  __ nop();  // Branch delay slot nop.
  __ bind(&non_outermost_js);
  __ li(s3, Operand(StackFrame::INNER_JSENTRY_FRAME));
  __ bind(&cont);
  __ Push(s3);

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the exception.
  __ jmp(&invoke);
  __ bind(&handler_entry);

  // Store the current pc as the handler offset. It's used later to create the
  // handler table.
  masm->isolate()->builtins()->SetJSEntryHandlerOffset(handler_entry.pos());

  // Caught exception: Store result (exception) in the exception
  // field in the JSEnv and return a failure sentinel.  Coming in here the
  // fp will be invalid because the PushStackHandler below sets it to 0 to
  // signal the existence of the JSEntry frame.
  __ li(s1, ExternalReference::Create(IsolateAddressId::kExceptionAddress,
                                      masm->isolate()));
  __ St_d(a0,
          MemOperand(s1, 0));  // We come back from 'invoke'. result is in a0.
  __ LoadRoot(a0, RootIndex::kException);
  __ b(&exit);  // b exposes branch delay slot.
  __ nop();     // Branch delay slot nop.

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushStackHandler();
  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the bal(&invoke) above, which
  // restores all kCalleeSaved registers (including cp and fp) to their
  // saved values before returning a failure to C.
  //
  // Registers:
  //  either
  //   a0: root register value
  //   a1: entry address
  //   a2: function
  //   a3: receiver
  //   a4: argc
  //   a5: argv
  //  or
  //   a0: root register value
  //   a1: microtask_queue
  //
  // Stack:
  // handler frame
  // entry frame
  // fast api call pc
  // fast api call fp
  // C entry FP
  // function slot
  // context slot
  // bad fp (0xFF...F)
  // callee saved registers + ra

  // Invoke the function by calling through JS entry trampoline builtin and
  // pop the faked function when we return.
  __ CallBuiltin(entry_trampoline);

  // Unlink this frame from the handler chain.
  __ PopStackHandler();

  __ bind(&exit);  // a0 holds result
  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  __ Pop(a5);
  __ Branch(&non_outermost_js_2, ne, a5,
            Operand(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ li(a5, js_entry_sp);
  __ St_d(zero_reg, MemOperand(a5, 0));
  __ bind(&non_outermost_js_2);

  // Restore the top frame descriptors from the stack.
  __ Pop(a4, a5);
  __ li(a6, ExternalReference::fast_c_call_caller_fp_address(masm->isolate()));
  __ St_d(a4, MemOperand(a6, 0));
  __ li(a6, ExternalReference::fast_c_call_caller_pc_address(masm->isolate()));
  __ St_d(a5, MemOperand(a6, 0));

  __ Pop(a5);
  __ li(a4, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                      masm->isolate()));
  __ St_d(a5, MemOperand(a4, 0));

  // Reset the stack to the callee saved registers.
  __ addi_d(sp, sp, -EntryFrameConstants::kNextExitFrameFPOffset);

  // Restore callee-saved fpu registers.
  __ MultiPopFPU(kCalleeSavedFPU);

  // Restore callee saved registers from the stack.
  __ MultiPop(kCalleeSaved | ra);
  // Return.
  __ Jump(ra);
}

}  // namespace

void Builtins::Generate_JSEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY, Builtin::kJSEntryTrampoline);
}

void Builtins::Generate_JSConstructEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::CONSTRUCT_ENTRY,
                          Builtin::kJSConstructEntryTrampoline);
}

void Builtins::Generate_JSRunMicrotasksEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY,
                          Builtin::kRunMicrotasksTrampoline);
}

static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  // ----------- S t a t e -------------
  //  -- a1: new.target
  //  -- a2: function
  //  -- a3: receiver_pointer
  //  -- a4: argc
  //  -- a5: argv
  // -----------------------------------

  // Enter an internal frame.
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Setup the context (we need to use the caller context from the isolate).
    ExternalReference context_address = ExternalReference::Create(
        IsolateAddressId::kContextAddress, masm->isolate());
    __ li(cp, context_address);
    __ Ld_d(cp, MemOperand(cp, 0));

    // Push the function and the receiver onto the stack.
    __ Push(a2);

    // Check if we have enough stack space to push all arguments.
    __ mov(a6, a4);
    Generate_CheckStackOverflow(masm, a6, a0, s2);

    // Copy arguments to the stack.
    // a4: argc
    // a5: argv, i.e. points to first arg
    Generate_PushArguments(masm, a5, a4, s1, s2, ArgumentsElementType::kHandle);

    // Push the receive.
    __ Push(a3);

    // a0: argc
    // a1: function
    // a3: new.target
    __ mov(a3, a1);
    __ mov(a1, a2);
    __ mov(a0, a4);

    // Initialize all JavaScript callee-saved registers, since they will be seen
    // by the garbage collector as part of handlers.
    __ LoadRoot(a4, RootIndex::kUndefinedValue);
    __ mov(a5, a4);
    __ mov(s1, a4);
    __ mov(s2, a4);
    __ mov(s3, a4);
    __ mov(s4, a4);
    __ mov(s5, a4);
#ifndef V8_COMPRESS_POINTERS
    __ mov(s8, a4);
#endif
    // s6 holds the root address. Do not clobber.
    // s7 is cp. Do not init.
    // s8 is pointer cage base register (kPointerCageBaseRegister).

    // Invoke the code.
    Builtin builtin = is_construct ? Builtin::kConstruct : Builtins::Call();
    __ CallBuiltin(builtin);

    // Leave internal frame.
  }
  __ Jump(ra);
}

void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}

void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}

void Builtins::Generate_RunMicrotasksTrampoline(MacroAssembler* masm) {
  // a1: microtask_queue
  __ mov(RunMicrotasksDescriptor::MicrotaskQueueRegister(), a1);
  __ TailCallBuiltin(Builtin::kRunMicrotasks);
}

static void LeaveInterpreterFrame(MacroAssembler* masm, Register scratch1,
                                  Register scratch2) {
  Register params_size = scratch1;

  // Get the size of the formal parameters + receiver (in bytes).
  __ Ld_d(params_size,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Ld_hu(params_size,
           FieldMemOperand(params_size, BytecodeArray::kParameterSizeOffset));

  Register actual_params_size = scratch2;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ Ld_d(actual_params_size,
          MemOperand(fp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  __ slt(t2, params_size, actual_params_size);
  __ Movn(params_size, actual_params_size, t2);

  // Leave the frame (also dropping the register file).
  __ LeaveFrame(StackFrame::INTERPRETED);

  // Drop receiver + arguments.
  __ DropArguments(params_size, MacroAssembler::kCountIncludesReceiver);
}

// Advance the current bytecode offset. This simulates what all bytecode
// handlers do upon completion of the underlying operation. Will bail out to a
// label if the bytecode (without prefix) is a return bytecode. Will not advance
// the bytecode offset if the current bytecode is a JumpLoop, instead just
// re-executing the JumpLoop to jump to the correct bytecode.
static void AdvanceBytecodeOffsetOrReturn(MacroAssembler* masm,
                                          Register bytecode_array,
                                          Register bytecode_offset,
                                          Register bytecode, Register scratch1,
                                          Register scratch2, Register scratch3,
                                          Label* if_return) {
  Register bytecode_size_table = scratch1;

  // The bytecode offset value will be increased by one in wide and extra wide
  // cases. In the case of having a wide or extra wide JumpLoop bytecode, we
  // will restore the original bytecode. In order to simplify the code, we have
  // a backup of it.
  Register original_bytecode_offset = scratch3;
  DCHECK(!AreAliased(bytecode_array, bytecode_offset, bytecode,
                     bytecode_size_table, original_bytecode_offset));
  __ Move(original_bytecode_offset, bytecode_offset);
  __ li(bytecode_size_table, ExternalReference::bytecode_size_table_address());

  // Check if the bytecode is a Wide or ExtraWide prefix bytecode.
  Label process_bytecode, extra_wide;
  static_assert(0 == static_cast<int>(interpreter::Bytecode::kWide));
  static_assert(1 == static_cast<int>(interpreter::Bytecode::kExtraWide));
  static_assert(2 == static_cast<int>(interpreter::Bytecode::kDebugBreakWide));
  static_assert(3 ==
                static_cast<int>(interpreter::Bytecode::kDebugBreakExtraWide));
  __ Branch(&process_bytecode, hi, bytecode, Operand(3));
  __ And(scratch2, bytecode, Operand(1));
  __ Branch(&extra_wide, ne, scratch2, Operand(zero_reg));

  // Load the next bytecode and update table to the wide scaled table.
  __ Add_d(bytecode_offset, bytecode_offset, Operand(1));
  __ Add_d(scratch2, bytecode_array, bytecode_offset);
  __ Ld_bu(bytecode, MemOperand(scratch2, 0));
  __ Add_d(bytecode_size_table, bytecode_size_table,
           Operand(kByteSize * interpreter::Bytecodes::kBytecodeCount));
  __ jmp(&process_bytecode);

  __ bind(&extra_wide);
  // Load the next bytecode and update table to the extra wide scaled table.
  __ Add_d(bytecode_offset, bytecode_offset, Operand(1));
  __ Add_d(scratch2, bytecode_array, bytecode_offset);
  __ Ld_bu(bytecode, MemOperand(scratch2, 0));
  __ Add_d(bytecode_size_table, bytecode_size_table,
           Operand(2 * kByteSize * interpreter::Bytecodes::kBytecodeCount));

  __ bind(&process_bytecode);

// Bailout to the return label if this is a return bytecode.
#define JUMP_IF_EQUAL(NAME)          \
  __ Branch(if_return, eq, bytecode, \
            Operand(static_cast<int>(interpreter::Bytecode::k##NAME)));
  RETURN_BYTECODE_LIST(JUMP_IF_EQUAL)
#undef JUMP_IF_EQUAL

  // If this is a JumpLoop, re-execute it to perform the jump to the beginning
  // of the loop.
  Label end, not_jump_loop;
  __ Branch(&not_jump_loop, ne, bytecode,
            Operand(static_cast<int>(interpreter::Bytecode::kJumpLoop)));
  // We need to restore the original bytecode_offset since we might have
  // increased it to skip the wide / extra-wide prefix bytecode.
  __ Move(bytecode_offset, original_bytecode_offset);
  __ jmp(&end);

  __ bind(&not_jump_loop);
  // Otherwise, load the size of the current bytecode and advance the offset.
  __ Add_d(scratch2, bytecode_size_table, bytecode);
  __ Ld_b(scratch2, MemOperand(scratch2, 0));
  __ Add_d(bytecode_offset, bytecode_offset, scratch2);

  __ bind(&end);
}

namespace {

void ResetSharedFunctionInfoAge(MacroAssembler* masm, Register sfi) {
  __ St_h(zero_reg, FieldMemOperand(sfi, SharedFunctionInfo::kAgeOffset));
}

void ResetJSFunctionAge(MacroAssembler* masm, Register js_function,
                        Register scratch) {
  __ LoadTaggedField(
      scratch,
      FieldMemOperand(js_function, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, scratch);
}

void ResetFeedbackVectorOsrUrgency(MacroAssembler* masm,
                                   Register feedback_vector, Register scratch) {
  DCHECK(!AreAliased(feedback_vector, scratch));
  __ Ld_bu(scratch,
           FieldMemOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
  __ And(scratch, scratch, Operand(~FeedbackVector::OsrUrgencyBits::kMask));
  __ St_b(scratch,
          FieldMemOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
}

}  // namespace

// static
void Builtins::Generate_BaselineOutOfLinePrologue(MacroAssembler* masm) {
  UseScratchRegisterScope temps(masm);
  temps.Include({s1, s2, s3});
  temps.Exclude({t7});
  auto descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtin::kBaselineOutOfLinePrologue);
  Register closure = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kClosure);
  // Load the feedback cell and vector from the closure.
  Register feedback_cell = temps.Acquire();
  Register feedback_vector = temps.Acquire();
  __ LoadTaggedField(feedback_cell,
                     FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedField(
      feedback_vector,
      FieldMemOperand(feedback_cell, FeedbackCell::kValueOffset));
  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    __ AssertFeedbackVector(feedback_vector, scratch);
  }
  // Check for an tiering state.
  Label flags_need_processing;
  Register flags = no_reg;
  {
    UseScratchRegisterScope temps(masm);
    flags = temps.Acquire();
    // flags will be used only in |flags_need_processing|
    // and outside it can be reused.
    __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, feedback_vector, CodeKind::BASELINE, &flags_need_processing);
  }
  {
    UseScratchRegisterScope temps(masm);
    ResetFeedbackVectorOsrUrgency(masm, feedback_vector, temps.Acquire());
  }
  // Increment invocation count for the function.
  {
    UseScratchRegisterScope temps(masm);
    Register invocation_count = temps.Acquire();
    __ Ld_w(invocation_count,
            FieldMemOperand(feedback_vector,
                            FeedbackVector::kInvocationCountOffset));
    __ Add_w(invocation_count, invocation_count, Operand(1));
    __ St_w(invocation_count,
            FieldMemOperand(feedback_vector,
                            FeedbackVector::kInvocationCountOffset));
  }

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  {
    ASM_CODE_COMMENT_STRING(masm, "Frame Setup");
    // Normally the first thing we'd do here is Push(ra, fp), but we already
    // entered the frame in BaselineCompiler::Prologue, as we had to use the
    // value ra before the call to this BaselineOutOfLinePrologue builtin.
    Register callee_context = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kCalleeContext);
    Register callee_js_function = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kClosure);
    {
      UseScratchRegisterScope temps(masm);
      ResetJSFunctionAge(masm, callee_js_function, temps.Acquire());
    }
    __ Push(callee_context, callee_js_function);
    DCHECK_EQ(callee_js_function, kJavaScriptCallTargetRegister);
    DCHECK_EQ(callee_js_function, kJSFunctionRegister);

    Register argc = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kJavaScriptCallArgCount);
    // We'll use the bytecode for both code age/OSR resetting, and pushing onto
    // the frame, so load it into a register.
    Register bytecode_array = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kInterpreterBytecodeArray);
    __ Push(argc, bytecode_array, feedback_cell, feedback_vector);

    {
      UseScratchRegisterScope temps(masm);
      Register invocation_count = temps.Acquire();
      __ AssertFeedbackVector(feedback_vector, invocation_count);
    }
  }

  Label call_stack_guard;
  Register frame_size = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kStackFrameSize);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt check");
    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real stack
    // limit or tighter. By ensuring we have space until that limit after
    // building the frame we can quickly precheck both at once.
    UseScratchRegisterScope temps(masm);
    Register sp_minus_frame_size = temps.Acquire();
    __ Sub_d(sp_minus_frame_size, sp, frame_size);
    Register interrupt_limit = temps.Acquire();
    __ LoadStackLimit(interrupt_limit,
                      MacroAssembler::StackLimitKind::kInterruptStackLimit);
    __ Branch(&call_stack_guard, Uless, sp_minus_frame_size,
              Operand(interrupt_limit));
  }

  // Do "fast" return to the caller pc in ra.
  // TODO(v8:11429): Document this frame setup better.
  __ Ret();

  __ bind(&flags_need_processing);
  {
    ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
    UseScratchRegisterScope temps(masm);
    temps.Exclude(flags);
    // Ensure the flags is not allocated again.
    // Drop the frame created by the baseline call.
    __ Pop(ra, fp);
    __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, feedback_vector);
    __ Trap();
  }

  __ bind(&call_stack_guard);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
    FrameScope frame_scope(masm, StackFrame::INTERNAL);
    // Save incoming new target or generator
    __ Push(kJavaScriptCallNewTargetRegister);
    __ SmiTag(frame_size);
    __ Push(frame_size);
    __ CallRuntime(Runtime::kStackGuardWithGap);
    __ Pop(kJavaScriptCallNewTargetRegister);
  }
  __ Ret();
  temps.Exclude({s1, s2, s3});
}

// static
void Builtins::Generate_BaselineOutOfLinePrologueDeopt(MacroAssembler* masm) {
  // We're here because we got deopted during BaselineOutOfLinePrologue's stack
  // check. Undo all its frame creation and call into the interpreter instead.

  // Drop the feedback vector, the bytecode offset (was the feedback vector
  // but got replaced during deopt) and bytecode array.
  __ Drop(3);

  // Context, closure, argc.
  __ Pop(kContextRegister, kJavaScriptCallTargetRegister,
         kJavaScriptCallArgCountRegister);

  // Drop frame pointer
  __ LeaveFrame(StackFrame::BASELINE);

  // Enter the interpreter.
  __ TailCallBuiltin(Builtin::kInterpreterEntryTrampoline);
}

// Generate code for entering a JS function with the interpreter.
// On entry to the function the receiver and arguments have been pushed on the
// stack left to right.
//
// The live registers are:
//   o a0 : actual argument count
//   o a1: the JS function object being called.
//   o a3: the incoming new target or generator object
//   o cp: our context
//   o fp: the caller's frame pointer
//   o sp: stack pointer
//   o ra: return address
//
// The function builds an interpreter frame.  See InterpreterFrameConstants in
// frame-constants.h for its layout.
void Builtins::Generate_InterpreterEntryTrampoline(
    MacroAssembler* masm, InterpreterEntryTrampolineMode mode) {
  Register closure = a1;

  // Get the bytecode array from the function object and load it into
  // kInterpreterBytecodeArrayRegister.
  Register sfi = a4;
  __ LoadTaggedField(
      sfi, FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, sfi);

  Label is_baseline;
  GetSharedFunctionInfoBytecodeOrBaseline(
      masm, sfi, kInterpreterBytecodeArrayRegister, kScratchReg, &is_baseline);

  // The bytecode array could have been flushed from the shared function info,
  // if so, call into CompileLazy.
  Label compile_lazy;
  __ JumpIfObjectType(&compile_lazy, ne, kInterpreterBytecodeArrayRegister,
                      BYTECODE_ARRAY_TYPE, kScratchReg);

  Label push_stack_frame;
  Register feedback_vector = a2;
  __ LoadFeedbackVector(feedback_vector, closure, a4, &push_stack_frame);

#ifndef V8_JITLESS
  // If feedback vector is valid, check for optimized code and update invocation
  // count.

  // Check the tiering state.
  Label flags_need_processing;
  Register flags = a4;
  __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      flags, feedback_vector, CodeKind::INTERPRETED_FUNCTION,
      &flags_need_processing);

  ResetFeedbackVectorOsrUrgency(masm, feedback_vector, a4);

  // Increment invocation count for the function.
  __ Ld_w(a4, FieldMemOperand(feedback_vector,
                              FeedbackVector::kInvocationCountOffset));
  __ Add_w(a4, a4, Operand(1));
  __ St_w(a4, FieldMemOperand(feedback_vector,
                              FeedbackVector::kInvocationCountOffset));

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done below).
#else
  // Note: By omitting the above code in jitless mode we also disable:
  // - kFlagsLogNextExecution: only used for logging/profiling; and
  // - kInvocationCountOffset: only used for tiering heuristics and code
  //   coverage.
#endif  // !V8_JITLESS

  __ bind(&push_stack_frame);
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ PushStandardFrame(closure);

  // Load initial bytecode offset.
  __ li(kInterpreterBytecodeOffsetRegister,
        Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));

  // Push bytecode array, Smi tagged bytecode array offset and the feedback
  // vector.
  __ SmiTag(a4, kInterpreterBytecodeOffsetRegister);
  __ Push(kInterpreterBytecodeArrayRegister, a4, feedback_vector);

  // Allocate the local and temporary register file on the stack.
  Label stack_overflow;
  {
    // Load frame size (word) from the BytecodeArray object.
    __ Ld_w(a4, FieldMemOperand(kInterpreterBytecodeArrayRegister,
                                BytecodeArray::kFrameSizeOffset));

    // Do a stack check to ensure we don't go over the limit.
    __ Sub_d(a5, sp, Operand(a4));
    __ LoadStackLimit(a2, MacroAssembler::StackLimitKind::kRealStackLimit);
    __ Branch(&stack_overflow, lo, a5, Operand(a2));

    // If ok, push undefined as the initial value for all register file entries.
    Label loop_header;
    Label loop_check;
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ Branch(&loop_check);
    __ bind(&loop_header);
    // TODO(rmcilroy): Consider doing more than one push per loop iteration.
    __ Push(kInterpreterAccumulatorRegister);
    // Continue loop if not done.
    __ bind(&loop_check);
    __ Sub_d(a4, a4, Operand(kSystemPointerSize));
    __ Branch(&loop_header, ge, a4, Operand(zero_reg));
  }

  // If the bytecode array has a valid incoming new target or generator object
  // register, initialize it with incoming value which was passed in r3.
  Label no_incoming_new_target_or_generator_register;
  __ Ld_w(a5, FieldMemOperand(
                  kInterpreterBytecodeArrayRegister,
                  BytecodeArray::kIncomingNewTargetOrGeneratorRegisterOffset));
  __ Branch(&no_incoming_new_target_or_generator_register, eq, a5,
            Operand(zero_reg));
  __ Alsl_d(a5, a5, fp, kSystemPointerSizeLog2, t7);
  __ St_d(a3, MemOperand(a5, 0));
  __ bind(&no_incoming_new_target_or_generator_register);

  // Perform interrupt stack check.
  // TODO(solanes): Merge with the real stack limit check above.
  Label stack_check_interrupt, after_stack_check_interrupt;
  __ LoadStackLimit(a5, MacroAssembler::StackLimitKind::kInterruptStackLimit);
  __ Branch(&stack_check_interrupt, lo, sp, Operand(a5));
  __ bind(&after_stack_check_interrupt);

  // The accumulator is already loaded with undefined.

  // Load the dispatch table into a register and dispatch to the bytecode
  // handler at the current bytecode offset.
  Label do_dispatch;
  __ bind(&do_dispatch);
  __ li(kInterpreterDispatchTableRegister,
        ExternalReference::interpreter_dispatch_table_address(masm->isolate()));
  __ Add_d(t5, kInterpreterBytecodeArrayRegister,
           kInterpreterBytecodeOffsetRegister);
  __ Ld_bu(a7, MemOperand(t5, 0));
  __ Alsl_d(kScratchReg, a7, kInterpreterDispatchTableRegister,
            kSystemPointerSizeLog2, t7);
  __ Ld_d(kJavaScriptCallCodeStartRegister, MemOperand(kScratchReg, 0));
  __ Call(kJavaScriptCallCodeStartRegister);

  __ RecordComment("--- InterpreterEntryReturnPC point ---");
  if (mode == InterpreterEntryTrampolineMode::kDefault) {
    masm->isolate()->heap()->SetInterpreterEntryReturnPCOffset(
        masm->pc_offset());
  } else {
    DCHECK_EQ(mode, InterpreterEntryTrampolineMode::kForProfiling);
    // Both versions must be the same up to this point otherwise the builtins
    // will not be interchangable.
    CHECK_EQ(
        masm->isolate()->heap()->interpreter_entry_return_pc_offset().value(),
        masm->pc_offset());
  }

  // Any returns to the entry trampoline are either due to the return bytecode
  // or the interpreter tail calling a builtin and then a dispatch.

  // Get bytecode array and bytecode offset from the stack frame.
  __ Ld_d(kInterpreterBytecodeArrayRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Ld_d(kInterpreterBytecodeOffsetRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister);

  // Either return, or advance to the next bytecode and dispatch.
  Label do_return;
  __ Add_d(a1, kInterpreterBytecodeArrayRegister,
           kInterpreterBytecodeOffsetRegister);
  __ Ld_bu(a1, MemOperand(a1, 0));
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, a1, a2, a3,
                                a4, &do_return);
  __ jmp(&do_dispatch);

  __ bind(&do_return);
  // The return value is in a0.
  LeaveInterpreterFrame(masm, t0, t1);
  __ Jump(ra);

  __ bind(&stack_check_interrupt);
  // Modify the bytecode offset in the stack to be kFunctionEntryBytecodeOffset
  // for the call to the StackGuard.
  __ li(kInterpreterBytecodeOffsetRegister,
        Operand(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                             kFunctionEntryBytecodeOffset)));
  __ St_d(kInterpreterBytecodeOffsetRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ CallRuntime(Runtime::kStackGuard);

  // After the call, restore the bytecode array, bytecode offset and accumulator
  // registers again. Also, restore the bytecode offset in the stack to its
  // previous value.
  __ Ld_d(kInterpreterBytecodeArrayRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ li(kInterpreterBytecodeOffsetRegister,
        Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);

  __ SmiTag(a5, kInterpreterBytecodeOffsetRegister);
  __ St_d(a5, MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  __ jmp(&after_stack_check_interrupt);

#ifndef V8_JITLESS
  __ bind(&flags_need_processing);
  __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, feedback_vector);

  __ bind(&is_baseline);
  {
    // Load the feedback vector from the closure.
    __ LoadTaggedField(
        feedback_vector,
        FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
    __ LoadTaggedField(
        feedback_vector,
        FieldMemOperand(feedback_vector, FeedbackCell::kValueOffset));

    Label install_baseline_code;
    // Check if feedback vector is valid. If not, call prepare for baseline to
    // allocate it.
    __ LoadTaggedField(
        t0, FieldMemOperand(feedback_vector, HeapObject::kMapOffset));
    __ Ld_hu(t0, FieldMemOperand(t0, Map::kInstanceTypeOffset));
    __ Branch(&install_baseline_code, ne, t0, Operand(FEEDBACK_VECTOR_TYPE));

    // Check for an tiering state.
    __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, feedback_vector, CodeKind::BASELINE, &flags_need_processing);

    // Load the baseline code into the closure.
    __ Move(a2, kInterpreterBytecodeArrayRegister);
    static_assert(kJavaScriptCallCodeStartRegister == a2, "ABI mismatch");
    __ ReplaceClosureCodeWithOptimizedCode(a2, closure);
    __ JumpCodeObject(a2, kJSEntrypointTag);

    __ bind(&install_baseline_code);
    __ GenerateTailCallToReturnedCode(Runtime::kInstallBaselineCode);
  }
#endif  // !V8_JITLESS

  __ bind(&compile_lazy);
  __ GenerateTailCallToReturnedCode(Runtime::kCompileLazy);
  // Unreachable code.
  __ break_(0xCC);

  __ bind(&stack_overflow);
  __ CallRuntime(Runtime::kThrowStackOverflow);
  // Unreachable code.
  __ break_(0xCC);
}

static void GenerateInterpreterPushArgs(MacroAssembler* masm, Register num_args,
                                        Register start_address,
                                        Register scratch, Register scratch2) {
  // Find the address of the last argument.
  __ Sub_d(scratch, num_args, Operand(1));
  __ slli_d(scratch, scratch, kSystemPointerSizeLog2);
  __ Sub_d(start_address, start_address, scratch);

  // Push the arguments.
  __ PushArray(start_address, num_args, scratch, scratch2,
               MacroAssembler::PushArrayOrder::kReverse);
}

// static
void Builtins::Generate_InterpreterPushArgsThenCallImpl(
    MacroAssembler* masm, ConvertReceiverMode receiver_mode,
    InterpreterPushArgsMode mode) {
  DCHECK(mode != InterpreterPushArgsMode::kArrayFunction);
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a2 : the address of the first argument to be pushed. Subsequent
  //          arguments should be consecutive above this, in the same order as
  //          they are to be pushed onto the stack.
  //  -- a1 : the target to call (can be any Object).
  // -----------------------------------
  Label stack_overflow;
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // The spread argument should not be pushed.
    __ Sub_d(a0, a0, Operand(1));
  }

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    __ Sub_d(a3, a0, Operand(kJSArgcReceiverSlots));
  } else {
    __ mov(a3, a0);
  }

  __ StackOverflowCheck(a3, a4, t0, &stack_overflow);

  // This function modifies a2, t0 and a4.
  GenerateInterpreterPushArgs(masm, a3, a2, a4, t0);

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    __ PushRoot(RootIndex::kUndefinedValue);
  }

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Pass the spread in the register a2.
    // a2 already points to the penultime argument, the spread
    // is below that.
    __ Ld_d(a2, MemOperand(a2, -kSystemPointerSize));
  }

  // Call the target.
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ TailCallBuiltin(Builtin::kCallWithSpread);
  } else {
    __ TailCallBuiltin(Builtins::Call(receiver_mode));
  }

  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    // Unreachable code.
    __ break_(0xCC);
  }
}

// static
void Builtins::Generate_InterpreterPushArgsThenConstructImpl(
    MacroAssembler* masm, InterpreterPushArgsMode mode) {
  // ----------- S t a t e -------------
  // -- a0 : argument count
  // -- a3 : new target
  // -- a1 : constructor to call
  // -- a2 : allocation site feedback if available, undefined otherwise.
  // -- a4 : address of the first argument
  // -----------------------------------
  Label stack_overflow;
  __ StackOverflowCheck(a0, a5, t0, &stack_overflow);

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // The spread argument should not be pushed.
    __ Sub_d(a0, a0, Operand(1));
  }

  Register argc_without_receiver = a6;
  __ Sub_d(argc_without_receiver, a0, Operand(kJSArgcReceiverSlots));

  // Push the arguments, This function modifies t0, a4 and a5.
  GenerateInterpreterPushArgs(masm, argc_without_receiver, a4, a5, t0);

  // Push a slot for the receiver.
  __ Push(zero_reg);

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Pass the spread in the register a2.
    // a4 already points to the penultimate argument, the spread
    // lies in the next interpreter register.
    __ Ld_d(a2, MemOperand(a4, -kSystemPointerSize));
  } else {
    __ AssertUndefinedOrAllocationSite(a2, t0);
  }

  if (mode == InterpreterPushArgsMode::kArrayFunction) {
    __ AssertFunction(a1);

    // Tail call to the function-specific construct stub (still in the caller
    // context at this point).
    __ TailCallBuiltin(Builtin::kArrayConstructorImpl);
  } else if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Call the constructor with a0, a1, and a3 unmodified.
    __ TailCallBuiltin(Builtin::kConstructWithSpread);
  } else {
    DCHECK_EQ(InterpreterPushArgsMode::kOther, mode);
    // Call the constructor with a0, a1, and a3 unmodified.
    __ TailCallBuiltin(Builtin::kConstruct);
  }

  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    // Unreachable code.
    __ break_(0xCC);
  }
}

// static
void Builtins::Generate_ConstructForwardAllArgsImpl(
    MacroAssembler* masm, ForwardWhichFrame which_frame) {
  // ----------- S t a t e -------------
  // -- a3 : new target
  // -- a1 : constructor to call
  // -----------------------------------
  Label stack_overflow;

  // Load the frame pointer into a4.
  switch (which_frame) {
    case ForwardWhichFrame::kCurrentFrame:
      __ Move(a4, fp);
      break;
    case ForwardWhichFrame::kParentFrame:
      __ Ld_d(a4, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
      break;
  }

  // Load the argument count into a0.
  __ Ld_d(a0, MemOperand(a4, StandardFrameConstants::kArgCOffset));
  __ StackOverflowCheck(a0, a5, t0, &stack_overflow);

  // Point a4 to the base of the argument list to forward, excluding the
  // receiver.
  __ Add_d(a4, a4,
           Operand((StandardFrameConstants::kFixedSlotCountAboveFp + 1) *
                   kSystemPointerSize));

  // Copy arguments on the stack. a5 and t0 are scratch registers.
  Register argc_without_receiver = a6;
  __ Sub_d(argc_without_receiver, a0, Operand(kJSArgcReceiverSlots));
  __ PushArray(a4, argc_without_receiver, a5, t0);

  // Push a slot for the receiver.
  __ Push(zero_reg);

  // Call the constructor with a0, a1, and a3 unmodified.
  __ TailCallBuiltin(Builtin::kConstruct);

  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    __ break_(0xCC);
  }
}

namespace {

void NewImplicitReceiver(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  // -- a0 : the number of arguments
  // -- a1 : constructor to call (checked to be a JSFunction)
  // -- a3 : new target
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- [arguments without receiver]
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer
  // -----------------------------------
  Register implicit_receiver = a4;

  // Save live registers.
  __ SmiTag(a0);
  __ Push(a0, a1, a3);
  __ CallBuiltin(Builtin::kFastNewObject);
  // Save result.
  __ Move(implicit_receiver, a0);
  // Restore live registers.
  __ Pop(a0, a1, a3);
  __ SmiUntag(a0);

  // Patch implicit receiver (in arguments)
  __ StoreReceiver(implicit_receiver);
  // Patch second implicit (in construct frame)
  __ St_d(implicit_receiver,
          MemOperand(fp, FastConstructFrameConstants::kImplicitReceiverOffset));

  // Restore context.
  __ Ld_d(cp, MemOperand(fp, FastConstructFrameConstants::kContextOffset));
}

}  // namespace

// static
void Builtins::Generate_InterpreterPushArgsThenFastConstructFunction(
    MacroAssembler* masm) {
  // ----------- S t a t e -------------
  // -- a0 : argument count
  // -- a1 : constructor to call (checked to be a JSFunction)
  // -- a3 : new target
  // -- a4 : address of the first argument
  // -- cp : context pointer
  // -----------------------------------
  __ AssertFunction(a1);

  // Check if target has a [[Construct]] internal method.
  Label non_constructor;
  __ LoadMap(a2, a1);
  __ Ld_bu(a2, FieldMemOperand(a2, Map::kBitFieldOffset));
  __ And(a2, a2, Operand(Map::Bits1::IsConstructorBit::kMask));
  __ Branch(&non_constructor, eq, a2, Operand(zero_reg));

  // Add a stack check before pushing arguments.
  Label stack_overflow;
  __ StackOverflowCheck(a0, a2, a5, &stack_overflow);

  // Enter a construct frame.
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterFrame(StackFrame::FAST_CONSTRUCT);

  // Implicit receiver stored in the construct frame.
  __ LoadRoot(a2, RootIndex::kTheHoleValue);
  __ Push(cp, a2);

  // Push arguments + implicit receiver.
  Register argc_without_receiver = a7;
  __ Sub_d(argc_without_receiver, a0, Operand(kJSArgcReceiverSlots));
  GenerateInterpreterPushArgs(masm, argc_without_receiver, a4, a5, a6);
  __ Push(a2);

  // Check if it is a builtin call.
  Label builtin_call;
  __ LoadTaggedField(
      a2, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
  __ Ld_wu(a2, FieldMemOperand(a2, SharedFunctionInfo::kFlagsOffset));
  __ And(a5, a2, Operand(SharedFunctionInfo::ConstructAsBuiltinBit::kMask));
  __ Branch(&builtin_call, ne, a5, Operand(zero_reg));

  // Check if we need to create an implicit receiver.
  Label not_create_implicit_receiver;
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(a2);
  __ JumpIfIsInRange(
      a2, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver);
  NewImplicitReceiver(masm);
  __ bind(&not_create_implicit_receiver);

  // Call the function.
  __ InvokeFunctionWithNewTarget(a1, a3, a0, InvokeType::kCall);

  // ----------- S t a t e -------------
  //  -- a0     constructor result
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer
  // -----------------------------------

  // Store offset of return address for deoptimizer.
  masm->isolate()->heap()->SetConstructStubInvokeDeoptPCOffset(
      masm->pc_offset());

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, do_throw, leave_and_return, check_receiver;

  // If the result is undefined, we jump out to using the implicit receiver.
  __ JumpIfNotRoot(a0, RootIndex::kUndefinedValue, &check_receiver);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ Ld_d(a0,
          MemOperand(fp, FastConstructFrameConstants::kImplicitReceiverOffset));
  __ JumpIfRoot(a0, RootIndex::kTheHoleValue, &do_throw);

  __ bind(&leave_and_return);
  // Leave construct frame.
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ Ret();

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.
  __ bind(&check_receiver);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(a0, &use_receiver);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(a0, a4, &leave_and_return);
  __ Branch(&use_receiver);

  __ bind(&builtin_call);
  // TODO(victorgomes): Check the possibility to turn this into a tailcall.
  __ InvokeFunctionWithNewTarget(a1, a3, a0, InvokeType::kCall);
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ Ret();

  __ bind(&do_throw);
  // Restore the context from the frame.
  __ Ld_d(cp, MemOperand(fp, FastConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  // Unreachable code.
  __ break_(0xCC);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
  // Unreachable code.
  __ break_(0xCC);

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);
}

static void Generate_InterpreterEnterBytecode(MacroAssembler* masm) {
  // Set the return address to the correct point in the interpreter entry
  // trampoline.
  Label builtin_trampoline, trampoline_loaded;
  Tagged<Smi> interpreter_entry_return_pc_offset(
      masm->isolate()->heap()->interpreter_entry_return_pc_offset());
  DCHECK_NE(interpreter_entry_return_pc_offset, Smi::zero());

  // If the SFI function_data is an InterpreterData, the function will have a
  // custom copy of the interpreter entry trampoline for profiling. If so,
  // get the custom trampoline, otherwise grab the entry address of the global
  // trampoline.
  __ Ld_d(t0, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ LoadTaggedField(
      t0, FieldMemOperand(t0, JSFunction::kSharedFunctionInfoOffset));
  GetSharedFunctionInfoData(masm, t0, t0, t1);
  __ JumpIfObjectType(&builtin_trampoline, ne, t0, INTERPRETER_DATA_TYPE,
                      kInterpreterDispatchTableRegister);

  __ LoadProtectedPointerField(
      t0, FieldMemOperand(t0, InterpreterData::kInterpreterTrampolineOffset));
  __ LoadCodeInstructionStart(t0, t0, kJSEntrypointTag);
  __ Branch(&trampoline_loaded);

  __ bind(&builtin_trampoline);
  __ li(t0, ExternalReference::
                address_of_interpreter_entry_trampoline_instruction_start(
                    masm->isolate()));
  __ Ld_d(t0, MemOperand(t0, 0));

  __ bind(&trampoline_loaded);
  __ Add_d(ra, t0, Operand(interpreter_entry_return_pc_offset.value()));

  // Initialize the dispatch table register.
  __ li(kInterpreterDispatchTableRegister,
        ExternalReference::interpreter_dispatch_table_address(masm->isolate()));

  // Get the bytecode array pointer from the frame.
  __ Ld_d(kInterpreterBytecodeArrayRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));

  if (v8_flags.debug_code) {
    // Check function data field is actually a BytecodeArray object.
    __ SmiTst(kInterpreterBytecodeArrayRegister, kScratchReg);
    __ Assert(ne,
              AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry,
              kScratchReg, Operand(zero_reg));
    __ GetObjectType(kInterpreterBytecodeArrayRegister, a1, a1);
    __ Assert(eq,
              AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry,
              a1, Operand(BYTECODE_ARRAY_TYPE));
  }

  // Get the target bytecode offset from the frame.
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  if (v8_flags.debug_code) {
    Label okay;
    __ Branch(&okay, ge, kInterpreterBytecodeOffsetRegister,
              Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
    // Unreachable code.
    __ break_(0xCC);
    __ bind(&okay);
  }

  // Dispatch to the target bytecode.
  __ Add_d(a1, kInterpreterBytecodeArrayRegister,
           kInterpreterBytecodeOffsetRegister);
  __ Ld_bu(a7, MemOperand(a1, 0));
  __ Alsl_d(a1, a7, kInterpreterDispatchTableRegister, kSystemPointerSizeLog2,
            t7);
  __ Ld_d(kJavaScriptCallCodeStartRegister, MemOperand(a1, 0));
  __ Jump(kJavaScriptCallCodeStartRegister);
}

void Builtins::Generate_InterpreterEnterAtNextBytecode(MacroAssembler* masm) {
  // Advance the current bytecode offset stored within the given interpreter
  // stack frame. This simulates what all bytecode handlers do upon completion
  // of the underlying operation.
  __ Ld_d(kInterpreterBytecodeArrayRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Ld_d(kInterpreterBytecodeOffsetRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister);

  Label enter_bytecode, function_entry_bytecode;
  __ Branch(&function_entry_bytecode, eq, kInterpreterBytecodeOffsetRegister,
            Operand(BytecodeArray::kHeaderSize - kHeapObjectTag +
                    kFunctionEntryBytecodeOffset));

  // Load the current bytecode.
  __ Add_d(a1, kInterpreterBytecodeArrayRegister,
           kInterpreterBytecodeOffsetRegister);
  __ Ld_bu(a1, MemOperand(a1, 0));

  // Advance to the next bytecode.
  Label if_return;
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, a1, a2, a3,
                                a4, &if_return);

  __ bind(&enter_bytecode);
  // Convert new bytecode offset to a Smi and save in the stackframe.
  __ SmiTag(a2, kInterpreterBytecodeOffsetRegister);
  __ St_d(a2, MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  Generate_InterpreterEnterBytecode(masm);

  __ bind(&function_entry_bytecode);
  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset. Detect this case and advance to the first
  // actual bytecode.
  __ li(kInterpreterBytecodeOffsetRegister,
        Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ Branch(&enter_bytecode);

  // We should never take the if_return path.
  __ bind(&if_return);
  __ Abort(AbortReason::kInvalidBytecodeAdvance);
}

void Builtins::Generate_InterpreterEnterAtBytecode(MacroAssembler* masm) {
  Generate_InterpreterEnterBytecode(masm);
}

namespace {
void Generate_ContinueToBuiltinHelper(MacroAssembler* masm,
                                      bool java_script_builtin,
                                      bool with_result) {
  const RegisterConfiguration* config(RegisterConfiguration::Default());
  int allocatable_register_count = config->num_allocatable_general_registers();
  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  if (with_result) {
    if (java_script_builtin) {
      __ mov(scratch, a0);
    } else {
      // Overwrite the hole inserted by the deoptimizer with the return value
      // from the LAZY deopt point.
      __ St_d(a0,
              MemOperand(
                  sp, config->num_allocatable_general_registers() *
                              kSystemPointerSize +
                          BuiltinContinuationFrameConstants::kFixedFrameSize));
    }
  }
  for (int i = allocatable_register_count - 1; i >= 0; --i) {
    int code = config->GetAllocatableGeneralCode(i);
    __ Pop(Register::from_code(code));
    if (java_script_builtin && code == kJavaScriptCallArgCountRegister.code()) {
      __ SmiUntag(Register::from_code(code));
    }
  }

  if (with_result && java_script_builtin) {
    // Overwrite the hole inserted by the deoptimizer with the return value from
    // the LAZY deopt point. t0 contains the arguments count, the return value
    // from LAZY is always the last argument.
    constexpr int return_value_offset =
        BuiltinContinuationFrameConstants::kFixedSlotCount -
        kJSArgcReceiverSlots;
    __ Add_d(a0, a0, Operand(return_value_offset));
    __ Alsl_d(t0, a0, sp, kSystemPointerSizeLog2, t7);
    __ St_d(scratch, MemOperand(t0, 0));
    // Recover arguments count.
    __ Sub_d(a0, a0, Operand(return_value_offset));
  }

  __ Ld_d(
      fp,
      MemOperand(sp, BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp));
  // Load builtin index (stored as a Smi) and use it to get the builtin start
  // address from the builtins table.
  __ Pop(t0);
  __ Add_d(sp, sp,
           Operand(BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp));
  __ Pop(ra);
  __ LoadEntryFromBuiltinIndex(t0, t0);
  __ Jump(t0);
}
}  // namespace

void Builtins::Generate_ContinueToCodeStubBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, false);
}

void Builtins::Generate_ContinueToCodeStubBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, true);
}

void Builtins::Generate_ContinueToJavaScriptBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, false);
}

void Builtins::Generate_ContinueToJavaScriptBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, true);
}

void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kNotifyDeoptimized);
  }

  DCHECK_EQ(kInterpreterAccumulatorRegister.code(), a0.code());
  __ Ld_d(a0, MemOperand(sp, 0 * kSystemPointerSize));
  __ Add_d(sp, sp, Operand(1 * kSystemPointerSize));  // Remove state.
  __ Ret();
}

namespace {

void Generate_OSREntry(MacroAssembler* masm, Register entry_address,
                       Operand offset = Operand(zero_reg)) {
  __ Add_d(ra, entry_address, offset);
  // And "return" to the OSR entry point of the function.
  __ Ret();
}

enum class OsrSourceTier {
  kInterpreter,
  kBaseline,
};

void OnStackReplacement(MacroAssembler* masm, OsrSourceTier source,
                        Register maybe_target_code) {
  Label jump_to_optimized_code;
  {
    // If maybe_target_code is not null, no need to call into runtime. A
    // precondition here is: if maybe_target_code is a InstructionStream object,
    // it must NOT be marked_for_deoptimization (callers must ensure this).
    __ CompareTaggedAndBranch(&jump_to_optimized_code, ne, maybe_target_code,
                              Operand(Smi::zero()));
  }

  ASM_CODE_COMMENT(masm);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kCompileOptimizedOSR);
  }

  // If the code object is null, just return to the caller.
  __ CompareTaggedAndBranch(&jump_to_optimized_code, ne, maybe_target_code,
                            Operand(Smi::zero()));
  __ Ret();

  __ bind(&jump_to_optimized_code);
  DCHECK_EQ(maybe_target_code, a0);  // Already in the right spot.

  // OSR entry tracing.
  {
    Label next;
    __ li(a1, ExternalReference::address_of_log_or_trace_osr());
    __ Ld_bu(a1, MemOperand(a1, 0));
    __ Branch(&next, eq, a1, Operand(zero_reg));

    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ Push(a0);  // Preserve the code object.
      __ CallRuntime(Runtime::kLogOrTraceOptimizedOSREntry, 0);
      __ Pop(a0);
    }

    __ bind(&next);
  }

  if (source == OsrSourceTier::kInterpreter) {
    // Drop the handler frame that is be sitting on top of the actual
    // JavaScript frame. This is the case then OSR is triggered from bytecode.
    __ LeaveFrame(StackFrame::STUB);
  }

  // Load deoptimization data from the code object.
  // <deopt_data> = <code>[#deoptimization_data_offset]
  __ LoadProtectedPointerField(
      a1, MemOperand(maybe_target_code,
                     Code::kDeoptimizationDataOrInterpreterDataOffset -
                         kHeapObjectTag));

  // Load the OSR entrypoint offset from the deoptimization data.
  // <osr_offset> = <deopt_data>[#header_size + #osr_pc_offset]
  __ SmiUntagField(a1,
                   MemOperand(a1, TrustedFixedArray::OffsetOfElementAt(
                                      DeoptimizationData::kOsrPcOffsetIndex) -
                                      kHeapObjectTag));

  __ LoadCodeInstructionStart(maybe_target_code, maybe_target_code,
                              kJSEntrypointTag);

  // Compute the target address = code_entry + osr_offset
  // <entry_addr> = <code_entry> + <osr_offset>
  Generate_OSREntry(masm, maybe_target_code, Operand(a1));
}
}  // namespace

void Builtins::Generate_InterpreterOnStackReplacement(MacroAssembler* masm) {
  using D = OnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 1);
  OnStackReplacement(masm, OsrSourceTier::kInterpreter,
                     D::MaybeTargetCodeRegister());
}

void Builtins::Generate_BaselineOnStackReplacement(MacroAssembler* masm) {
  using D = OnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 1);

  __ Ld_d(kContextRegister,
          MemOperand(fp, BaselineFrameConstants::kContextOffset));
  OnStackReplacement(masm, OsrSourceTier::kBaseline,
                     D::MaybeTargetCodeRegister());
}

// static
void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0    : argc
  //  -- sp[0] : receiver
  //  -- sp[4] : thisArg
  //  -- sp[8] : argArray
  // -----------------------------------

  Register argc = a0;
  Register arg_array = a2;
  Register receiver = a1;
  Register this_arg = a5;
  Register undefined_value = a3;
  Register scratch = a4;

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load receiver into a1, argArray into a2 (if present), remove all
  // arguments from the stack (including the receiver), and push thisArg (if
  // present) instead.
  {
    __ Sub_d(scratch, argc, JSParameterCount(0));
    __ Ld_d(this_arg, MemOperand(sp, kSystemPointerSize));
    __ Ld_d(arg_array, MemOperand(sp, 2 * kSystemPointerSize));
    __ Movz(arg_array, undefined_value, scratch);  // if argc == 0
    __ Movz(this_arg, undefined_value, scratch);   // if argc == 0
    __ Sub_d(scratch, scratch, Operand(1));
    __ Movz(arg_array, undefined_value, scratch);  // if argc == 1
    __ Ld_d(receiver, MemOperand(sp, 0));
    __ DropArgumentsAndPushNewReceiver(argc, this_arg,
                                       MacroAssembler::kCountIncludesReceiver);
  }

  // ----------- S t a t e -------------
  //  -- a2    : argArray
  //  -- a1    : receiver
  //  -- a3    : undefined root value
  //  -- sp[0] : thisArg
  // -----------------------------------

  // 2. We don't need to check explicitly for callable receiver here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Tail call with no arguments if argArray is null or undefined.
  Label no_arguments;
  __ LoadRoot(scratch, RootIndex::kNullValue);
  __ CompareTaggedAndBranch(&no_arguments, eq, arg_array, Operand(scratch));
  __ CompareTaggedAndBranch(&no_arguments, eq, arg_array,
                            Operand(undefined_value));

  // 4a. Apply the receiver to the given argArray.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);

  // 4b. The argArray is either null or undefined, so we tail call without any
  // arguments to the receiver.
  __ bind(&no_arguments);
  {
    __ li(a0, JSParameterCount(0));
    DCHECK(receiver == a1);
    __ TailCallBuiltin(Builtins::Call());
  }
}

// static
void Builtins::Generate_FunctionPrototypeCall(MacroAssembler* masm) {
  // 1. Get the callable to call (passed as receiver) from the stack.
  { __ Pop(a1); }

  // 2. Make sure we have at least one argument.
  // a0: actual number of arguments
  {
    Label done;
    __ Branch(&done, ne, a0, Operand(JSParameterCount(0)));
    __ PushRoot(RootIndex::kUndefinedValue);
    __ Add_d(a0, a0, Operand(1));
    __ bind(&done);
  }

  // 3. Adjust the actual number of arguments.
  __ addi_d(a0, a0, -1);

  // 4. Call the callable.
  __ TailCallBuiltin(Builtins::Call());
}

void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0     : argc
  //  -- sp[0]  : receiver
  //  -- sp[8]  : target         (if argc >= 1)
  //  -- sp[16] : thisArgument   (if argc >= 2)
  //  -- sp[24] : argumentsList  (if argc == 3)
  // -----------------------------------

  Register argc = a0;
  Register arguments_list = a2;
  Register target = a1;
  Register this_argument = a5;
  Register undefined_value = a3;
  Register scratch = a4;

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load target into a1 (if present), argumentsList into a2 (if present),
  // remove all arguments from the stack (including the receiver), and push
  // thisArgument (if present) instead.
  {
    // Claim (3 - argc) dummy arguments form the stack, to put the stack in a
    // consistent state for a simple pop operation.

    __ Sub_d(scratch, argc, Operand(JSParameterCount(0)));
    __ Ld_d(target, MemOperand(sp, kSystemPointerSize));
    __ Ld_d(this_argument, MemOperand(sp, 2 * kSystemPointerSize));
    __ Ld_d(arguments_list, MemOperand(sp, 3 * kSystemPointerSize));
    __ Movz(arguments_list, undefined_value, scratch);  // if argc == 0
    __ Movz(this_argument, undefined_value, scratch);   // if argc == 0
    __ Movz(target, undefined_value, scratch);          // if argc == 0
    __ Sub_d(scratch, scratch, Operand(1));
    __ Movz(arguments_list, undefined_value, scratch);  // if argc == 1
    __ Movz(this_argument, undefined_value, scratch);   // if argc == 1
    __ Sub_d(scratch, scratch, Operand(1));
    __ Movz(arguments_list, undefined_value, scratch);  // if argc == 2

    __ DropArgumentsAndPushNewReceiver(argc, this_argument,
                                       MacroAssembler::kCountIncludesReceiver);
  }

  // ----------- S t a t e -------------
  //  -- a2    : argumentsList
  //  -- a1    : target
  //  -- a3    : undefined root value
  //  -- sp[0] : thisArgument
  // -----------------------------------

  // 2. We don't need to check explicitly for callable target here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Apply the target to the given argumentsList.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);
}

void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0     : argc
  //  -- sp[0]   : receiver
  //  -- sp[8]   : target
  //  -- sp[16]  : argumentsList
  //  -- sp[24]  : new.target (optional)
  // -----------------------------------

  Register argc = a0;
  Register arguments_list = a2;
  Register target = a1;
  Register new_target = a3;
  Register undefined_value = a4;
  Register scratch = a5;

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load target into a1 (if present), argumentsList into a2 (if present),
  // new.target into a3 (if present, otherwise use target), remove all
  // arguments from the stack (including the receiver), and push thisArgument
  // (if present) instead.
  {
    // Claim (3 - argc) dummy arguments form the stack, to put the stack in a
    // consistent state for a simple pop operation.

    __ Sub_d(scratch, argc, Operand(JSParameterCount(0)));
    __ Ld_d(target, MemOperand(sp, kSystemPointerSize));
    __ Ld_d(arguments_list, MemOperand(sp, 2 * kSystemPointerSize));
    __ Ld_d(new_target, MemOperand(sp, 3 * kSystemPointerSize));
    __ Movz(arguments_list, undefined_value, scratch);  // if argc == 0
    __ Movz(new_target, undefined_value, scratch);      // if argc == 0
    __ Movz(target, undefined_value, scratch);          // if argc == 0
    __ Sub_d(scratch, scratch, Operand(1));
    __ Movz(arguments_list, undefined_value, scratch);  // if argc == 1
    __ Movz(new_target, target, scratch);               // if argc == 1
    __ Sub_d(scratch, scratch, Operand(1));
    __ Movz(new_target, target, scratch);  // if argc == 2

    __ DropArgumentsAndPushNewReceiver(argc, undefined_value,
                                       MacroAssembler::kCountIncludesReceiver);
  }

  // ----------- S t a t e -------------
  //  -- a2    : argumentsList
  //  -- a1    : target
  //  -- a3    : new.target
  //  -- sp[0] : receiver (undefined)
  // -----------------------------------

  // 2. We don't need to check explicitly for constructor target here,
  // since that's the first thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 3. We don't need to check explicitly for constructor new.target here,
  // since that's the second thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 4. Construct the target with the given new.target and argumentsList.
  __ TailCallBuiltin(Builtin::kConstructWithArrayLike);
}

namespace {

// Allocate new stack space for |count| arguments and shift all existing
// arguments already on the stack. |pointer_to_new_space_out| points to the
// first free slot on the stack to copy additional arguments to and
// |argc_in_out| is updated to include |count|.
void Generate_AllocateSpaceAndShiftExistingArguments(
    MacroAssembler* masm, Register count, Register argc_in_out,
    Register pointer_to_new_space_out, Register scratch1, Register scratch2,
    Register scratch3) {
  DCHECK(!AreAliased(count, argc_in_out, pointer_to_new_space_out, scratch1,
                     scratch2));
  Register old_sp = scratch1;
  Register new_space = scratch2;
  __ mov(old_sp, sp);
  __ slli_d(new_space, count, kSystemPointerSizeLog2);
  __ Sub_d(sp, sp, Operand(new_space));

  Register end = scratch2;
  Register value = scratch3;
  Register dest = pointer_to_new_space_out;
  __ mov(dest, sp);
  __ Alsl_d(end, argc_in_out, old_sp, kSystemPointerSizeLog2);
  Label loop, done;
  __ Branch(&done, ge, old_sp, Operand(end));
  __ bind(&loop);
  __ Ld_d(value, MemOperand(old_sp, 0));
  __ St_d(value, MemOperand(dest, 0));
  __ Add_d(old_sp, old_sp, Operand(kSystemPointerSize));
  __ Add_d(dest, dest, Operand(kSystemPointerSize));
  __ Branch(&loop, lt, old_sp, Operand(end));
  __ bind(&done);

  // Update total number of arguments.
  __ Add_d(argc_in_out, argc_in_out, count);
}

}  // namespace

// static
void Builtins::Generate_CallOrConstructVarargs(MacroAssembler* masm,
                                               Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- a1 : target
  //  -- a0 : number of parameters on the stack
  //  -- a2 : arguments list (a FixedArray)
  //  -- a4 : len (number of elements to push from args)
  //  -- a3 : new.target (for [[Construct]])
  // -----------------------------------
  if (v8_flags.debug_code) {
    // Allow a2 to be a FixedArray, or a FixedDoubleArray if a4 == 0.
    Label ok, fail;
    __ AssertNotSmi(a2);
    __ GetObjectType(a2, t8, t8);
    __ Branch(&ok, eq, t8, Operand(FIXED_ARRAY_TYPE));
    __ Branch(&fail, ne, t8, Operand(FIXED_DOUBLE_ARRAY_TYPE));
    __ Branch(&ok, eq, a4, Operand(zero_reg));
    // Fall through.
    __ bind(&fail);
    __ Abort(AbortReason::kOperandIsNotAFixedArray);

    __ bind(&ok);
  }

  Register args = a2;
  Register len = a4;

  // Check for stack overflow.
  Label stack_overflow;
  __ StackOverflowCheck(len, kScratchReg, a5, &stack_overflow);

  // Move the arguments already in the stack,
  // including the receiver and the return address.
  // a4: Number of arguments to make room for.
  // a0: Number of arguments already on the stack.
  // a7: Points to first free slot on the stack after arguments were shifted.
  Generate_AllocateSpaceAndShiftExistingArguments(masm, a4, a0, a7, a6, t0, t1);

  // Push arguments onto the stack (thisArgument is already on the stack).
  {
    Label done, push, loop;
    Register src = a6;
    Register scratch = len;

    __ addi_d(src, args, FixedArray::kHeaderSize - kHeapObjectTag);
    __ Branch(&done, eq, len, Operand(zero_reg));
    __ slli_d(scratch, len, kSystemPointerSizeLog2);
    __ Sub_d(scratch, sp, Operand(scratch));
#if !V8_STATIC_ROOTS_BOOL
    // We do not use the Branch(reg, RootIndex) macro without static roots,
    // as it would do a LoadRoot behind the scenes and we want to avoid that
    // in a loop.
    __ LoadTaggedRoot(t1, RootIndex::kTheHoleValue);
#endif  // !V8_STATIC_ROOTS_BOOL
    __ bind(&loop);
    __ LoadTaggedField(a5, MemOperand(src, 0));
    __ addi_d(src, src, kTaggedSize);
#if V8_STATIC_ROOTS_BOOL
    __ Branch(&push, ne, a5, RootIndex::kTheHoleValue);
#else
    __ slli_w(t0, a5, 0);
    __ Branch(&push, ne, t0, Operand(t1));
#endif
    __ LoadRoot(a5, RootIndex::kUndefinedValue);
    __ bind(&push);
    __ St_d(a5, MemOperand(a7, 0));
    __ Add_d(a7, a7, Operand(kSystemPointerSize));
    __ Add_d(scratch, scratch, Operand(kSystemPointerSize));
    __ Branch(&loop, ne, scratch, Operand(sp));
    __ bind(&done);
  }

  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallOrConstructForwardVarargs(MacroAssembler* masm,
                                                      CallOrConstructMode mode,
                                                      Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a3 : the new.target (for [[Construct]] calls)
  //  -- a1 : the target to call (can be any Object)
  //  -- a2 : start index (to support rest parameters)
  // -----------------------------------

  // Check if new.target has a [[Construct]] internal method.
  if (mode == CallOrConstructMode::kConstruct) {
    Label new_target_constructor, new_target_not_constructor;
    __ JumpIfSmi(a3, &new_target_not_constructor);
    __ LoadTaggedField(t1, FieldMemOperand(a3, HeapObject::kMapOffset));
    __ Ld_bu(t1, FieldMemOperand(t1, Map::kBitFieldOffset));
    __ And(t1, t1, Operand(Map::Bits1::IsConstructorBit::kMask));
    __ Branch(&new_target_constructor, ne, t1, Operand(zero_reg));
    __ bind(&new_target_not_constructor);
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
      __ Push(a3);
      __ CallRuntime(Runtime::kThrowNotConstructor);
    }
    __ bind(&new_target_constructor);
  }

  Label stack_done, stack_overflow;
  __ Ld_d(a7, MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Sub_d(a7, a7, Operand(kJSArgcReceiverSlots));
  __ Sub_d(a7, a7, a2);
  __ Branch(&stack_done, le, a7, Operand(zero_reg));
  {
    // Check for stack overflow.
    __ StackOverflowCheck(a7, a4, a5, &stack_overflow);

    // Forward the arguments from the caller frame.

    // Point to the first argument to copy (skipping the receiver).
    __ Add_d(a6, fp,
             Operand(CommonFrameConstants::kFixedFrameSizeAboveFp +
                     kSystemPointerSize));
    __ Alsl_d(a6, a2, a6, kSystemPointerSizeLog2, t7);

    // Move the arguments already in the stack,
    // including the receiver and the return address.
    // a7: Number of arguments to make room for.
    // a0: Number of arguments already on the stack.
    // a2: Points to first free slot on the stack after arguments were shifted.
    Generate_AllocateSpaceAndShiftExistingArguments(masm, a7, a0, a2, t0, t1,
                                                    t2);

    // Copy arguments from the caller frame.
    // TODO(victorgomes): Consider using forward order as potentially more cache
    // friendly.
    {
      Label loop;
      __ bind(&loop);
      {
        __ Sub_w(a7, a7, Operand(1));
        __ Alsl_d(t0, a7, a6, kSystemPointerSizeLog2, t7);
        __ Ld_d(kScratchReg, MemOperand(t0, 0));
        __ Alsl_d(t0, a7, a2, kSystemPointerSizeLog2, t7);
        __ St_d(kScratchReg, MemOperand(t0, 0));
        __ Branch(&loop, ne, a7, Operand(zero_reg));
      }
    }
  }
  __ bind(&stack_done);
  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallFunction(MacroAssembler* masm,
                                     ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the function to call (checked to be a JSFunction)
  // -----------------------------------
  __ AssertFunction(a1);

  __ LoadTaggedField(
      a2, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));

  // Enter the context of the function; ToObject has to run in the function
  // context, and we also need to take the global proxy from the function
  // context in case of conversion.
  __ LoadTaggedField(cp, FieldMemOperand(a1, JSFunction::kContextOffset));
  // We need to convert the receiver for non-native sloppy mode functions.
  Label done_convert;
  __ Ld_wu(a3, FieldMemOperand(a2, SharedFunctionInfo::kFlagsOffset));
  __ And(kScratchReg, a3,
         Operand(SharedFunctionInfo::IsNativeBit::kMask |
                 SharedFunctionInfo::IsStrictBit::kMask));
  __ Branch(&done_convert, ne, kScratchReg, Operand(zero_reg));
  {
    // ----------- S t a t e -------------
    //  -- a0 : the number of arguments
    //  -- a1 : the function to call (checked to be a JSFunction)
    //  -- a2 : the shared function info.
    //  -- cp : the function context.
    // -----------------------------------

    if (mode == ConvertReceiverMode::kNullOrUndefined) {
      // Patch receiver to global proxy.
      __ LoadGlobalProxy(a3);
    } else {
      Label convert_to_object, convert_receiver;
      __ LoadReceiver(a3);
      __ JumpIfSmi(a3, &convert_to_object);
      __ JumpIfJSAnyIsNotPrimitive(a3, a4, &done_convert);
      if (mode != ConvertReceiverMode::kNotNullOrUndefined) {
        Label convert_global_proxy;
        __ JumpIfRoot(a3, RootIndex::kUndefinedValue, &convert_global_proxy);
        __ JumpIfNotRoot(a3, RootIndex::kNullValue, &convert_to_object);
        __ bind(&convert_global_proxy);
        {
          // Patch receiver to global proxy.
          __ LoadGlobalProxy(a3);
        }
        __ Branch(&convert_receiver);
      }
      __ bind(&convert_to_object);
      {
        // Convert receiver using ToObject.
        // TODO(bmeurer): Inline the allocation here to avoid building the frame
        // in the fast case? (fall back to AllocateInNewSpace?)
        FrameScope scope(masm, StackFrame::INTERNAL);
        __ SmiTag(a0);
        __ Push(a0, a1);
        __ mov(a0, a3);
        __ Push(cp);
        __ CallBuiltin(Builtin::kToObject);
        __ Pop(cp);
        __ mov(a3, a0);
        __ Pop(a0, a1);
        __ SmiUntag(a0);
      }
      __ LoadTaggedField(
          a2, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
      __ bind(&convert_receiver);
    }
    __ StoreReceiver(a3);
  }
  __ bind(&done_convert);

  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the function to call (checked to be a JSFunction)
  //  -- a2 : the shared function info.
  //  -- cp : the function context.
  // -----------------------------------

  __ Ld_hu(
      a2, FieldMemOperand(a2, SharedFunctionInfo::kFormalParameterCountOffset));
  __ InvokeFunctionCode(a1, no_reg, a2, a0, InvokeType::kJump);
}

// static
void Builtins::Generate_CallBoundFunctionImpl(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the function to call (checked to be a JSBoundFunction)
  // -----------------------------------
  __ AssertBoundFunction(a1);

  // Patch the receiver to [[BoundThis]].
  {
    __ LoadTaggedField(t0,
                       FieldMemOperand(a1, JSBoundFunction::kBoundThisOffset));
    __ StoreReceiver(t0);
  }

  // Load [[BoundArguments]] into a2 and length of that into a4.
  __ LoadTaggedField(
      a2, FieldMemOperand(a1, JSBoundFunction::kBoundArgumentsOffset));
  __ SmiUntagField(a4, FieldMemOperand(a2, FixedArray::kLengthOffset));

  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the function to call (checked to be a JSBoundFunction)
  //  -- a2 : the [[BoundArguments]] (implemented as FixedArray)
  //  -- a4 : the number of [[BoundArguments]]
  // -----------------------------------

  // Reserve stack space for the [[BoundArguments]].
  {
    Label done;
    __ slli_d(a5, a4, kSystemPointerSizeLog2);
    __ Sub_d(t0, sp, Operand(a5));
    // Check the stack for overflow. We are not trying to catch interruptions
    // (i.e. debug break and preemption) here, so check the "real stack limit".
    __ LoadStackLimit(kScratchReg,
                      MacroAssembler::StackLimitKind::kRealStackLimit);
    __ Branch(&done, hs, t0, Operand(kScratchReg));
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
      __ CallRuntime(Runtime::kThrowStackOverflow);
    }
    __ bind(&done);
  }

  // Pop receiver.
  __ Pop(t0);

  // Push [[BoundArguments]].
  {
    Label loop, done_loop;
    __ SmiUntagField(a4, FieldMemOperand(a2, FixedArray::kLengthOffset));
    __ Add_d(a0, a0, Operand(a4));
    __ Add_d(a2, a2, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
    __ bind(&loop);
    __ Sub_d(a4, a4, Operand(1));
    __ Branch(&done_loop, lt, a4, Operand(zero_reg));
    __ Alsl_d(a5, a4, a2, kTaggedSizeLog2, t7);
    __ LoadTaggedField(kScratchReg, MemOperand(a5, 0));
    __ Push(kScratchReg);
    __ Branch(&loop);
    __ bind(&done_loop);
  }

  // Push receiver.
  __ Push(t0);

  // Call the [[BoundTargetFunction]] via the Call builtin.
  __ LoadTaggedField(
      a1, FieldMemOperand(a1, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtins::Call());
}

// static
void Builtins::Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the target to call (can be any Object).
  // -----------------------------------

  Register target = a1;
  Register map = t1;
  Register instance_type = t2;
  Register scratch = t8;
  DCHECK(!AreAliased(a0, target, map, instance_type, scratch));

  Label non_callable, class_constructor;
  __ JumpIfSmi(target, &non_callable);
  __ LoadMap(map, target);
  __ GetInstanceTypeRange(map, instance_type, FIRST_CALLABLE_JS_FUNCTION_TYPE,
                          scratch);
  __ TailCallBuiltin(Builtins::CallFunction(mode), ls, scratch,
                     Operand(LAST_CALLABLE_JS_FUNCTION_TYPE -
                             FIRST_CALLABLE_JS_FUNCTION_TYPE));
  __ TailCallBuiltin(Builtin::kCallBoundFunction, eq, instance_type,
                     Operand(JS_BOUND_FUNCTION_TYPE));

  // Check if target has a [[Call]] internal method.
  {
    Register flags = t1;
    __ Ld_bu(flags, FieldMemOperand(map, Map::kBitFieldOffset));
    map = no_reg;
    __ And(flags, flags, Operand(Map::Bits1::IsCallableBit::kMask));
    __ Branch(&non_callable, eq, flags, Operand(zero_reg));
  }

  __ TailCallBuiltin(Builtin::kCallProxy, eq, instance_type,
                     Operand(JS_PROXY_TYPE));

  // Check if target is a wrapped function and call CallWrappedFunction external
  // builtin
  __ TailCallBuiltin(Builtin::kCallWrappedFunction, eq, instance_type,
                     Operand(JS_WRAPPED_FUNCTION_TYPE));

  // ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList)
  // Check that the function is not a "classConstructor".
  __ Branch(&class_constructor, eq, instance_type,
            Operand(JS_CLASS_CONSTRUCTOR_TYPE));

  // 2. Call to something else, which might have a [[Call]] internal method (if
  // not we raise an exception).
  // Overwrite the original receiver with the (original) target.
  __ StoreReceiver(target);
  // Let the "call_as_function_delegate" take care of the rest.
  __ LoadNativeContextSlot(target, Context::CALL_AS_FUNCTION_DELEGATE_INDEX);
  __ TailCallBuiltin(
      Builtins::CallFunction(ConvertReceiverMode::kNotNullOrUndefined));

  // 3. Call to something that is not callable.
  __ bind(&non_callable);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(target);
    __ CallRuntime(Runtime::kThrowCalledNonCallable);
  }

  // 4. The function is a "classConstructor", need to raise an exception.
  __ bind(&class_constructor);
  {
    FrameScope frame(masm, StackFrame::INTERNAL);
    __ Push(target);
    __ CallRuntime(Runtime::kThrowConstructorNonCallableError);
  }
}

void Builtins::Generate_ConstructFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the constructor to call (checked to be a JSFunction)
  //  -- a3 : the new target (checked to be a constructor)
  // -----------------------------------
  __ AssertConstructor(a1);
  __ AssertFunction(a1);

  // Calling convention for function specific ConstructStubs require
  // a2 to contain either an AllocationSite or undefined.
  __ LoadRoot(a2, RootIndex::kUndefinedValue);

  Label call_generic_stub;

  // Jump to JSBuiltinsConstructStub or JSConstructStubGeneric.
  __ LoadTaggedField(
      a4, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
  __ Ld_wu(a4, FieldMemOperand(a4, SharedFunctionInfo::kFlagsOffset));
  __ And(a4, a4, Operand(SharedFunctionInfo::ConstructAsBuiltinBit::kMask));
  __ Branch(&call_generic_stub, eq, a4, Operand(zero_reg));

  __ TailCallBuiltin(Builtin::kJSBuiltinsConstructStub);

  __ bind(&call_generic_stub);
  __ TailCallBuiltin(Builtin::kJSConstructStubGeneric);
}

// static
void Builtins::Generate_ConstructBoundFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the function to call (checked to be a JSBoundFunction)
  //  -- a3 : the new target (checked to be a constructor)
  // -----------------------------------
  __ AssertConstructor(a1);
  __ AssertBoundFunction(a1);

  // Load [[BoundArguments]] into a2 and length of that into a4.
  __ LoadTaggedField(
      a2, FieldMemOperand(a1, JSBoundFunction::kBoundArgumentsOffset));
  __ SmiUntagField(a4, FieldMemOperand(a2, FixedArray::kLengthOffset));

  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the function to call (checked to be a JSBoundFunction)
  //  -- a2 : the [[BoundArguments]] (implemented as FixedArray)
  //  -- a3 : the new target (checked to be a constructor)
  //  -- a4 : the number of [[BoundArguments]]
  // -----------------------------------

  // Reserve stack space for the [[BoundArguments]].
  {
    Label done;
    __ slli_d(a5, a4, kSystemPointerSizeLog2);
    __ Sub_d(t0, sp, Operand(a5));
    // Check the stack for overflow. We are not trying to catch interruptions
    // (i.e. debug break and preemption) here, so check the "real stack limit".
    __ LoadStackLimit(kScratchReg,
                      MacroAssembler::StackLimitKind::kRealStackLimit);
    __ Branch(&done, hs, t0, Operand(kScratchReg));
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
      __ CallRuntime(Runtime::kThrowStackOverflow);
    }
    __ bind(&done);
  }

  // Pop receiver.
  __ Pop(t0);

  // Push [[BoundArguments]].
  {
    Label loop, done_loop;
    __ SmiUntagField(a4, FieldMemOperand(a2, FixedArray::kLengthOffset));
    __ Add_d(a0, a0, Operand(a4));
    __ Add_d(a2, a2, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
    __ bind(&loop);
    __ Sub_d(a4, a4, Operand(1));
    __ Branch(&done_loop, lt, a4, Operand(zero_reg));
    __ Alsl_d(a5, a4, a2, kTaggedSizeLog2, t7);
    __ LoadTaggedField(kScratchReg, MemOperand(a5, 0));
    __ Push(kScratchReg);
    __ Branch(&loop);
    __ bind(&done_loop);
  }

  // Push receiver.
  __ Push(t0);

  // Patch new.target to [[BoundTargetFunction]] if new.target equals target.
  {
    Label skip_load;
    __ CompareTaggedAndBranch(&skip_load, ne, a1, Operand(a3));
    __ LoadTaggedField(
        a3, FieldMemOperand(a1, JSBoundFunction::kBoundTargetFunctionOffset));
    __ bind(&skip_load);
  }

  // Construct the [[BoundTargetFunction]] via the Construct builtin.
  __ LoadTaggedField(
      a1, FieldMemOperand(a1, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtin::kConstruct);
}

// static
void Builtins::Generate_Construct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- a0 : the number of arguments
  //  -- a1 : the constructor to call (can be any Object)
  //  -- a3 : the new target (either the same as the constructor or
  //          the JSFunction on which new was invoked initially)
  // -----------------------------------

  Register target = a1;
  Register map = t1;
  Register instance_type = t2;
  Register scratch = t8;
  DCHECK(!AreAliased(a0, target, map, instance_type, scratch));

  // Check if target is a Smi.
  Label non_constructor, non_proxy;
  __ JumpIfSmi(target, &non_constructor);

  // Check if target has a [[Construct]] internal method.
  __ LoadTaggedField(map, FieldMemOperand(target, HeapObject::kMapOffset));
  {
    Register flags = t3;
    __ Ld_bu(flags, FieldMemOperand(map, Map::kBitFieldOffset));
    __ And(flags, flags, Operand(Map::Bits1::IsConstructorBit::kMask));
    __ Branch(&non_constructor, eq, flags, Operand(zero_reg));
  }

  // Dispatch based on instance type.
  __ GetInstanceTypeRange(map, instance_type, FIRST_JS_FUNCTION_TYPE, scratch);
  __ TailCallBuiltin(Builtin::kConstructFunction, ls, scratch,
                     Operand(LAST_JS_FUNCTION_TYPE - FIRST_JS_FUNCTION_TYPE));

  // Only dispatch to bound functions after checking whether they are
  // constructors.
  __ TailCallBuiltin(Builtin::kConstructBoundFunction, eq, instance_type,
                     Operand(JS_BOUND_FUNCTION_TYPE));

  // Only dispatch to proxies after checking whether they are constructors.
  __ Branch(&non_proxy, ne, instance_type, Operand(JS_PROXY_TYPE));
  __ TailCallBuiltin(Builtin::kConstructProxy);

  // Called Construct on an exotic Object with a [[Construct]] internal method.
  __ bind(&non_proxy);
  {
    // Overwrite the original receiver with the (original) target.
    __ StoreReceiver(target);
    // Let the "call_as_constructor_delegate" take care of the rest.
    __ LoadNativeContextSlot(target,
                             Context::CALL_AS_CONSTRUCTOR_DELEGATE_INDEX);
    __ TailCallBuiltin(Builtins::CallFunction());
  }

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);
}

#if V8_ENABLE_WEBASSEMBLY
// Compute register lists for parameters to be saved. We save all parameter
// registers (see wasm-linkage.h). They might be overwritten in the runtime
// call below. We don't have any callee-saved registers in wasm, so no need to
// store anything else.
constexpr RegList kSavedGpRegs = ([]() constexpr {
  RegList saved_gp_regs;
  for (Register gp_param_reg : wasm::kGpParamRegisters) {
    saved_gp_regs.set(gp_param_reg);
  }

  // The instance has already been stored in the fixed part of the frame.
  saved_gp_regs.clear(kWasmInstanceRegister);
  // All set registers were unique.
  CHECK_EQ(saved_gp_regs.Count(), arraysize(wasm::kGpParamRegisters) - 1);
  CHECK_EQ(WasmLiftoffSetupFrameConstants::kNumberOfSavedGpParamRegs,
           saved_gp_regs.Count());
  return saved_gp_regs;
})();

constexpr DoubleRegList kSavedFpRegs = ([]() constexpr {
  DoubleRegList saved_fp_regs;
  for (DoubleRegister fp_param_reg : wasm::kFpParamRegisters) {
    saved_fp_regs.set(fp_param_reg);
  }

  CHECK_EQ(saved_fp_regs.Count(), arraysize(wasm::kFpParamRegisters));
  CHECK_EQ(WasmLiftoffSetupFrameConstants::kNumberOfSavedFpParamRegs,
           saved_fp_regs.Count());
  return saved_fp_regs;
})();

// When entering this builtin, we have just created a Wasm stack frame:
//
// [   Wasm instance   ]  <-- sp
// [ WASM frame marker ]
// [     saved fp      ]  <-- fp
//
// Add the feedback vector to the stack.
//
// [  feedback vector  ]  <-- sp
// [   Wasm instance   ]
// [ WASM frame marker ]
// [     saved fp      ]  <-- fp
void Builtins::Generate_WasmLiftoffFrameSetup(MacroAssembler* masm) {
  Register func_index = wasm::kLiftoffFrameSetupFunctionReg;
  Register vector = t1;
  Register scratch = t2;
  Label allocate_vector, done;

  __ LoadTaggedField(
      vector, FieldMemOperand(kWasmInstanceRegister,
                              WasmTrustedInstanceData::kFeedbackVectorsOffset));
  __ Alsl_d(vector, func_index, vector, kTaggedSizeLog2);
  __ LoadTaggedField(vector, FieldMemOperand(vector, FixedArray::kHeaderSize));
  __ JumpIfSmi(vector, &allocate_vector);
  __ bind(&done);
  __ Push(vector);
  __ Ret();

  __ bind(&allocate_vector);
  // Feedback vector doesn't exist yet. Call the runtime to allocate it.
  // We temporarily change the frame type for this, because we need special
  // handling by the stack walker in case of GC.
  __ li(scratch, StackFrame::TypeToMarker(StackFrame::WASM_LIFTOFF_SETUP));
  __ St_d(scratch, MemOperand(fp, TypedFrameConstants::kFrameTypeOffset));

  // Save registers.
  __ MultiPush(kSavedGpRegs);
  __ MultiPushFPU(kSavedFpRegs);
  __ Push(ra);

  // Arguments to the runtime function: instance, func_index, and an
  // additional stack slot for the NativeModule.
  __ SmiTag(func_index);
  __ Push(kWasmInstanceRegister, func_index, zero_reg);
  __ Move(cp, Smi::zero());
  __ CallRuntime(Runtime::kWasmAllocateFeedbackVector, 3);
  __ mov(vector, kReturnRegister0);

  // Restore registers and frame type.
  __ Pop(ra);
  __ MultiPopFPU(kSavedFpRegs);
  __ MultiPop(kSavedGpRegs);
  __ Ld_d(kWasmInstanceRegister,
          MemOperand(fp, WasmFrameConstants::kWasmInstanceOffset));
  __ li(scratch, StackFrame::TypeToMarker(StackFrame::WASM));
  __ St_d(scratch, MemOperand(fp, TypedFrameConstants::kFrameTypeOffset));
  __ Branch(&done);
}

void Builtins::Generate_WasmCompileLazy(MacroAssembler* masm) {
  // The function index was put in t0 by the jump table trampoline.
  // Convert to Smi for the runtime call
  __ SmiTag(kWasmCompileLazyFuncIndexRegister);

  {
    HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Save registers that we need to keep alive across the runtime call.
    __ Push(kWasmInstanceRegister);
    __ MultiPush(kSavedGpRegs);
    __ MultiPushFPU(kSavedFpRegs);

    // kFixedFrameSizeFromFp is hard coded to include space for Simd
    // registers, so we still need to allocate extra (unused) space on the stack
    // as if they were saved.
    __ Sub_d(sp, sp, kSavedFpRegs.Count() * kDoubleSize);

    __ Push(kWasmInstanceRegister, kWasmCompileLazyFuncIndexRegister);

    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kWasmCompileLazy, 2);

    // Untag the returned Smi into into t7, for later use.
    static_assert(!kSavedGpRegs.has(t7));
    __ SmiUntag(t7, a0);

    __ Add_d(sp, sp, kSavedFpRegs.Count() * kDoubleSize);
    // Restore registers.
    __ MultiPopFPU(kSavedFpRegs);
    __ MultiPop(kSavedGpRegs);
    __ Pop(kWasmInstanceRegister);
  }

  // The runtime function returned the jump table slot offset as a Smi (now in
  // t7). Use that to compute the jump target.
  static_assert(!kSavedGpRegs.has(t8));
  __ Ld_d(t8, FieldMemOperand(kWasmInstanceRegister,
                              WasmTrustedInstanceData::kJumpTableStartOffset));
  __ Add_d(t7, t8, Operand(t7));

  // Finally, jump to the jump table slot for the function.
  __ Jump(t7);
}

void Builtins::Generate_WasmDebugBreak(MacroAssembler* masm) {
  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  {
    FrameScope scope(masm, StackFrame::WASM_DEBUG_BREAK);

    // Save all parameter registers. They might hold live values, we restore
    // them after the runtime call.
    __ MultiPush(WasmDebugBreakFrameConstants::kPushedGpRegs);
    __ MultiPushFPU(WasmDebugBreakFrameConstants::kPushedFpRegs);

    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(cp, Smi::zero());
    __ CallRuntime(Runtime::kWasmDebugBreak, 0);

    // Restore registers.
    __ MultiPopFPU(WasmDebugBreakFrameConstants::kPushedFpRegs);
    __ MultiPop(WasmDebugBreakFrameConstants::kPushedGpRegs);
  }
  __ Ret();
}

void Builtins::Generate_WasmReturnPromiseOnSuspendAsm(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmToJsWrapperAsm(MacroAssembler* masm) {
  // Push registers in reverse order so that they are on the stack like
  // in an array, with the first item being at the lowest address.
  constexpr int cnt_fp = arraysize(wasm::kFpParamRegisters);
  constexpr int cnt_gp = arraysize(wasm::kGpParamRegisters) - 1;
  int required_stack_space = cnt_fp * kDoubleSize + cnt_gp * kSystemPointerSize;
  __ Sub_d(sp, sp, Operand(required_stack_space));
  for (int i = cnt_fp - 1; i >= 0; i--) {
    __ Fst_d(wasm::kFpParamRegisters[i],
             MemOperand(sp, i * kDoubleSize + cnt_gp * kSystemPointerSize));
  }

  // Without wasm::kGpParamRegisters[0] here.
  for (int i = cnt_gp; i >= 1; i--) {
    __ St_d(wasm::kGpParamRegisters[i],
            MemOperand(sp, (i - 1) * kSystemPointerSize));
  }
  // Reserve fixed slots for the CSA wrapper.
  // Two slots for stack-switching (central stack pointer and secondary stack
  // limit), one for the signature.
  __ Push(zero_reg, zero_reg, zero_reg);
  __ TailCallBuiltin(Builtin::kWasmToJsWrapperCSA);
}

void Builtins::Generate_WasmTrapHandlerLandingPad(MacroAssembler* masm) {
  // This builtin gets called from the WebAssembly trap handler when an
  // out-of-bounds memory access happened or when a null reference gets
  // dereferenced. This builtin then fakes a call from the instruction that
  // triggered the signal to the runtime. This is done by setting a return
  // address and then jumping to a builtin which will call further to the
  // runtime.
  // As the return address we use the fault address + 1. Using the fault address
  // itself would cause problems with safepoints and source positions.
  //
  // The problem with safepoints is that a safepoint has to be registered at the
  // return address, and that at most one safepoint should be registered at a
  // location. However, there could already be a safepoint registered at the
  // fault address if the fault address is the return address of a call.
  //
  // The problem with source positions is that the stack trace code looks for
  // the source position of a call before the return address. The source
  // position of the faulty memory access, however, is recorded at the fault
  // address. Therefore the stack trace code would not find the source position
  // if we used the fault address as the return address.
  __ Add_d(ra, kWasmTrapHandlerFaultAddressRegister, 1);
  __ TailCallBuiltin(Builtin::kWasmTrapHandlerThrowTrap);
}

void Builtins::Generate_WasmSuspend(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmResume(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmReject(MacroAssembler* masm) {
  // TODO(v8:12191): Implement for this platform.
  __ Trap();
}

void Builtins::Generate_WasmOnStackReplace(MacroAssembler* masm) {
  // Only needed on x64.
  __ Trap();
}

void Builtins::Generate_JSToWasmWrapperAsm(MacroAssembler* masm) { __ Trap(); }

void Builtins::Generate_WasmToOnHeapWasmToJsTrampoline(MacroAssembler* masm) {
  // Load the code pointer from the WasmApiFunctionRef and tail-call there.
  Register api_function_ref = wasm::kGpParamRegisters[0];
  // Use t0 which is not in kGpParamRegisters.
  Register call_target = t0;
  UseScratchRegisterScope temps{masm};
  temps.Exclude(call_target);
#ifdef V8_ENABLE_SANDBOX
  __ LoadCodeEntrypointViaCodePointer(
      call_target,
      FieldMemOperand(api_function_ref, WasmApiFunctionRef::kCodeOffset),
      kWasmEntrypointTag);
#else
  Register code = call_target;
  __ LoadTaggedField(
      code, FieldMemOperand(api_function_ref, WasmApiFunctionRef::kCodeOffset));
  __ Ld_d(call_target, FieldMemOperand(code, Code::kInstructionStartOffset));
#endif
  __ Jump(call_target);
}

#endif  // V8_ENABLE_WEBASSEMBLY

void Builtins::Generate_CEntry(MacroAssembler* masm, int result_size,
                               ArgvMode argv_mode, bool builtin_exit_frame,
                               bool switch_to_central_stack) {
  // Called from JavaScript; parameters are on stack as if calling JS function
  // a0: number of arguments including receiver
  // a1: pointer to builtin function
  // fp: frame pointer    (restored after C call)
  // sp: stack pointer    (restored as callee's sp after C call)
  // cp: current context  (C callee-saved)
  //
  // If argv_mode == ArgvMode::kRegister:
  // a2: pointer to the first argument

  if (argv_mode == ArgvMode::kRegister) {
    // Move argv into the correct register.
    __ mov(s1, a2);
  } else {
    // Compute the argv pointer in a callee-saved register.
    __ Alsl_d(s1, a0, sp, kSystemPointerSizeLog2, t7);
    __ Sub_d(s1, s1, kSystemPointerSize);
  }

  // Enter the exit frame that transitions from JavaScript to C++.
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(
      0, builtin_exit_frame ? StackFrame::BUILTIN_EXIT : StackFrame::EXIT);

  // s0: number of arguments  including receiver (C callee-saved)
  // s1: pointer to first argument (C callee-saved)
  // s2: pointer to builtin function (C callee-saved)

  // Prepare arguments for C routine.
  // a0 = argc
  __ mov(s0, a0);
  __ mov(s2, a1);

  // We are calling compiled C/C++ code. a0 and a1 hold our two arguments. We
  // also need to reserve the 4 argument slots on the stack.

  __ AssertStackIsAligned();

  // a0 = argc, a1 = argv, a2 = isolate
  __ li(a2, ExternalReference::isolate_address(masm->isolate()));
  __ mov(a1, s1);

  __ StoreReturnAddressAndCall(s2);

  // Result returned in a0 or a1:a0 - do not destroy these registers!

  // Check result for exception sentinel.
  Label exception_returned;
  // The returned value may be a trusted object, living outside of the main
  // pointer compression cage, so we need to use full pointer comparison here.
  __ CompareRootAndBranch(a0, RootIndex::kException, eq, &exception_returned,
                          ComparisonMode::kFullPointer);

  // Check that there is no exception, otherwise we
  // should have returned the exception sentinel.
  if (v8_flags.debug_code) {
    Label okay;
    ExternalReference exception_address = ExternalReference::Create(
        IsolateAddressId::kExceptionAddress, masm->isolate());
    __ li(a2, exception_address);
    __ Ld_d(a2, MemOperand(a2, 0));
    // Cannot use check here as it attempts to generate call into runtime.
    __ Branch(&okay, eq, a2, RootIndex::kTheHoleValue);
    __ stop();
    __ bind(&okay);
  }

  // Exit C frame and return.
  // a0:a1: result
  // sp: stack pointer
  // fp: frame pointer
  Register argc = argv_mode == ArgvMode::kRegister
                      // We don't want to pop arguments so set argc to no_reg.
                      ? no_reg
                      // s0: still holds argc (callee-saved).
                      : s0;
  __ LeaveExitFrame(argc, EMIT_RETURN);

  // Handling of exception.
  __ bind(&exception_returned);

  ExternalReference pending_handler_context_address = ExternalReference::Create(
      IsolateAddressId::kPendingHandlerContextAddress, masm->isolate());
  ExternalReference pending_handler_entrypoint_address =
      ExternalReference::Create(
          IsolateAddressId::kPendingHandlerEntrypointAddress, masm->isolate());
  ExternalReference pending_handler_fp_address = ExternalReference::Create(
      IsolateAddressId::kPendingHandlerFPAddress, masm->isolate());
  ExternalReference pending_handler_sp_address = ExternalReference::Create(
      IsolateAddressId::kPendingHandlerSPAddress, masm->isolate());

  // Ask the runtime for help to determine the handler. This will set a0 to
  // contain the current exception, don't clobber it.
  ExternalReference find_handler =
      ExternalReference::Create(Runtime::kUnwindAndFindExceptionHandler);
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ PrepareCallCFunction(3, 0, a0);
    __ mov(a0, zero_reg);
    __ mov(a1, zero_reg);
    __ li(a2, ExternalReference::isolate_address(masm->isolate()));
    __ CallCFunction(find_handler, 3, SetIsolateDataSlots::kNo);
  }

  // Retrieve the handler context, SP and FP.
  __ li(cp, pending_handler_context_address);
  __ Ld_d(cp, MemOperand(cp, 0));
  __ li(sp, pending_handler_sp_address);
  __ Ld_d(sp, MemOperand(sp, 0));
  __ li(fp, pending_handler_fp_address);
  __ Ld_d(fp, MemOperand(fp, 0));

  // If the handler is a JS frame, restore the context to the frame. Note that
  // the context will be set to (cp == 0) for non-JS frames.
  Label zero;
  __ Branch(&zero, eq, cp, Operand(zero_reg));
  __ St_d(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ bind(&zero);

  // Clear c_entry_fp, like we do in `LeaveExitFrame`.
  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    __ li(scratch, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                             masm->isolate()));
    __ St_d(zero_reg, MemOperand(scratch, 0));
  }

  // Compute the handler entry address and jump to it.
  __ li(t7, pending_handler_entrypoint_address);
  __ Ld_d(t7, MemOperand(t7, 0));
  __ Jump(t7);
}

void Builtins::Generate_DoubleToI(MacroAssembler* masm) {
  Label done;
  Register result_reg = t0;

  Register scratch = GetRegisterThatIsNotOneOf(result_reg);
  Register scratch2 = GetRegisterThatIsNotOneOf(result_reg, scratch);
  Register scratch3 = GetRegisterThatIsNotOneOf(result_reg, scratch, scratch2);
  DoubleRegister double_scratch = kScratchDoubleReg;

  // Account for saved regs.
  const int kArgumentOffset = 4 * kSystemPointerSize;

  __ Push(result_reg);
  __ Push(scratch, scratch2, scratch3);

  // Load double input.
  __ Fld_d(double_scratch, MemOperand(sp, kArgumentOffset));

  // Try a conversion to a signed integer.
  __ TryInlineTruncateDoubleToI(result_reg, double_scratch, &done);

  // Load the double value and perform a manual truncation.
  Register input_high = scratch2;
  Register input_low = scratch3;

  // TryInlineTruncateDoubleToI destory kScratchDoubleReg, so reload it.
  __ Ld_d(result_reg, MemOperand(sp, kArgumentOffset));

  // Extract the biased exponent in result.
  __ bstrpick_d(input_high, result_reg,
                HeapNumber::kMantissaBits + HeapNumber::kExponentBits - 1,
                HeapNumber::kMantissaBits);

  __ Sub_d(scratch, input_high,
           HeapNumber::kExponentBias + HeapNumber::kMantissaBits + 32);
  Label not_zero;
  __ Branch(&not_zero, lt, scratch, Operand(zero_reg));
  __ mov(result_reg, zero_reg);
  __ Branch(&done);
  __ bind(&not_zero);

  // Isolate the mantissa bits, and set the implicit '1'.
  __ bstrpick_d(input_low, result_reg, HeapNumber::kMantissaBits - 1, 0);
  __ Or(input_low, input_low, Operand(1ULL << HeapNumber::kMantissaBits));

  Label lessthan_zero_reg;
  __ Branch(&lessthan_zero_reg, ge, result_reg, Operand(zero_reg));
  __ Sub_d(input_low, zero_reg, Operand(input_low));
  __ bind(&lessthan_zero_reg);

  // Shift the mantissa bits in the correct place. We know that we have to shift
  // it left here, because exponent >= 63 >= kMantissaBits.
  __ Sub_d(input_high, input_high,
           Operand(HeapNumber::kExponentBias + HeapNumber::kMantissaBits));
  __ sll_w(result_reg, input_low, input_high);

  __ bind(&done);

  __ St_d(result_reg, MemOperand(sp, kArgumentOffset));
  __ Pop(scratch, scratch2, scratch3);
  __ Pop(result_reg);
  __ Ret();
}

void Builtins::Generate_CallApiCallbackImpl(MacroAssembler* masm,
                                            CallApiCallbackMode mode) {
  // ----------- S t a t e -------------
  // CallApiCallbackMode::kOptimizedNoProfiling/kOptimized modes:
  //  -- a1                  : api function address
  // Both modes:
  //  -- a2                  : arguments count (not including the receiver)
  //  -- a3                  : FunctionTemplateInfo
  //  -- a0                  : holder
  //  -- cp                  : context
  //  -- sp[0]               : receiver
  //  -- sp[8]               : first argument
  //  -- ...
  //  -- sp[(argc) * 8]      : last argument
  // -----------------------------------

  Register function_callback_info_arg = kCArgRegs[0];

  Register api_function_address = no_reg;
  Register argc = no_reg;
  Register func_templ = no_reg;
  Register holder = no_reg;
  Register topmost_script_having_context = no_reg;
  Register scratch = t0;

  switch (mode) {
    case CallApiCallbackMode::kGeneric:
      argc = CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister();
      topmost_script_having_context = CallApiCallbackGenericDescriptor::
          TopmostScriptHavingContextRegister();
      func_templ =
          CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister();
      holder = CallApiCallbackGenericDescriptor::HolderRegister();
      break;

    case CallApiCallbackMode::kOptimizedNoProfiling:
    case CallApiCallbackMode::kOptimized:
      // Caller context is always equal to current context because we don't
      // inline Api calls cross-context.
      topmost_script_having_context = kContextRegister;
      api_function_address =
          CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister();
      argc = CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister();
      func_templ =
          CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister();
      holder = CallApiCallbackOptimizedDescriptor::HolderRegister();
      break;
  }
  DCHECK(!AreAliased(api_function_address, topmost_script_having_context, argc,
                     holder, scratch));

  using FCA = FunctionCallbackArguments;
  using ER = ExternalReference;

  static_assert(FCA::kArgsLength == 6);
  static_assert(FCA::kNewTargetIndex == 5);
  static_assert(FCA::kTargetIndex == 4);
  static_assert(FCA::kReturnValueIndex == 3);
  static_assert(FCA::kUnusedIndex == 2);
  static_assert(FCA::kIsolateIndex == 1);
  static_assert(FCA::kHolderIndex == 0);

  // Set up FunctionCallbackInfo's implicit_args on the stack as follows:
  //
  // Target state:
  //   sp[0 * kSystemPointerSize]: kHolder   <= FCA::implicit_args_
  //   sp[1 * kSystemPointerSize]: kIsolate
  //   sp[2 * kSystemPointerSize]: undefined (padding, unused)
  //   sp[3 * kSystemPointerSize]: undefined (kReturnValue)
  //   sp[4 * kSystemPointerSize]: kData
  //   sp[5 * kSystemPointerSize]: undefined (kNewTarget)
  // Existing state:
  //   sp[6 * kSystemPointerSize]:           <= FCA:::values_

  __ StoreRootRelative(IsolateData::topmost_script_having_context_offset(),
                       topmost_script_having_context);
  if (mode == CallApiCallbackMode::kGeneric) {
    api_function_address = ReassignRegister(topmost_script_having_context);
  }

  // Reserve space on the stack.
  __ Sub_d(sp, sp, Operand(FCA::kArgsLength * kSystemPointerSize));

  // kHolder.
  __ St_d(holder, MemOperand(sp, FCA::kHolderIndex * kSystemPointerSize));

  // kIsolate.
  __ li(scratch, ER::isolate_address(masm->isolate()));
  __ St_d(scratch, MemOperand(sp, FCA::kIsolateIndex * kSystemPointerSize));

  // kUnused.
  __ St_d(zero_reg, MemOperand(sp, FCA::kUnusedIndex * kSystemPointerSize));

  // kReturnValue.
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  __ St_d(scratch, MemOperand(sp, FCA::kReturnValueIndex * kSystemPointerSize));

  // kTarget.
  __ St_d(func_templ, MemOperand(sp, FCA::kTargetIndex * kSystemPointerSize));

  // kNewTarget.
  __ St_d(scratch, MemOperand(sp, FCA::kNewTargetIndex * kSystemPointerSize));

  // Keep a pointer to kHolder (= implicit_args) in a {holder} register.
  // We use it below to set up the FunctionCallbackInfo object.
  __ mov(holder, sp);

  // Allocate the v8::Arguments structure in the arguments' space since
  // it's not controlled by GC.
  static constexpr int kSlotsToDropOnStackSize = 1 * kSystemPointerSize;
  static constexpr int kApiStackSpace =
      (FCA::kSize + kSlotsToDropOnStackSize) / kSystemPointerSize;
  static_assert(kApiStackSpace == 4);
  static_assert(FCA::kImplicitArgsOffset == 0);
  static_assert(FCA::kValuesOffset == 1 * kSystemPointerSize);
  static_assert(FCA::kLengthOffset == 2 * kSystemPointerSize);
  const int exit_frame_params_count =
      mode == CallApiCallbackMode::kGeneric
          ? ApiCallbackExitFrameConstants::kAdditionalParametersCount
          : 0;

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  if (mode == CallApiCallbackMode::kGeneric) {
    ASM_CODE_COMMENT_STRING(masm, "Push API_CALLBACK_EXIT frame arguments");
    __ AllocateStackSpace(exit_frame_params_count * kSystemPointerSize);

    // No padding is required.
    static_assert(ApiCallbackExitFrameConstants::kOptionalPaddingSize == 0);

    // Context parameter.
    static_assert(ApiCallbackExitFrameConstants::kContextOffset ==
                  4 * kSystemPointerSize);
    __ St_d(kContextRegister, MemOperand(sp, 2 * kSystemPointerSize));

    // Argc parameter as a Smi.
    static_assert(ApiCallbackExitFrameConstants::kArgcOffset ==
                  3 * kSystemPointerSize);
    __ SmiTag(scratch, argc);
    __ St_d(scratch, MemOperand(sp, 1 * kSystemPointerSize));

    // Target parameter.
    static_assert(ApiCallbackExitFrameConstants::kTargetOffset ==
                  2 * kSystemPointerSize);
    __ St_d(func_templ, MemOperand(sp, 0 * kSystemPointerSize));

    __ LoadExternalPointerField(
        api_function_address,
        FieldMemOperand(func_templ,
                        FunctionTemplateInfo::kMaybeRedirectedCallbackOffset),
        kFunctionTemplateInfoCallbackTag);

    __ EnterExitFrame(kApiStackSpace, StackFrame::API_CALLBACK_EXIT);
  } else {
    __ EnterExitFrame(kApiStackSpace, StackFrame::EXIT);
  }

  {
    ASM_CODE_COMMENT_STRING(masm, "Initialize FunctionCallbackInfo");
    // FunctionCallbackInfo::implicit_args_ (points at kHolder as set up above).
    // Arguments are after the return address (pushed by EnterExitFrame()).
    __ St_d(holder, ExitFrameStackSlotOperand(FCA::kImplicitArgsOffset));

    // FunctionCallbackInfo::values_ (points at the first varargs argument
    // passed on the stack).
    __ Add_d(holder, holder,
             Operand(FCA::kArgsLengthWithReceiver * kSystemPointerSize));

    __ St_d(holder, ExitFrameStackSlotOperand(FCA::kValuesOffset));

    // FunctionCallbackInfo::length_.
    // Stored as int field, 32-bit integers within struct on stack always left
    // justified by n64 ABI.
    __ St_w(argc, ExitFrameStackSlotOperand(FCA::kLengthOffset));
  }

  // We also store the number of bytes to drop from the stack after returning
  // from the API function here.
  // Note: Unlike on other architectures, this stores the number of slots to
  // drop, not the number of bytes.
  MemOperand stack_space_operand =
      ExitFrameStackSlotOperand(FCA::kLengthOffset + kSlotsToDropOnStackSize);
  __ Add_d(scratch, argc,
           Operand(FCA::kArgsLengthWithReceiver + exit_frame_params_count));
  __ St_d(scratch, stack_space_operand);

  __ RecordComment("v8::FunctionCallback's argument.");
  // function_callback_info_arg = v8::FunctionCallbackInfo&
  __ Add_d(function_callback_info_arg, sp, Operand(1 * kSystemPointerSize));

  DCHECK(
      !AreAliased(api_function_address, scratch, function_callback_info_arg));

  ExternalReference thunk_ref = ER::invoke_function_callback(mode);
  Register no_thunk_arg = no_reg;

  MemOperand return_value_operand = ExitFrameCallerStackSlotOperand(
      FCA::kReturnValueIndex + exit_frame_params_count);

  static constexpr int kUseStackSpaceOperand = 0;

  const bool with_profiling =
      mode != CallApiCallbackMode::kOptimizedNoProfiling;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, no_thunk_arg, kUseStackSpaceOperand,
                           &stack_space_operand, return_value_operand);
}

void Builtins::Generate_CallApiGetter(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- cp                  : context
  //  -- a1                  : receiver
  //  -- a3                  : accessor info
  //  -- a0                  : holder
  // -----------------------------------
  //
  // Build v8::PropertyCallbackInfo::args_ array on the stack and push property
  // name below the exit frame to make GC aware of them.
  using PCI = PropertyCallbackInfo<v8::Value>;
  using PCA = PropertyCallbackArguments;
  static_assert(PCA::kShouldThrowOnErrorIndex == 0);
  static_assert(PCA::kHolderIndex == 1);
  static_assert(PCA::kIsolateIndex == 2);
  static_assert(PCA::kHolderV2Index == 3);
  static_assert(PCA::kReturnValueIndex == 4);
  static_assert(PCA::kDataIndex == 5);
  static_assert(PCA::kThisIndex == 6);
  static_assert(PCA::kArgsLength == 7);

  // Set up PropertyCallbackInfo's args_ on the stack as follows:
  // Target state:
  //   sp[0 * kSystemPointerSize]: name
  //   sp[1 * kSystemPointerSize]: kShouldThrowOnErrorIndex   <= PCI:args_
  //   sp[2 * kSystemPointerSize]: kHolderIndex
  //   sp[3 * kSystemPointerSize]: kIsolateIndex
  //   sp[4 * kSystemPointerSize]: kHolderV2Index
  //   sp[5 * kSystemPointerSize]: kReturnValueIndex
  //   sp[6 * kSystemPointerSize]: kDataIndex
  //   sp[7 * kSystemPointerSize]: kThisIndex / receiver

  Register name_arg = kCArgRegs[0];
  Register property_callback_info_arg = kCArgRegs[1];

  Register api_function_address = a2;
  Register receiver = ApiGetterDescriptor::ReceiverRegister();
  Register holder = ApiGetterDescriptor::HolderRegister();
  Register callback = ApiGetterDescriptor::CallbackRegister();
  Register scratch = a4;
  DCHECK(!AreAliased(receiver, holder, callback, scratch));

  // Here and below +1 is for name() pushed after the args_ array.
  __ Sub_d(sp, sp, (PCA::kArgsLength + 1) * kSystemPointerSize);
  __ St_d(receiver, MemOperand(sp, (PCA::kThisIndex + 1) * kSystemPointerSize));
  __ LoadTaggedField(scratch,
                     FieldMemOperand(callback, AccessorInfo::kDataOffset));
  __ St_d(scratch, MemOperand(sp, (PCA::kDataIndex + 1) * kSystemPointerSize));
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  __ St_d(scratch,
          MemOperand(sp, (PCA::kReturnValueIndex + 1) * kSystemPointerSize));
  __ St_d(zero_reg,
          MemOperand(sp, (PCA::kHolderV2Index + 1) * kSystemPointerSize));
  __ li(scratch, ExternalReference::isolate_address(masm->isolate()));
  __ St_d(scratch,
          MemOperand(sp, (PCA::kIsolateIndex + 1) * kSystemPointerSize));
  __ St_d(holder, MemOperand(sp, (PCA::kHolderIndex + 1) * kSystemPointerSize));
  // should_throw_on_error -> false
  DCHECK_EQ(0, Smi::zero().ptr());
  __ St_d(zero_reg, MemOperand(sp, (PCA::kShouldThrowOnErrorIndex + 1) *
                                       kSystemPointerSize));
  __ LoadTaggedField(scratch,
                     FieldMemOperand(callback, AccessorInfo::kNameOffset));
  __ St_d(scratch, MemOperand(sp, 0 * kSystemPointerSize));

  // v8::PropertyCallbackInfo::args_ array and name handle.
  static constexpr int kPaddingOnStackSlots = 0;
  static constexpr int kNameOnStackSlots = 1;
  static constexpr int kNameStackIndex = kPaddingOnStackSlots;
  static constexpr int kPCAStackIndex =
      kNameOnStackSlots + kPaddingOnStackSlots;
  static constexpr int kStackUnwindSpace = PCA::kArgsLength + kPCAStackIndex;

  __ RecordComment(
      "Load address of v8::PropertyAccessorInfo::args_ array and name handle.");
#ifdef V8_ENABLE_DIRECT_LOCAL
  // name_arg = Local<Name>(name), name value was pushed to GC-ed stack space.
  __ mov(name_arg, scratch);
  USE(kNameStackIndex);
#else
  // name_arg = Local<Name>(&name), name value was pushed to GC-ed stack space.
  __ Add_d(name_arg, sp, Operand(kNameStackIndex * kSystemPointerSize));
#endif
  // property_callback_info_arg = v8::PCI::args_ (= &ShouldThrow)
  __ Add_d(property_callback_info_arg, sp,
           Operand(kPCAStackIndex * kSystemPointerSize));

  const int kApiStackSpace = 1;
  static_assert(kApiStackSpace * kSystemPointerSize == sizeof(PCI));
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(kApiStackSpace, StackFrame::API_ACCESSOR_EXIT);

  __ RecordComment("Create v8::PropertyCallbackInfo object on the stack.");
  // Initialize v8::PropertyCallbackInfo::args_ field.
  __ St_d(property_callback_info_arg, MemOperand(sp, 1 * kSystemPointerSize));
  // property_callback_info_arg = v8::PropertyCallbackInfo&
  __ Add_d(property_callback_info_arg, sp, Operand(1 * kSystemPointerSize));

  __ RecordComment("Load api_function_address");
  __ LoadExternalPointerField(
      api_function_address,
      FieldMemOperand(callback, AccessorInfo::kMaybeRedirectedGetterOffset),
      kAccessorInfoGetterTag);

  DCHECK(
      !AreAliased(api_function_address, property_callback_info_arg, name_arg));

  ExternalReference thunk_ref =
      ExternalReference::invoke_accessor_getter_callback();
  // Pass AccessorInfo to thunk wrapper in case profiler or side-effect
  // checking is enabled.
  Register thunk_arg = callback;

  MemOperand return_value_operand =
      ExitFrameCallerStackSlotOperand(PCA::kReturnValueIndex + kPCAStackIndex);
  MemOperand* const kUseStackSpaceConstant = nullptr;

  const bool with_profiling = true;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, thunk_arg, kStackUnwindSpace,
                           kUseStackSpaceConstant, return_value_operand);
}

void Builtins::Generate_DirectCEntry(MacroAssembler* masm) {
  // The sole purpose of DirectCEntry is for movable callers (e.g. any general
  // purpose InstructionStream object) to be able to call into C functions that
  // may trigger GC and thus move the caller.
  //
  // DirectCEntry places the return address on the stack (updated by the GC),
  // making the call GC safe. The irregexp backend relies on this.

  __ St_d(ra, MemOperand(sp, 0));  // Store the return address.
  __ Call(t7);                     // Call the C++ function.
  __ Ld_d(ra, MemOperand(sp, 0));  // Return to calling code.

  // TODO(LOONG_dev): LOONG64 Check this assert.
  if (v8_flags.debug_code && v8_flags.enable_slow_asserts) {
    // In case of an error the return address may point to a memory area
    // filled with kZapValue by the GC. Dereference the address and check for
    // this.
    __ Ld_d(a4, MemOperand(ra, 0));
    __ Assert(ne, AbortReason::kReceivedInvalidReturnAddress, a4,
              Operand(reinterpret_cast<uint64_t>(kZapValue)));
  }

  __ Jump(ra);
}

namespace {

// This code tries to be close to ia32 code so that any changes can be
// easily ported.
void Generate_DeoptimizationEntry(MacroAssembler* masm,
                                  DeoptimizeKind deopt_kind) {
  Isolate* isolate = masm->isolate();

  // Unlike on ARM we don't save all the registers, just the useful ones.
  // For the rest, there are gaps on the stack, so the offsets remain the same.
  const int kNumberOfRegisters = Register::kNumRegisters;

  RegList restored_regs = kJSCallerSaved | kCalleeSaved;
  RegList saved_regs = restored_regs | sp | ra;

  const int kSimd128RegsSize = kSimd128Size * Simd128Register::kNumRegisters;

  // Save all allocatable simd128 / double registers before messing with them.
  // TODO(loong64): Add simd support here.
  __ Sub_d(sp, sp, Operand(kSimd128RegsSize));
  const RegisterConfiguration* config = RegisterConfiguration::Default();
  for (int i = 0; i < config->num_allocatable_double_registers(); ++i) {
    int code = config->GetAllocatableDoubleCode(i);
    const DoubleRegister fpu_reg = DoubleRegister::from_code(code);
    int offset = code * kSimd128Size;
    __ Fst_d(fpu_reg, MemOperand(sp, offset));
  }

  // Push saved_regs (needed to populate FrameDescription::registers_).
  // Leave gaps for other registers.
  __ Sub_d(sp, sp, kNumberOfRegisters * kSystemPointerSize);
  for (int16_t i = kNumberOfRegisters - 1; i >= 0; i--) {
    if ((saved_regs.bits() & (1 << i)) != 0) {
      __ St_d(ToRegister(i), MemOperand(sp, kSystemPointerSize * i));
    }
  }

  __ li(a2,
        ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate));
  __ St_d(fp, MemOperand(a2, 0));

  const int kSavedRegistersAreaSize =
      (kNumberOfRegisters * kSystemPointerSize) + kSimd128RegsSize;

  // Get the address of the location in the code object (a2) (return
  // address for lazy deoptimization) and compute the fp-to-sp delta in
  // register a3.
  __ mov(a2, ra);
  __ Add_d(a3, sp, Operand(kSavedRegistersAreaSize));

  __ sub_d(a3, fp, a3);

  // Allocate a new deoptimizer object.
  __ PrepareCallCFunction(5, a4);
  // Pass six arguments, according to n64 ABI.
  __ mov(a0, zero_reg);
  Label context_check;
  __ Ld_d(a1, MemOperand(fp, CommonFrameConstants::kContextOrFrameTypeOffset));
  __ JumpIfSmi(a1, &context_check);
  __ Ld_d(a0, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ bind(&context_check);
  __ li(a1, Operand(static_cast<int>(deopt_kind)));
  // a2: code address or 0 already loaded.
  // a3: already has fp-to-sp delta.
  __ li(a4, ExternalReference::isolate_address(isolate));

  // Call Deoptimizer::New().
  {
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::new_deoptimizer_function(), 5);
  }

  // Preserve "deoptimizer" object in register a0 and get the input
  // frame descriptor pointer to a1 (deoptimizer->input_);
  // Move deopt-obj to a0 for call to Deoptimizer::ComputeOutputFrames() below.
  __ Ld_d(a1, MemOperand(a0, Deoptimizer::input_offset()));

  // Copy core registers into FrameDescription::registers_[kNumRegisters].
  DCHECK_EQ(Register::kNumRegisters, kNumberOfRegisters);
  for (int i = 0; i < kNumberOfRegisters; i++) {
    int offset =
        (i * kSystemPointerSize) + FrameDescription::registers_offset();
    if ((saved_regs.bits() & (1 << i)) != 0) {
      __ Ld_d(a2, MemOperand(sp, i * kSystemPointerSize));
      __ St_d(a2, MemOperand(a1, offset));
    } else if (v8_flags.debug_code) {
      __ li(a2, Operand(kDebugZapValue));
      __ St_d(a2, MemOperand(a1, offset));
    }
  }

  // Copy simd128 / double registers to the input frame.
  // TODO(loong64): Add simd support here.
  int simd128_regs_offset = FrameDescription::simd128_registers_offset();
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    int dst_offset = code * kSimd128Size + simd128_regs_offset;
    int src_offset =
        code * kSimd128Size + kNumberOfRegisters * kSystemPointerSize;
    __ Fld_d(f0, MemOperand(sp, src_offset));
    __ Fst_d(f0, MemOperand(a1, dst_offset));
  }

  // Remove the saved registers from the stack.
  __ Add_d(sp, sp, Operand(kSavedRegistersAreaSize));

  // Compute a pointer to the unwinding limit in register a2; that is
  // the first stack slot not part of the input frame.
  __ Ld_d(a2, MemOperand(a1, FrameDescription::frame_size_offset()));
  __ add_d(a2, a2, sp);

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ Add_d(a3, a1, Operand(FrameDescription::frame_content_offset()));
  Label pop_loop;
  Label pop_loop_header;
  __ Branch(&pop_loop_header);
  __ bind(&pop_loop);
  __ Pop(a4);
  __ St_d(a4, MemOperand(a3, 0));
  __ addi_d(a3, a3, sizeof(uint64_t));
  __ bind(&pop_loop_header);
  __ BranchShort(&pop_loop, ne, a2, Operand(sp));
  // Compute the output frame in the deoptimizer.
  __ Push(a0);  // Preserve deoptimizer object across call.
  // a0: deoptimizer object; a1: scratch.
  __ PrepareCallCFunction(1, a1);
  // Call Deoptimizer::ComputeOutputFrames().
  {
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::compute_output_frames_function(), 1);
  }
  __ Pop(a0);  // Restore deoptimizer object (class Deoptimizer).

  __ Ld_d(sp, MemOperand(a0, Deoptimizer::caller_frame_top_offset()));

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop, outer_loop_header, inner_loop_header;
  // Outer loop state: a4 = current "FrameDescription** output_",
  // a1 = one past the last FrameDescription**.
  __ Ld_w(a1, MemOperand(a0, Deoptimizer::output_count_offset()));
  __ Ld_d(a4, MemOperand(a0, Deoptimizer::output_offset()));  // a4 is output_.
  __ Alsl_d(a1, a1, a4, kSystemPointerSizeLog2);
  __ Branch(&outer_loop_header);
  __ bind(&outer_push_loop);
  // Inner loop state: a2 = current FrameDescription*, a3 = loop index.
  __ Ld_d(a2, MemOperand(a4, 0));  // output_[ix]
  __ Ld_d(a3, MemOperand(a2, FrameDescription::frame_size_offset()));
  __ Branch(&inner_loop_header);
  __ bind(&inner_push_loop);
  __ Sub_d(a3, a3, Operand(sizeof(uint64_t)));
  __ Add_d(a6, a2, Operand(a3));
  __ Ld_d(a7, MemOperand(a6, FrameDescription::frame_content_offset()));
  __ Push(a7);
  __ bind(&inner_loop_header);
  __ BranchShort(&inner_push_loop, ne, a3, Operand(zero_reg));

  __ Add_d(a4, a4, Operand(kSystemPointerSize));
  __ bind(&outer_loop_header);
  __ BranchShort(&outer_push_loop, lt, a4, Operand(a1));

  __ Ld_d(a1, MemOperand(a0, Deoptimizer::input_offset()));
  // TODO(loong64): Add simd support here.
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    const DoubleRegister fpu_reg = DoubleRegister::from_code(code);
    int src_offset = code * kSimd128Size + simd128_regs_offset;
    __ Fld_d(fpu_reg, MemOperand(a1, src_offset));
  }

  // Push pc and continuation from the last output frame.
  __ Ld_d(a6, MemOperand(a2, FrameDescription::pc_offset()));
  __ Push(a6);
  __ Ld_d(a6, MemOperand(a2, FrameDescription::continuation_offset()));
  __ Push(a6);

  // Technically restoring 'at' should work unless zero_reg is also restored
  // but it's safer to check for this.
  DCHECK(!(restored_regs.has(t7)));
  // Restore the registers from the last output frame.
  __ mov(t7, a2);
  for (int i = kNumberOfRegisters - 1; i >= 0; i--) {
    int offset =
        (i * kSystemPointerSize) + FrameDescription::registers_offset();
    if ((restored_regs.bits() & (1 << i)) != 0) {
      __ Ld_d(ToRegister(i), MemOperand(t7, offset));
    }
  }

  __ Pop(t7);  // Get continuation, leave pc on stack.
  __ Pop(ra);
  __ Jump(t7);
  __ stop();
}

}  // namespace

void Builtins::Generate_DeoptimizationEntry_Eager(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kEager);
}

void Builtins::Generate_DeoptimizationEntry_Lazy(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kLazy);
}

namespace {

// Restarts execution either at the current or next (in execution order)
// bytecode. If there is baseline code on the shared function info, converts an
// interpreter frame into a baseline frame and continues execution in baseline
// code. Otherwise execution continues with bytecode.
void Generate_BaselineOrInterpreterEntry(MacroAssembler* masm,
                                         bool next_bytecode,
                                         bool is_osr = false) {
  Label start;
  __ bind(&start);

  // Get function from the frame.
  Register closure = a1;
  __ Ld_d(closure, MemOperand(fp, StandardFrameConstants::kFunctionOffset));

  // Get the InstructionStream object from the shared function info.
  Register code_obj = s1;
  __ LoadTaggedField(
      code_obj,
      FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));

  if (is_osr) {
    ResetSharedFunctionInfoAge(masm, code_obj);
  }

  GetSharedFunctionInfoData(masm, code_obj, code_obj, t2);

  // Check if we have baseline code. For OSR entry it is safe to assume we
  // always have baseline code.
  if (!is_osr) {
    Label start_with_baseline;
    __ JumpIfObjectType(&start_with_baseline, eq, code_obj, CODE_TYPE, t2);

    // Start with bytecode as there is no baseline code.
    Builtin builtin = next_bytecode ? Builtin::kInterpreterEnterAtNextBytecode
                                    : Builtin::kInterpreterEnterAtBytecode;
    __ TailCallBuiltin(builtin);

    // Start with baseline code.
    __ bind(&start_with_baseline);
  } else if (v8_flags.debug_code) {
    __ GetObjectType(code_obj, t2, t2);
    __ Assert(eq, AbortReason::kExpectedBaselineData, t2, Operand(CODE_TYPE));
  }

  if (v8_flags.debug_code) {
    AssertCodeIsBaseline(masm, code_obj, t2);
  }

  // Load the feedback cell and vector.
  Register feedback_cell = a2;
  Register feedback_vector = t8;
  __ LoadTaggedField(feedback_cell,
                     FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedField(
      feedback_vector,
      FieldMemOperand(feedback_cell, FeedbackCell::kValueOffset));

  Label install_baseline_code;
  // Check if feedback vector is valid. If not, call prepare for baseline to
  // allocate it.
  __ JumpIfObjectType(&install_baseline_code, ne, feedback_vector,
                      FEEDBACK_VECTOR_TYPE, t2);

  // Save BytecodeOffset from the stack frame.
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  // Replace bytecode offset with feedback cell.
  static_assert(InterpreterFrameConstants::kBytecodeOffsetFromFp ==
                BaselineFrameConstants::kFeedbackCellFromFp);
  __ St_d(feedback_cell,
          MemOperand(fp, BaselineFrameConstants::kFeedbackCellFromFp));
  feedback_cell = no_reg;
  // Update feedback vector cache.
  static_assert(InterpreterFrameConstants::kFeedbackVectorFromFp ==
                BaselineFrameConstants::kFeedbackVectorFromFp);
  __ St_d(feedback_vector,
          MemOperand(fp, InterpreterFrameConstants::kFeedbackVectorFromFp));
  feedback_vector = no_reg;

  // Compute baseline pc for bytecode offset.
  ExternalReference get_baseline_pc_extref;
  if (next_bytecode || is_osr) {
    get_baseline_pc_extref =
        ExternalReference::baseline_pc_for_next_executed_bytecode();
  } else {
    get_baseline_pc_extref =
        ExternalReference::baseline_pc_for_bytecode_offset();
  }

  Register get_baseline_pc = a3;
  __ li(get_baseline_pc, get_baseline_pc_extref);

  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset.
  // TODO(pthier): Investigate if it is feasible to handle this special case
  // in TurboFan instead of here.
  Label valid_bytecode_offset, function_entry_bytecode;
  if (!is_osr) {
    __ Branch(&function_entry_bytecode, eq, kInterpreterBytecodeOffsetRegister,
              Operand(BytecodeArray::kHeaderSize - kHeapObjectTag +
                      kFunctionEntryBytecodeOffset));
  }

  __ Sub_d(kInterpreterBytecodeOffsetRegister,
           kInterpreterBytecodeOffsetRegister,
           (BytecodeArray::kHeaderSize - kHeapObjectTag));

  __ bind(&valid_bytecode_offset);
  // Get bytecode array from the stack frame.
  __ Ld_d(kInterpreterBytecodeArrayRegister,
          MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  // Save the accumulator register, since it's clobbered by the below call.
  __ Push(kInterpreterAccumulatorRegister);
  {
    __ Move(kCArgRegs[0], code_obj);
    __ Move(kCArgRegs[1], kInterpreterBytecodeOffsetRegister);
    __ Move(kCArgRegs[2], kInterpreterBytecodeArrayRegister);
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ PrepareCallCFunction(3, 0, a4);
    __ CallCFunction(get_baseline_pc, 3, 0);
  }
  __ LoadCodeInstructionStart(code_obj, code_obj, kJSEntrypointTag);
  __ Add_d(code_obj, code_obj, kReturnRegister0);
  __ Pop(kInterpreterAccumulatorRegister);

  if (is_osr) {
    // TODO(liuyu): Remove Ld as arm64 after register reallocation.
    __ Ld_d(kInterpreterBytecodeArrayRegister,
            MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
    Generate_OSREntry(masm, code_obj);
  } else {
    __ Jump(code_obj);
  }
  __ Trap();  // Unreachable.

  if (!is_osr) {
    __ bind(&function_entry_bytecode);
    // If the bytecode offset is kFunctionEntryOffset, get the start address of
    // the first bytecode.
    __ mov(kInterpreterBytecodeOffsetRegister, zero_reg);
    if (next_bytecode) {
      __ li(get_baseline_pc,
            ExternalReference::baseline_pc_for_bytecode_offset());
    }
    __ Branch(&valid_bytecode_offset);
  }

  __ bind(&install_baseline_code);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(kInterpreterAccumulatorRegister);
    __ Push(closure);
    __ CallRuntime(Runtime::kInstallBaselineCode, 1);
    __ Pop(kInterpreterAccumulatorRegister);
  }
  // Retry from the start after installing baseline code.
  __ Branch(&start);
}

}  // namespace

void Builtins::Generate_BaselineOrInterpreterEnterAtBytecode(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, false);
}

void Builtins::Generate_BaselineOrInterpreterEnterAtNextBytecode(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, true);
}

void Builtins::Generate_InterpreterOnStackReplacement_ToBaseline(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, false, true);
}

void Builtins::Generate_RestartFrameTrampoline(MacroAssembler* masm) {
  // Restart the current frame:
  // - Look up current function on the frame.
  // - Leave the frame.
  // - Restart the frame by calling the function.

  __ Ld_d(a1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ Ld_d(a0, MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ LeaveFrame(StackFrame::INTERPRETED);

  // The arguments are already in the stack (including any necessary padding),
  // we should not try to massage the arguments again.
  __ li(a2, Operand(kDontAdaptArgumentsSentinel));
  __ InvokeFunction(a1, a2, a0, InvokeType::kJump);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_LOONG64
