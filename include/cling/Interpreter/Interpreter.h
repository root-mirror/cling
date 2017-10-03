//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INTERPRETER_H
#define CLING_INTERPRETER_H

#include "cling/Interpreter/InvocationOptions.h"

#include "llvm/ADT/StringRef.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

#ifdef LLVM_ON_WIN32
#include <unordered_set>
#endif

namespace llvm {
  class raw_ostream;
  struct GenericValue;
  class ExecutionEngine;
  class LLVMContext;
  class Module;
  class StringRef;
  class Type;
  template <typename T> class SmallVectorImpl;
}

namespace clang {
  class ASTContext;
  class ASTDeserializationListener;
  class CompilerInstance;
  class Decl;
  class DeclContext;
  class DiagnosticsEngine;
  class FunctionDecl;
  class GlobalDecl;
  class MacroInfo;
  class NamedDecl;
  class Parser;
  class Preprocessor;
  class QualType;
  class RecordDecl;
  class Sema;
  class SourceLocation;
  class SourceManager;
  class Type;
  class PresumedLoc;
}

namespace cling {
  namespace runtime {
    namespace internal {
      class DynamicExprInfo;
      template <typename T>
      T EvaluateT(DynamicExprInfo* ExprInfo, clang::DeclContext* DC);
      class LifetimeHandler;
    }
  }
  class ClangInternalState;
  class CompilationOptions;
  class DynamicLibraryManager;
  class IncrementalExecutor;
  class IncrementalParser;
  class InterpreterCallbacks;
  class LookupHelper;
  class Value;
  class Transaction;

  ///\brief Class that implements the interpreter-like behavior. It manages the
  /// incremental compilation.
  ///
  class Interpreter {
  public:
     // IgnoreFilesFunc_t takes a const reference to avoid having to
     // include the actual definition of PresumedLoc.
     using IgnoreFilesFunc_t = bool (*)(const clang::PresumedLoc&);

    ///\brief Pushes a new transaction, which will collect the decls that came
    /// within the scope of the RAII object. Calls commit transaction at
    /// destruction.
    class PushTransactionRAII {
    private:
      Transaction* m_Transaction;
      const Interpreter* m_Interpreter;
    public:
      PushTransactionRAII(const Interpreter* i);
      ~PushTransactionRAII();
      void pop() const;
    };

    class StateDebuggerRAII {
    private:
      const Interpreter* m_Interpreter;
      std::unique_ptr<ClangInternalState> m_State;
    public:
      StateDebuggerRAII(const Interpreter* i);
      ~StateDebuggerRAII();
    };

    ///\brief Describes the return result of the different routines that do the
    /// incremental compilation.
    ///
    enum CompilationResult {
      kSuccess,
      kFailure,
      kMoreInputExpected
    };

    ///\brief Describes the result of running a function.
    ///
    enum ExecutionResult {
      ///\brief The function was run successfully.
      kExeSuccess,
      ///\brief Code generator is unavailable; not an error.
      kExeNoCodeGen,

      ///\brief First error value.
      kExeFirstError,
      ///\brief The function is not known and cannot be called.
      kExeFunctionNotCompiled = kExeFirstError,
      ///\brief While compiling the function, unknown symbols were encountered.
      kExeUnresolvedSymbols,
      ///\brief Compilation error.
      kExeCompilationError,
      ///\brief The function is not known.
      kExeUnkownFunction,
      ///\brief The Transaction had no module (probably an error in CodeGen).
      kExeNoModule,

      ///\brief Number of possible results.
      kNumExeResults
    };

  private:

    ///\brief Interpreter invocation options.
    ///
    InvocationOptions m_Opts;

    ///\brief The llvm library state, a per-thread object.
    ///
    std::unique_ptr<llvm::LLVMContext> m_LLVMContext;

    ///\brief Cling's execution engine - a well wrapped llvm execution engine.
    ///
    std::unique_ptr<IncrementalExecutor> m_Executor;

    ///\brief Cling's worker class implementing the incremental compilation.
    ///
    std::unique_ptr<IncrementalParser> m_IncrParser;

    ///\brief Cling's reflection information query.
    ///
    std::unique_ptr<LookupHelper> m_LookupHelper;

    ///\brief Cache of compiled destructors wrappers.
    std::unordered_map<const clang::RecordDecl*, void*> m_DtorWrappers;

