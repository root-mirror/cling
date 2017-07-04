//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_PLATFORM_H
#define CLING_PLATFORM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <string>

#ifdef LLVM_ON_WIN32
#include <array>
#endif

namespace llvm {
  class raw_ostream;
}

namespace cling {
namespace utils {
namespace platform {

  ///\brief Returns the current working directory
  ///
  std::string GetCwd();

  ///\brief Get the system library paths
  ///
  /// \returns true on success false otherwise
  ///
  bool GetSystemLibraryPaths(llvm::SmallVectorImpl<std::string>& Paths);

  ///\brief Returns a normalized version of the given Path
  ///
  std::string NormalizePath(const std::string& Path);

  ///\brief Open a handle to a shared library. On Unix the lib is opened with
  /// RTLD_LAZY|RTLD_GLOBAL flags.
  ///
  /// \param [in] Path - Library to open
  /// \param [out] Err - Write errors to this string when given
  ///
  /// \returns the library handle
  ///
  const void* DLOpen(const std::string& Path, std::string* Err = nullptr);

  ///\brief Look for given symbol in all modules loaded by the current process
  ///
  /// \returns The adress of the symbol or null if not found
  ///
  const void* DLSym(const std::string& Name, std::string* Err = nullptr);

  ///\brief Close a handle to a shared library.
  ///
  /// \param [in] Lib - Handle to library from previous call to DLOpen
  /// \param [out] Err - Write errors to this string when given
  ///
  /// \returns the library handle
  ///
  void DLClose(const void* Lib, std::string* Err = nullptr);

  ///\brief Demangle the given symbol name
  ///
  /// \returns The demangled name or an empty string
  ///
  std::string Demangle(const std::string& Symbol);

  ///\brief Return true if the given pointer is in a valid memory region.
  ///
  bool IsMemoryValid(const void *P);

  ///\brief Invoke a command and read it's output.
  ///
  /// \param [in] Cmd - Command and arguments to invoke.
  /// \param [out] Buf - Buffer to write output to.
  /// \param [in] StdErrToStdOut - Redirect stderr to stdout.
  ///
  /// \returns whether any output was written to Buf
  ///
  bool Popen(const std::string& Cmd, llvm::SmallVectorImpl<char>& Buf,
             bool StdErrToStdOut = false);

#if defined(LLVM_ON_UNIX)

#if defined(__APPLE__)

inline namespace osx {
  ///\brief Get a path to an OSX SDK that can be used for -isysroot. Matches
  ///  1. Version matching the running system
  ///  2. Version that cling was compiled
  ///  3. Highest installed version
  ///
  /// \param [out] SysRoot - The path to the SDK
  /// \param [in] Verbose - Log progress
  ///
  struct MacOSSDK {
    llvm::raw_ostream* Verbose;
    MacOSSDK(const char[1], llvm::raw_ostream* V = nullptr) : Verbose(V) {}
    bool getISysRoot(std::string& SysRoot);
  };

} // namespace osx
using SDK = MacOSSDK;

#else  // __linux__

struct EmptySDK { EmptySDK(const char[1], llvm::raw_ostream* = nullptr) {} };
using SDK = EmptySDK;

#endif // __linux__

#elif defined(LLVM_ON_WIN32)

inline namespace windows {

  ///\brief Get an error message from the last Windows API
  ///
  /// \param [in] Prefix - Prefix the message with this (ex. API call name)
  ///
  /// \returns true if ::GetLastError returned an error code
  ///
  bool GetLastErrorAsString(std::string& ErrStr, const char* Prefix = nullptr);

  ///\brief Reports the last Windows API error (currently to cling::errs)
  ///
  /// \param [in] Prefix - Prefix the message with this
  ///
  bool ReportLastError(const char* Prefix = nullptr);

  ///\brief Return true if a given Path is a dynamic library
  ///
  bool IsDLL(const std::string& Path);

  /// \brief Read registry string.
  /// This also supports a means to look for high-versioned keys by use
  /// of a $VERSION placeholder in the key path.
  /// $VERSION in the key path is a placeholder for the version number,
  /// causing the highest value path to be searched for and used.
  /// I.e. "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\$VERSION".
  /// There can be additional characters in the component.  Only the numberic
  /// characters are compared.
  ///
  /// \param [in] Key - Key to lookup
  /// \param [in] ValueName - Value to lookup
  /// \param [out] Value - The value of the given key-value pair
  /// \param [out] VersKey - The value that $VERSION is equal to for Value.
  ///
  /// \returns true if key-value existed and was read into Value
  ///
  bool GetSystemRegistryString(const char* Key, const char* ValueName,
                               std::string& Value, std::string* VersKey = 0);

  ///\brief Get paths to installed VisualStudio directory matching:
  ///  1. Version that cling was compiled
  ///  2. Version that shell is initialized to
  ///  3. Highest installed version
  class WindowsSDK {
    enum { kNumEnvVarsIn = 5, kNumSavedEnvVars = 6 };
    typedef std::array<const char* const, kNumEnvVarsIn> EnvBlock;

    // Cached environment variables that may be temporarily overwritten.
    std::array<std::string*, kNumSavedEnvVars> m_Env = {};

    // Whether the environment variables need to be rest
    bool m_Reset;

  public:
    ///\brief SDK version major 7, 8, 10
    ///
    uint16_t Major; // Should be uint8_t, but MSVC doesn't support %hhu!

    ///\brief SDK root directory.
    ///
    std::string Root;

    ///\brief StdLib include directories (#include <stdio.h>)
    ///
    llvm::SmallVector<std::string, 2> StdInclude;

    ///\brief Windows include directories (#include <Windows.h>)
    ///
    llvm::SmallVector<std::string, 3> SdkIncludes;

    ///\brief Whether the environment variables are correct for clang to setup
    /// include directories properly.
    ///
    bool UsingEnv() const { return StdInclude.empty() && SdkIncludes.empty(); }

    ///\brief Get the Windows SDK directories. On failure Major will be 0.
    ///
    ///\param [in] Env - The CLING_VStudioEnv block.
    ///\param [in] Verbose - Log information to this stream.
    ///
    WindowsSDK(EnvBlock Env, llvm::raw_ostream* Verbose = nullptr);
    ~WindowsSDK();
  };

  ///\brief Runtime override for _CxxThrowException in Interpreter.
  //
  __declspec(noreturn) void __stdcall ClingRaiseSEHException(void*, void*);

  void RegisterEHFrames(uint8_t* Addr, size_t Size, uintptr_t BaseAddr,
                        bool Block);

  void DeRegisterEHFrames(uint8_t* Addr, size_t Size);

} // namespace windows
using SDK = WindowsSDK;
#endif // LLVM_ON_WIN32

} // namespace platform
} // namespace utils
namespace platform = utils::platform;
} // namespace cling

#endif // CLING_PLATFORM_H
