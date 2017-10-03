//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "IncrementalExecutor.h"
#include "IncrementalJIT.h"
#include "Threading.h"

#include "cling/Interpreter/Value.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Utils/AST.h"
#include "cling/Utils/Output.h"
#include "cling/Utils/Platform.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/CodeGenOptions.h"
#include "clang/Frontend/CompilerInstance.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace cling {

namespace {

static std::unique_ptr<TargetMachine>
CreateHostTargetMachine(const clang::CompilerInstance& CI) {
  const clang::TargetOptions& TargetOpts = CI.getTargetOpts();
  const clang::CodeGenOptions& CGOpt = CI.getCodeGenOpts();
  const std::string& Triple = TargetOpts.Triple;

  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);
  if (!TheTarget) {
    cling::errs() << "cling::IncrementalExecutor: unable to find target:\n"
                  << Error;
    return std::unique_ptr<TargetMachine>();
  }

// We have to use large code model for PowerPC64 because TOC and text sections
// can be more than 2GB apart.
#if defined(__powerpc64__) || defined(__PPC64__)
  CodeModel::Model CMModel = CodeModel::Large;
#else
  CodeModel::Model CMModel = CodeModel::JITDefault;
#endif
  CodeGenOpt::Level OptLevel = CodeGenOpt::Default;
  switch (CGOpt.OptimizationLevel) {
    case 0: OptLevel = CodeGenOpt::None; break;
    case 1: OptLevel = CodeGenOpt::Less; break;
    case 2: OptLevel = CodeGenOpt::Default; break;
    case 3: OptLevel = CodeGenOpt::Aggressive; break;
    default: OptLevel = CodeGenOpt::Default;
  }

  std::string MCPU;
  std::string FeaturesStr;

  return std::unique_ptr<TargetMachine>(TheTarget->createTargetMachine(Triple,
                                        MCPU, FeaturesStr,
                                        llvm::TargetOptions(),
                                        Optional<Reloc::Model>(), CMModel,
                                        OptLevel));
}

} // anonymous namespace

IncrementalExecutor::IncrementalExecutor(clang::DiagnosticsEngine& diags,
                                         const clang::CompilerInstance& CI):
  m_Callbacks(nullptr), m_externalIncrementalExecutor(nullptr)
#if 0
  : m_Diags(diags)
#endif
{

  // MSVC doesn't support m_AtExitFuncsSpinLock=ATOMIC_FLAG_INIT; in the class definition
  std::atomic_flag_clear( &m_AtExitFuncsSpinLock );

  std::unique_ptr<TargetMachine> TM(CreateHostTargetMachine(CI));

  TM->Options.EmulatedTLS = 1;

  m_BackendPasses.reset(new BackendPasses(CI.getCodeGenOpts(),
                                          CI.getTargetOpts(),
                                          CI.getLangOpts(),
                                          *TM));
  m_JIT.reset(new IncrementalJIT(*this, std::move(TM)));
}

// Keep in source: ~unique_ptr<ClingJIT> needs ClingJIT
IncrementalExecutor::~IncrementalExecutor() {}

void IncrementalExecutor::shuttingDown() {
  // It is legal to register an atexit handler from within another atexit
  // handler and furthor-more the standard says they need to run in reverse
  // order, so this function must be recursion safe.
  AtExitFunctions Local;
  {
    cling::internal::SpinLockGuard slg(m_AtExitFuncsSpinLock);
    // Check this case first, to avoid the swap all-together.
    if (m_AtExitFuncs.empty())
      return;
    Local.swap(m_AtExitFuncs);
  }
  for (auto&& Ordered : llvm::reverse(Local.ordered())) {
    for (auto&& AtExit : llvm::reverse(Ordered->second))
      AtExit();
    // The standard says that they need to run in reverse order, which means
    // anything added from 'AtExit()' must now be run!
    shuttingDown();
  }
}

void IncrementalExecutor::AddAtExitFunc(void (*func) (void*), void* arg,
                                        llvm::Module* M) {
  // Register a CXAAtExit function
  cling::internal::SpinLockGuard slg(m_AtExitFuncsSpinLock);
  m_AtExitFuncs[M].emplace_back(func, arg);
}

void unresolvedSymbol()
{
  // This might get called recursively, or a billion of times. Do not generate
  // useless output; unresolvedSymbol() is always handed out with an error
  // message - that's enough.
  //cling::errs() << "IncrementalExecutor: calling unresolved symbol, "
  //  "see previous error message!\n";

  // throw exception instead?
}

void* IncrementalExecutor::HandleMissingFunction(const std::string& mangled_name)
{
  // Not found in the map, add the symbol in the list of unresolved symbols
  if (m_unresolvedSymbols.insert(mangled_name).second) {
    //cling::errs() << "IncrementalExecutor: use of undefined symbol '"
    //             << mangled_name << "'!\n";
  }

  return utils::FunctionToVoidPtr(&unresolvedSymbol);
}