    ///\brief Counter used when we need unique names.
    ///
    mutable unsigned long long m_UniqueCounter;

    ///\brief Flag toggling the Debug printing on or off.
    ///
    bool m_PrintDebug;

    ///\brief Whether DynamicLookupRuntimeUniverse.h has been parsed.
    ///
    bool m_DynamicLookupDeclared;

    ///\brief Flag toggling the dynamic scopes on or off.
    ///
    bool m_DynamicLookupEnabled;

    ///\brief Flag toggling the raw input on or off.
    ///
    bool m_RawInputEnabled;

    ///\brief Flag toggling the optimization level to be used.
    ///
    int m_OptLevel;

    ///\brief Interpreter callbacks.
    ///
    std::unique_ptr<InterpreterCallbacks> m_Callbacks;

    ///\brief Dynamic library manager object.
    ///
    std::unique_ptr<DynamicLibraryManager> m_DyLibManager;

    ///\brief Information about the last stored states through .storeState
    ///
    mutable std::vector<ClangInternalState*> m_StoredStates;

    enum {
      kStdStringTransaction = 0, // Transaction known to contain std::string
      kNumTransactions
    };
    mutable const Transaction* m_CachedTrns[kNumTransactions] = {};

#ifdef LLVM_ON_WIN32
    // Windows specific _Facet_base pointers registered at runtime.
    std::unordered_set<void*> m_RuntimeFacets;
#endif

    ///\brief Worker function, building block for interpreter's public
    /// interfaces.
    ///
    ///\param [in] input - The input being compiled.
    ///\param [in] CompilationOptions - The option set driving the compilation.
    ///\param [out] T - The cling::Transaction of the compiled input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult DeclareInternal(const std::string& input,
                                      const CompilationOptions& CO,
                                      Transaction** T = 0) const;

    ///\brief Worker function, building block for interpreter's public
    /// interfaces.
    ///
    ///\param [in] input - The input being compiled.
    ///\param [in] CompilationOptions - The option set driving the compilation.
    ///\param [in,out] V - The result of the evaluation of the input. Must be
    ///       initialized to point to the return value's location if the
    ///       expression result is an aggregate.
    ///\param [out] T - The cling::Transaction of the compiled input.
    ///\param [in] wrapPoint - Where in input to begin the wrapper
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult EvaluateInternal(const std::string& input,
                                       CompilationOptions CO,
                                       Value* V = 0,
                                       Transaction** T = 0,
                                       size_t wrapPoint = 0);

    ///\brief Worker function to code complete after all the mechanism
    /// has been set up.
    ///
    ///\param [in] input - The input being completed.
    ///\param [in] offset - The offset for the completion point.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult CodeCompleteInternal(const std::string& input,
                                           unsigned offset);

    ///\brief Wraps a given input.
    ///
    /// The interpreter must be able to run statements on the fly, which is not
    /// C++ standard-compliant operation. In order to do that we must wrap the
    /// input into a artificial function, containing the statements and run it.
    ///\param [in] Input - The input to wrap.
    ///\param [out] Buffer - string to store input if wrapped (can be Input).
    ///\param [in/out] WrapPoint - The position in Input to add the wrapper.
    /// On exit WrapPoint is updated to the position of Input in Buffer.
    ///
    ///\returns A reference to Buffer when wrapped, otherwise a ref to Input
    ///
    const std::string& WrapInput(const std::string& Input, std::string& Buffer,
                                 size_t& WrapPoint) const;

    ///\brief Runs given wrapper function.
    ///
    ///\param [in] fname - The function name.
    ///\param [in,out] res - The return result of the run function. Must be
    ///       initialized to point to the return value's location if the
    ///       expression result is an aggregate.
    ///
    ///\returns The result of the execution.
    ///
    ExecutionResult RunFunction(const clang::FunctionDecl* FD,
                                Value* res = 0);

    ///\brief Forwards to cling::IncrementalExecutor::addSymbol.
    ///
    bool addSymbol(const char* symbolName,  void* symbolAddress);

    ///\brief Compile the function definition and return its Decl.
    ///
    ///\param[in] name - name of the function, used to find its Decl.
    ///\param[in] code - function definition, starting with 'extern "C"'.
    ///\param[in] withAccessControl - whether to enforce access restrictions.
    const clang::FunctionDecl* DeclareCFunction(llvm::StringRef name,
                                                llvm::StringRef code,
                                                bool withAccessControl);

    ///\brief Initialize runtime and C/C++ level overrides
    ///
    ///\param[in] NoRuntime - Don't include the runtime headers / gCling
    ///\param[in] SyntaxOnly - In SyntaxOnly mode
    ///
    ///\returns The resulting Transation of initialization.
    ///
    Transaction* Initialize(bool NoRuntime, bool SyntaxOnly);

    ///\brief The target constructor to be called from both the delegating
    /// constructors. parentInterp might be nullptr.
    ///
    Interpreter(int argc, const char* const *argv,
                const char* llvmdir, bool noRuntime,
                const Interpreter* parentInterp);

  public:
    ///\brief Constructor for Interpreter.
    ///
    ///\param[in] argc - no. of args.
    ///\param[in] argv - arguments passed when driver is invoked.
    ///\param[in] llvmdir - ???
    ///\param[in] noRuntime - flag to control the presence of runtime universe
    ///
    Interpreter(int argc, const char* const *argv, const char* llvmdir = 0,
                bool noRuntime = false) :
      Interpreter(argc, argv, llvmdir, noRuntime, nullptr) { }

    ///\brief Constructor for child Interpreter.
    ///\param[in] parentInterpreter - the  parent interpreter of this interpreter
    ///\param[in] argc - no. of args.
    ///\param[in] argv - arguments passed when driver is invoked.
    ///\param[in] llvmdir - ???
    ///\param[in] noRuntime - flag to control the presence of runtime universe
    ///
    Interpreter(const Interpreter &parentInterpreter,int argc, const char* const *argv,
                const char* llvmdir = 0, bool noRuntime = true);

    virtual ~Interpreter();

    ///\brief Whether the Interpreter is setup and ready to be used.
    ///
    bool isValid() const;

    const InvocationOptions& getOptions() const { return m_Opts; }
    InvocationOptions& getOptions() { return m_Opts; }

    const llvm::LLVMContext* getLLVMContext() const {
      return m_LLVMContext.get();
    }

    llvm::LLVMContext* getLLVMContext() { return m_LLVMContext.get(); }

    LookupHelper& getLookupHelper() const { return *m_LookupHelper; }

    const clang::Parser& getParser() const;
    clang::Parser& getParser();

    ///\brief Returns the current or last Transactions source location.
    ///
    ///\param[in] skipWrapper - skip the length of a cling wrapper
    ///
    clang::SourceLocation getSourceLocation(bool skipWrapper = true) const;

    ///\brief Returns the next available valid free source location.
    ///
    clang::SourceLocation getNextAvailableLoc() const;

    ///\brief true if -fsyntax-only flag passed.
    ///
    bool isInSyntaxOnlyMode() const;

    ///\brief Shows the current version of the project.
    ///
    ///\returns The current svn revision (svn Id).
    ///
    static const char* getVersion();

    ///\brief Creates unique name that can be used for various aims.
    ///
    void createUniqueName(std::string& out);

    ///\brief Checks whether the name was generated by Interpreter's unique name
    /// generator.
    ///
    ///\param[in] name - The name being checked.
    ///
    ///\returns true if the name is generated.
    ///
    bool isUniqueName(llvm::StringRef name);

    ///\brief Adds multiple include paths separated by a delimter.
    ///
    ///\param[in] PathsStr - Path(s)
    ///\param[in] Delim - Delimiter to separate paths or NULL if a single path
    ///
    void AddIncludePaths(llvm::StringRef PathsStr, const char* Delim = ":");

    ///\brief Adds a single include path (-I).
    ///
     void AddIncludePath(llvm::StringRef PathsStr);

    ///\brief Prints the current include paths that are used.
    ///
    ///\param[out] incpaths - Pass in a llvm::SmallVector<std::string, N> with
    ///       sufficiently sized N, to hold the result of the call.
    ///\param[in] withSystem - if true, incpaths will also contain system
    ///       include paths (framework, STL etc).
    ///\param[in] withFlags - if true, each element in incpaths will be prefixed
    ///       with a "-I" or similar, and some entries of incpaths will signal
    ///       a new include path region (e.g. "-cxx-isystem"). Also, flags
    ///       defining header search behavior will be included in incpaths, e.g.
    ///       "-nostdinc".
    ///
    void GetIncludePaths(llvm::SmallVectorImpl<std::string>& incpaths,
                         bool withSystem, bool withFlags);

    ///\brief Prints the current include paths that are used.
    ///
    ///\param[in] S - stream to dump to or nullptr for default (cling::outs)
    ///
    void DumpIncludePath(llvm::raw_ostream* S = nullptr);

    ///\brief Dump various internal data.
    ///
    ///\param[in] what - which data to dump. 'undo', 'ast', 'asttree'
    ///\param[in] filter - optional argument to filter data with.
    ///
    void dump(llvm::StringRef what, llvm::StringRef filter);

    ///\brief Store the interpreter state in files
    /// Store the AST, the included files and the lookup tables
    ///
    ///\param[in] name - The name of the files where the state will
    /// be printed
    ///
    void storeInterpreterState(const std::string& name) const;

    ///\brief Compare the actual interpreter state with the one stored
    /// previously.
    ///
    ///\param[in] name - The name of the previously stored file
    ///
    void compareInterpreterState(const std::string& name) const;

    ///\brief Print the included files in a temporary file
    ///
    ///\param[in] out - The output stream to be printed into.
    ///
    void printIncludedFiles (llvm::raw_ostream& out) const;

    ///\brief Compiles the given input.
    ///
    /// This interface helps to run everything that cling can run. From
    /// declaring header files to running or evaluating single statements.
    /// Note that this should be used when there is no idea of what kind of
    /// input is going to be processed. Otherwise if is known, for example
    /// only header files are going to be processed it is much faster to run the
    /// specific interface for doing that - in the particular case - declare().
    ///
    ///\param[in] input - The input to be compiled.
    ///\param[in,out] V - The result of the evaluation of the input. Must be
    ///       initialized to point to the return value's location if the
    ///       expression result is an aggregate.
    ///\param[out] T - The cling::Transaction of the compiled input.
    ///\param[in] disableValuePrinting - Whether to echo the expression result.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult process(const std::string& input, Value* V = 0,
                              Transaction** T = 0,
                              bool disableValuePrinting = false);

    ///\brief Parses input line, which doesn't contain statements. No code
    /// generation is done.
    ///
    /// Same as declare without codegening. Useful when a library is loaded and
    /// the header files need to be imported.
    ///
    ///\param[in] input - The input containing the declarations.
    ///\param[out] T - The cling::Transaction of the parsed input.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult parse(const std::string& input,
                            Transaction** T = 0) const;

    ///\brief Looks for a already generated PCM for the given header file and
    /// loads it.
    ///
    ///\param[in] headerFile - The header file for which a module should be
    ///                        loaded.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult loadModuleForHeader(const std::string& headerFile);

    ///\brief Parses input line, which doesn't contain statements. Code
    /// generation needed to make the module functional.
    ///
    /// Same as declare without most of the codegening.  Only a few
    /// things, like inline function are codegened.  Useful when a
    /// library is loaded and the header files need to be imported.
    ///
    ///\param[in] input - The input containing the declarations.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult parseForModule(const std::string& input);

    ///\brief Code completes user input.
    ///
    /// The interface circumvents the most of the extra work necessary to
    /// code complete code.
    ///
    /// @param[in] line - The input containing the string to be completed.
    /// @param[in] cursor - The offset for the completion point.
    /// @param[out] completions - The results for teh completion
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult codeComplete(const std::string& line, size_t& cursor,
                                   std::vector<std::string>& completions) const;

    ///\brief Compiles input line, which doesn't contain statements.
    ///
    /// The interface circumvents the most of the extra work necessary to
    /// compile and run statements.
    ///
    /// @param[in] input - The input containing only declarations (aka
    ///                    Top Level Declarations)
    /// @param[out] T - The cling::Transaction of the input
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult declare(const std::string& input, Transaction** T = 0);

    ///\brief Compiles input line, which contains only expressions.
    ///
    /// The interface circumvents the most of the extra work necessary extract
    /// the declarations from the input.
    ///
    /// @param[in] input - The input containing only expressions
    /// @param[in,out] V - The value of the executed input. Must be
    ///       initialized to point to the return value's location if the
    ///       expression result is an aggregate.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult evaluate(const std::string& input, Value& V);

    ///\brief Compiles input line, which contains only expressions and prints
    /// out the result of its execution.
    ///
    /// The interface circumvents the most of the extra work necessary extract
    /// the declarations from the input.
    ///
    /// @param[in] input - The input containing only expressions.
    /// @param[in,out] V - The value of the executed input. Must be
    ///       initialized to point to the return value's location if the
    ///       expression result is an aggregate.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult echo(const std::string& input, Value* V = 0);

    ///\brief Compiles input line and runs.
    ///
    /// The interface is the fastest way to compile and run a statement or
    /// expression. It just wraps the input into a function definition and runs
    /// that function, without any other "magic".
    ///
    /// @param[in] input - The input containing only expressions.
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult execute(const std::string& input);

    ///\brief Generates code for all Decls of a transaction.
    ///
    /// @param[in] T - The cling::Transaction that contains the declarations and
    ///                the compilation/generation options. Takes ownership!
    ///
    ///\returns Whether the operation was fully successful.
    ///
    CompilationResult emitAllDecls(Transaction* T);

    ///\brief Looks up a file or library according to the current interpreter
    /// include paths and system include paths.
    ///\param[in] file - The name of the file.
    ///
    ///\returns the canonical path to the file or library or empty string if not
    /// found.
    ///
    std::string lookupFileOrLibrary(llvm::StringRef file);

    ///\brief Loads a shared library.
    ///
    ///\param [in] filename - The file to loaded.
    ///\param [in] lookup - Whether to try to resolve the filepath
    ///
    ///\returns kMoreInputExpected is returned when file could not be found
    /// otherwise kSuccess or kFailure
    ///
    CompilationResult loadLibrary(const std::string& filename,
                                  bool lookup = true);

    ///\brief Loads header file
    ///
    ///\param [in] filename - The file to loaded.
    ///\param [out] T -  Transaction containing the loaded file.
    ///\returns result of the compilation.
    ///
    CompilationResult loadHeader(const std::string& filename,
                                 Transaction** T = 0);

    ///\brief Loads header file or shared library.
    ///
    ///\param [in] filename - The file to be loaded.
    ///\param [in] allowSharedLib - Whether to try to load the file as shared
    ///                             library.
    ///\param [out] T -  Transaction containing the loaded file.
    ///\returns result of the compilation.
    ///
    CompilationResult loadFile(const std::string& filename,
                               bool allowSharedLib = true,
                               Transaction** T = 0);

    ///\brief Unloads (forgets) a transaction from AST and JITed symbols.
    ///
    /// If one of the declarations caused error in clang it is rolled back from
    /// the AST. This is essential feature for the error recovery subsystem.
    /// This is also a key entry point for the code unloading.
    ///
    ///\param[in] T - the transaction to unload.
    ///
    void unload(Transaction& T);

    ///\brief Unloads (forgets) given number of transactions.
    ///
    ///\param[in] numberOfTransactions - how many transactions to revert
    ///                                  starting from the last.
    ///
    void unload(unsigned numberOfTransactions);

    void runAndRemoveStaticDestructors();
    void runAndRemoveStaticDestructors(unsigned numberOfTransactions);

    bool isPrintingDebug() const { return m_PrintDebug; }
    void enablePrintDebug(bool print = true) { m_PrintDebug = print; }

    void enableDynamicLookup(bool value = true);
    bool isDynamicLookupEnabled() const { return m_DynamicLookupEnabled; }

    bool isRawInputEnabled() const { return m_RawInputEnabled; }
    void enableRawInput(bool raw = true) { m_RawInputEnabled = raw; }

    int getDefaultOptLevel() const { return m_OptLevel; }
    void setDefaultOptLevel(int optLevel) { m_OptLevel = optLevel; }

    clang::CompilerInstance* getCI() const;
    clang::CompilerInstance* getCIOrNull() const;
    clang::Sema& getSema() const;
    clang::DiagnosticsEngine& getDiagnostics() const;

    ///\brief Create suitable default compilation options.
    CompilationOptions makeDefaultCompilationOpts() const;

    //FIXME: This must be in InterpreterCallbacks.
    void installLazyFunctionCreator(void* (*fp)(const std::string&));

    //FIXME: Lets the IncrementalParser run static inits on transaction
    // completed. Find a better way.
    ExecutionResult executeTransaction(Transaction& T);

    ///\brief Evaluates given expression within given declaration context.
    ///
    ///\param[in] expr - The expression.
    ///\param[in] DC - The declaration context in which the expression is going
    ///                to be evaluated.
    ///\param[in] ValuePrinterReq - Whether the value printing is requested.
    ///
    ///\returns The result of the evaluation if the expression.
    ///
    Value Evaluate(const char* expr, clang::DeclContext* DC,
                            bool ValuePrinterReq = false);

    ///\brief Interpreter callbacks accessors.
    /// Note that this class takes ownership of any callback object given to it.
    ///
    void setCallbacks(std::unique_ptr<InterpreterCallbacks> C);
    const InterpreterCallbacks* getCallbacks() const {return m_Callbacks.get();}
    InterpreterCallbacks* getCallbacks() { return m_Callbacks.get(); }

    const DynamicLibraryManager* getDynamicLibraryManager() const {
      return m_DyLibManager.get();
    }
    DynamicLibraryManager* getDynamicLibraryManager() {
      return m_DyLibManager.get();
    }

    const Transaction* getFirstTransaction() const;
    const Transaction* getLastTransaction() const;
    const Transaction* getCurrentTransaction() const;

    ///\brief Returns the current or last Transaction.
    ///
    const Transaction* getLatestTransaction() const;

    ///\brief Returns a reference to a Transaction known to contain std::string.
    ///
    const Transaction*& getStdStringTransaction() const {
      return m_CachedTrns[kStdStringTransaction];
    }

    ///\brief Compile extern "C" function and return its address.
    ///
    ///\param[in] name - function name
    ///\param[in] code - function definition, must contain 'extern "C"'
    ///\param[in] ifUniq - only compile this function if no function
    /// with the same name exists, else return the existing address
    ///\param[in] withAccessControl - whether to enforce access restrictions
    ///
    ///\returns the address of the function or 0 if the compilation failed.
    void* compileFunction(llvm::StringRef name, llvm::StringRef code,
                          bool ifUniq = true, bool withAccessControl = true);

    ///\brief Compile (and cache) destructor calls for a record decl. Used by ~Value.
    /// They are of type extern "C" void()(void* pObj).
    void* compileDtorCallFor(const clang::RecordDecl* RD);

    ///\brief Gets the address of an existing global and whether it was JITted.
    ///
    /// JIT symbols might not be immediately convertible to e.g. a function
    /// pointer as their call setup is different.
    ///
    ///\param[in]  D       - the global's Decl to find
    ///\param[out] fromJIT - whether the symbol was JITted.
    void* getAddressOfGlobal(const clang::GlobalDecl& D,
                             bool* fromJIT = 0) const;

    ///\brief Gets the address of an existing global and whether it was JITted.
    ///
    /// JIT symbols might not be immediately convertible to e.g. a function
    /// pointer as their call setup is different.
    ///
    ///\param[in]  SymName - the name of the global to search
    ///\param[out] fromJIT - whether the symbol was JITted.
    ///
    void* getAddressOfGlobal(llvm::StringRef SymName, bool* fromJIT = 0) const;

    ///\brief Get a given macro definition by name.
    ///
    ///\param[in]  Name - the name of the macro to look for
    ///
    ///\returns the MacroInfo if the macro was defined, otherwise null
    ///
    const clang::MacroInfo* getMacro(llvm::StringRef Name) const;

    ///\brief Get a given macro value by name.
    ///
    ///\param[in]  Name - the name of the macro to look for
    ///\param[out] Strip - characters to remove from the value.
    ///
    ///\returns the macro's value if the macro was defined, otherwise empty
    ///
    std::string getMacroValue(llvm::StringRef Name,
                              const char* Strip = "\"") const;

    ///\brief Add an atexit function.
    ///
    ///\param[in] Func - Function to be called.
    ///\param[in] Arg - argument passed to the function.
    ///
    void AddAtExitFunc(void (*Func) (void*), void* Arg);

    ///\brief Forwards to cling::IncrementalExecutor::addModule.
    ///
    void addModule(llvm::Module* module, int OptLevel);

    void GenerateAutoloadingMap(llvm::StringRef inFile, llvm::StringRef outFile,
                                bool enableMacros = false, bool enableLogs = true);

    void forwardDeclare(Transaction& T, clang::Preprocessor& P,
                        clang::ASTContext& Ctx,
                        llvm::raw_ostream& out,
                        bool enableMacros = false,
                        llvm::raw_ostream* logs = 0,
                        IgnoreFilesFunc_t ignoreFiles =
                          [](const clang::PresumedLoc&) { return false;}) const;

    friend class runtime::internal::LifetimeHandler;
  };
} // namespace cling

#endif // CLING_INTERPRETER_H