void* IncrementalExecutor::NotifyLazyFunctionCreators(const std::string& mangled_name)
{
  for (std::vector<LazyFunctionCreatorFunc_t>::iterator it
         = m_lazyFuncCreator.begin(), et = m_lazyFuncCreator.end();
       it != et; ++it) {
    void* ret = (void*)((LazyFunctionCreatorFunc_t)*it)(mangled_name);
    if (ret)
      return ret;
  }
  void *address = nullptr;
  if (m_externalIncrementalExecutor)
   address = m_externalIncrementalExecutor->getAddressOfGlobal(mangled_name);

  return (address ? address : HandleMissingFunction(mangled_name));
}

#if 0
// FIXME: employ to empty module dependencies *within* the *current* module.
static void
freeCallersOfUnresolvedSymbols(llvm::SmallVectorImpl<llvm::Function*>&
                               funcsToFree, llvm::ExecutionEngine* engine) {
  llvm::SmallPtrSet<llvm::Function*, 40> funcsToFreeUnique;
  for (size_t i = 0; i < funcsToFree.size(); ++i) {
    llvm::Function* func = funcsToFree[i];
    assert(func && "Cannot free NULL function");
    if (funcsToFreeUnique.insert(func).second) {
      for (llvm::Value::use_iterator IU = func->use_begin(),
             EU = func->use_end(); IU != EU; ++IU) {
        llvm::Instruction* instUser = llvm::dyn_cast<llvm::Instruction>(*IU);
        if (!instUser) continue;
        if (!instUser->getParent()) continue;
        if (llvm::Function* userFunc = instUser->getParent()->getParent())
          funcsToFree.push_back(userFunc);
      }
    }
  }
  for (llvm::SmallPtrSet<llvm::Function*, 40>::iterator
         I = funcsToFreeUnique.begin(), E = funcsToFreeUnique.end();
       I != E; ++I) {
    // This should force the JIT to recompile the function. But the stubs stay,
    // and the JIT reuses the stubs now pointing nowhere, i.e. without updating
    // the machine code address. Fix the JIT, or hope that MCJIT helps.
    //engine->freeMachineCodeForFunction(*I);
    engine->updateGlobalMapping(*I, 0);
  }
}
#endif

IncrementalExecutor::ExecutionResult
IncrementalExecutor::runStaticInitializersOnce(const Transaction& T) {
  llvm::Module* m = T.getModule();
  assert(m && "Module must not be null");

  // We don't care whether something was unresolved before.
  m_unresolvedSymbols.clear();

  // check if there is any unresolved symbol in the list
  if (diagnoseUnresolvedSymbols("static initializers"))
    return kExeUnresolvedSymbols;

  llvm::GlobalVariable* GV
     = m->getGlobalVariable("llvm.global_ctors", true);
  // Nothing to do is good, too.
  if (!GV) return kExeSuccess;

  // Close similarity to
  // m_engine->runStaticConstructorsDestructors(false) aka
  // llvm::ExecutionEngine::runStaticConstructorsDestructors()
  // is intentional; we do an extra pass to check whether the JIT
  // managed to collect all the symbols needed by the niitializers.
  // Should be an array of '{ i32, void ()* }' structs.  The first value is
  // the init priority, which we ignore.
  llvm::ConstantArray *InitList
    = llvm::dyn_cast<llvm::ConstantArray>(GV->getInitializer());

  // We need to delete it here just in case we have recursive inits, otherwise
  // it will call inits multiple times.
  GV->eraseFromParent();

  if (InitList == 0)
    return kExeSuccess;

  //SmallVector<Function*, 2> initFuncs;

  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i) {
    llvm::ConstantStruct *CS
      = llvm::dyn_cast<llvm::ConstantStruct>(InitList->getOperand(i));
    if (CS == 0) continue;

    llvm::Constant *FP = CS->getOperand(1);
    if (FP->isNullValue())
      continue;  // Found a sentinal value, ignore.

    // Strip off constant expression casts.
    if (llvm::ConstantExpr *CE = llvm::dyn_cast<llvm::ConstantExpr>(FP))
      if (CE->isCast())
        FP = CE->getOperand(0);

    // Execute the ctor/dtor function!
    if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(FP)) {
      const llvm::StringRef fName = F->getName();
      executeInit(fName);
/*
      initFuncs.push_back(F);
      if (fName.startswith("_GLOBAL__sub_I_")) {
        BasicBlock& BB = F->getEntryBlock();
        for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I)
          if (CallInst* call = dyn_cast<CallInst>(I))
            initFuncs.push_back(call->getCalledFunction());
      }
*/
    }
  }

/*
  for (SmallVector<Function*,2>::iterator I = initFuncs.begin(),
         E = initFuncs.end(); I != E; ++I) {
    // Cleanup also the dangling init functions. They are in the form:
    // define internal void @_GLOBAL__I_aN() section "..."{
    // entry:
    //   call void @__cxx_global_var_init(N-1)()
    //   call void @__cxx_global_var_initM()
    //   ret void
    // }
    //
    // define internal void @__cxx_global_var_init(N-1)() section "..." {
    // entry:
    //   call void @_ZN7MyClassC1Ev(%struct.MyClass* @n)
    //   ret void
    // }

    // Erase __cxx_global_var_init(N-1)() first.
    (*I)->removeDeadConstantUsers();
    (*I)->eraseFromParent();
  }
*/

  return kExeSuccess;
}

void IncrementalExecutor::runAndRemoveStaticDestructors(Transaction* T) {
  assert(T && "Must be set");
  // Collect all the dtors bound to this transaction.
  AtExitFunctions::mapped_type Local;
  {
    cling::internal::SpinLockGuard slg(m_AtExitFuncsSpinLock);
    auto Itr = m_AtExitFuncs.find(T->getModule());
    if (Itr == m_AtExitFuncs.end()) return;
    m_AtExitFuncs.erase(Itr, &Local);
  } // end of spin lock lifetime block.

  // 'Unload' the cxa_atexit, atexit entities.
  for (auto&& AtExit : llvm::reverse(Local)) {
    AtExit();
    // Run anything that was just registered in 'AtExit()'
    runAndRemoveStaticDestructors(T);
  }
}

void
IncrementalExecutor::installLazyFunctionCreator(LazyFunctionCreatorFunc_t fp)
{
  m_lazyFuncCreator.push_back(fp);
}

bool
IncrementalExecutor::addSymbol(const char* Name,  void* Addr,
                               bool Jit) {
  return m_JIT->lookupSymbol(Name, Addr, Jit).second;
}

void* IncrementalExecutor::getAddressOfGlobal(llvm::StringRef symbolName,
                                              bool* fromJIT /*=0*/) {
  // Return a symbol's address, and whether it was jitted.
  void* address = m_JIT->lookupSymbol(symbolName).first;

  // It's not from the JIT if it's in a dylib.
  if (fromJIT)
    *fromJIT = !address;

  if (!address)
    return (void*)m_JIT->getSymbolAddress(symbolName, false /*no dlsym*/);

  return address;
}

void*
IncrementalExecutor::getPointerToGlobalFromJIT(const llvm::GlobalValue& GV) {
  // Get the function / variable pointer referenced by GV.

  // We don't care whether something was unresolved before.
  m_unresolvedSymbols.clear();

  void* addr = (void*)m_JIT->getSymbolAddress(GV.getName(),
                                              false /*no dlsym*/);

  if (diagnoseUnresolvedSymbols(GV.getName(), "symbol"))
    return 0;
  return addr;
}

bool IncrementalExecutor::diagnoseUnresolvedSymbols(llvm::StringRef trigger,
                                                    llvm::StringRef title) {
  if (m_unresolvedSymbols.empty())
    return false;

  llvm::SmallVector<llvm::Function*, 128> funcsToFree;
  for (const std::string& sym : m_unresolvedSymbols) {
#if 0
    // FIXME: This causes a lot of test failures, for some reason it causes
    // the call to HandleMissingFunction to be elided.
    unsigned diagID = m_Diags.getCustomDiagID(clang::DiagnosticsEngine::Error,
                                              "%0 unresolved while jitting %1");
    (void)diagID;
    //m_Diags.Report(diagID) << sym << funcname; // TODO: demangle the names.
#endif

    cling::errs() << "IncrementalExecutor::executeFunction: symbol '" << sym
                  << "' unresolved while linking ";
    if (trigger.find(utils::Synthesize::UniquePrefix) != llvm::StringRef::npos)
      cling::errs() << "[cling interface function]";
    else {
      if (!title.empty())
        cling::errs() << title << " '";
      cling::errs() << trigger;
      if (!title.empty())
        cling::errs() << "'";
    }
    cling::errs() << "!\n";

    // Be helpful, demangle!
    std::string demangledName = platform::Demangle(sym);
    if (!demangledName.empty()) {
       cling::errs()
          << "You are probably missing the definition of "
          << demangledName << "\n"
          << "Maybe you need to load the corresponding shared library?\n";
    }

    //llvm::Function *ff = m_engine->FindFunctionNamed(i->c_str());
    // i could also reference a global variable, in which case ff == 0.
    //if (ff)
    //  funcsToFree.push_back(ff);
  }
  //freeCallersOfUnresolvedSymbols(funcsToFree, m_engine.get());
  m_unresolvedSymbols.clear();
  return true;
}

}// end namespace cling
