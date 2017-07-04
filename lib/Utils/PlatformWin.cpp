//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Utils/Platform.h"

#if defined(LLVM_ON_WIN32)

#include "Windows/MSVCSetupApi.h"
#include "cling/Utils/Output.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <map>
#include <sstream>
#include <stdlib.h>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
 #define NOGDI
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <Windows.h>
#include <Psapi.h>   // EnumProcessModulesEx
#include <direct.h>  // _getcwd
#include <shlobj.h>  // SHGetFolderPath
#pragma comment(lib, "Advapi32.lib")

extern "C" char* __unDName(char *demangled, const char *mangled, int out_len,
                           void * (* pAlloc )(size_t), void (* pFree )(void *),
                           unsigned short int flags);

#define MAX_PATHC (MAX_PATH + 1)

namespace cling {
namespace utils {
namespace platform {

inline namespace windows {

static void GetErrorAsString(DWORD Err, std::string& ErrStr, const char* Prefix) {
  llvm::raw_string_ostream Strm(ErrStr);
  if (Prefix)
    Strm << Prefix << ": returned " << Err << " ";

  LPTSTR Message = nullptr;
  const DWORD Size = ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, Err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&Message, 0, nullptr);

  if (Size && Message) {
    Strm << Message;
    ::LocalFree(Message);
    ErrStr = llvm::StringRef(Strm.str()).rtrim().str();
  }
}

static void ReportError(DWORD Err, const char* Prefix) {
  std::string Message;
  GetErrorAsString(Err, Message, Prefix);
  cling::errs() << Err << '\n';
}

bool GetLastErrorAsString(std::string& ErrStr, const char* Prefix) {
  if (const DWORD Err = ::GetLastError()) {
    GetErrorAsString(Err, ErrStr, Prefix);
    return true;
  }
  return false;
}

bool ReportLastError(const char* Prefix) {
  if (const DWORD Err = ::GetLastError()) {
    ReportError(Err, Prefix);
    return true;
  }
  return false;
}

namespace {

// Taken from clang/lib/Driver/MSVCToolChain.cpp
static bool readFullStringValue(HKEY hkey, const char *valueName,
                                std::string &value) {
  std::wstring WideValueName;
  if (valueName && !llvm::ConvertUTF8toWide(valueName, WideValueName))
    return false;

  // First just query for the required size.
  DWORD valueSize = 0, type = 0;
  DWORD result = ::RegQueryValueExW(hkey, WideValueName.c_str(), NULL, &type,
                                    NULL, &valueSize);
  if (result == ERROR_SUCCESS) {
    if (type != REG_SZ || !valueSize)
      return false;

    llvm::SmallVector<wchar_t, MAX_PATHC> buffer;
    buffer.resize(valueSize/sizeof(wchar_t));
    result = ::RegQueryValueExW(hkey, WideValueName.c_str(), NULL, NULL,
                                reinterpret_cast<BYTE*>(&buffer[0]), &valueSize);
    if (result == ERROR_SUCCESS) {
      // String might be null terminated, which we don't want
      while (!buffer.empty() && buffer.back() == 0)
        buffer.pop_back();

      std::wstring WideValue(buffer.data(), buffer.size());
      // The destination buffer must be empty as an invariant of the conversion
      // function; but this function is sometimes called in a loop that passes
      // in the same buffer, however. Simply clear it out so we can overwrite it
      value.clear();
      return llvm::convertWideToUTF8(WideValue, value);
    }
  }

  ReportError(result, "RegQueryValueEx");
  return false;
}

} // anonymous namespace

bool GetSystemRegistryString(const char *keyPath, const char *valueName,
                             std::string& outValue, std::string* phValue) {
  HKEY hRootKey = NULL;
  const char* subKey = NULL;

  if (::strncmp(keyPath, "HKEY_CLASSES_ROOT", 17) == 0) {
    hRootKey = HKEY_CLASSES_ROOT;
    subKey = keyPath + 17;
  } else if (::strncmp(keyPath, "HKEY_USERS", 10) == 0) {
    hRootKey = HKEY_USERS;
    subKey = keyPath + 10;
  } else if (::strncmp(keyPath, "HKEY_LOCAL_MACHINE", 18) == 0) {
    hRootKey = HKEY_LOCAL_MACHINE;
    subKey = keyPath + 18;
  } else if (::strncmp(keyPath, "HKEY_CURRENT_USER", 17) == 0) {
    hRootKey = HKEY_CURRENT_USER;
    subKey = keyPath + 17;
  } else {
    return false;
  }
  // Accept HKEY_CLASSES_ROOT or HKEY_CLASSES_ROOT\\ as the key to lookup in
  switch (subKey[0]) {
    case '\\': ++subKey;
    case 0:    break;
    default:   return false; // HKEY_CLASSES_ROOT_MORE_STUFF ?
  }

  long lResult;
  bool returnValue = false;
  HKEY hKey = NULL;

  // If we have a $VERSION placeholder, do the highest-version search.
  if (const char *placeHolder = ::strstr(subKey, "$VERSION")) {
    char bestName[256];
    bestName[0] = '\0';

    const char *keyEnd = placeHolder - 1;
    const char *nextKey = placeHolder;
    // Find end of previous key.
    while ((keyEnd > subKey) && (*keyEnd != '\\'))
      keyEnd--;
    // Find end of key containing $VERSION.
    while (*nextKey && (*nextKey != '\\'))
      nextKey++;
    size_t partialKeyLength = keyEnd - subKey;
    char partialKey[256];
    if (partialKeyLength > sizeof(partialKey))
      partialKeyLength = sizeof(partialKey);
    ::strncpy(partialKey, subKey, partialKeyLength);
    partialKey[partialKeyLength] = '\0';
    HKEY hTopKey = NULL;
    lResult = ::RegOpenKeyExA(hRootKey, partialKey, 0,
                              KEY_READ | KEY_WOW64_32KEY, &hTopKey);
    if (lResult == ERROR_SUCCESS) {
      char keyName[256];
      // int bestIndex = -1;
      double bestValue = 0.0;
      DWORD size = sizeof(keyName) - 1;
      for (DWORD index = 0; ::RegEnumKeyExA(hTopKey, index, keyName, &size, NULL,
                                            NULL, NULL, NULL) == ERROR_SUCCESS;
           index++) {
        const char *sp = keyName;
        while (*sp && !isdigit(*sp))
          sp++;
        if (!*sp)
          continue;
        const char *ep = sp + 1;
        while (*ep && (isdigit(*ep) || (*ep == '.')))
          ep++;
        char numBuf[32];
        ::strncpy(numBuf, sp, sizeof(numBuf) - 1);
        numBuf[sizeof(numBuf) - 1] = '\0';
        double dvalue = ::strtod(numBuf, NULL);
        if (dvalue > bestValue) {
          // Test that InstallDir is indeed there before keeping this index.
          // Open the chosen key path remainder.
          ::strcpy(bestName, keyName);
          // Append rest of key.
          ::strncat(bestName, nextKey, sizeof(bestName) - 1);
          bestName[sizeof(bestName) - 1] = '\0';
          lResult = ::RegOpenKeyExA(hTopKey, bestName, 0,
                                    KEY_READ | KEY_WOW64_32KEY, &hKey);
          if (lResult == ERROR_SUCCESS) {
            if (readFullStringValue(hKey, valueName, outValue)) {
              // bestIndex = (int)index;
              bestValue = dvalue;
              if (phValue)
                *phValue = bestName;
              returnValue = true;
            }
            ::RegCloseKey(hKey);
          }
        }
        size = sizeof(keyName) - 1;
      }
      ::RegCloseKey(hTopKey);
    } else
      ReportError(lResult, "RegOpenKeyEx");
  } else {
    // If subKey is empty, then valueName is subkey, and we retreive that
    if (subKey[0]==0) {
      subKey = valueName;
      valueName = nullptr;
    }
    lResult = ::RegOpenKeyExA(hRootKey, subKey, 0, KEY_READ | KEY_WOW64_32KEY,
                              &hKey);
    if (lResult == ERROR_SUCCESS) {
      returnValue = readFullStringValue(hKey, valueName, outValue);
      if (phValue)
        phValue->clear();
      ::RegCloseKey(hKey);
    } else
      ReportError(lResult, "RegOpenKeyEx");
  }
  return returnValue;
}

bool IsDLL(const std::string& Path) {
  bool isDLL = false;
  HANDLE hFile = ::CreateFileA(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (hFile == INVALID_HANDLE_VALUE) {
    ReportLastError("CreateFile");
    return false;
  }
  HANDLE hFileMapping = ::CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0,
                                            NULL);
  if (hFileMapping == INVALID_HANDLE_VALUE) {
    ReportLastError("CreateFileMapping");
    ::CloseHandle(hFile);
    return false;
  }

  LPVOID lpFileBase = ::MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
  if (!lpFileBase) {
    ReportLastError("CreateFileMapping");
    ::CloseHandle(hFileMapping);
    ::CloseHandle(hFile);
    return false;
  }

  PIMAGE_DOS_HEADER pDOSHeader = static_cast<PIMAGE_DOS_HEADER>(lpFileBase);
  if (pDOSHeader->e_magic == IMAGE_DOS_SIGNATURE) {
    PIMAGE_NT_HEADERS pNTHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
        (PBYTE)lpFileBase + pDOSHeader->e_lfanew);
    if ((pNTHeader->Signature == IMAGE_NT_SIGNATURE) &&
        ((pNTHeader->FileHeader.Characteristics & IMAGE_FILE_DLL)))
      isDLL = true;
  }

  ::UnmapViewOfFile(lpFileBase);
  ::CloseHandle(hFileMapping);
  ::CloseHandle(hFile);
  return isDLL;
}

} // namespace windows

std::string GetCwd() {
  char Buffer[MAX_PATHC];
  if (::_getcwd(Buffer, sizeof(Buffer)))
    return Buffer;

  ::perror("Could not get current working directory");
  return std::string();
}

std::string NormalizePath(const std::string& Path) {
  char Buf[MAX_PATHC];
  if (const char* Result = ::_fullpath(Buf, Path.c_str(), sizeof(Buf)))
    return std::string(Result);

  ReportLastError("_fullpath");
  return std::string();
}

bool IsMemoryValid(const void *P) {
  MEMORY_BASIC_INFORMATION MBI;
  if (::VirtualQuery(P, &MBI, sizeof(MBI)) == 0) {
    ReportLastError("VirtualQuery");
    return false;
  }
  if (MBI.State != MEM_COMMIT)
    return false;
  return true;
}

const void* DLOpen(const std::string& Path, std::string* Err) {
  HMODULE dyLibHandle = ::LoadLibraryA(Path.c_str());
  if (!dyLibHandle && Err)
    GetLastErrorAsString(*Err, "LoadLibrary");

  return reinterpret_cast<void*>(dyLibHandle);
}

const void* DLSym(const std::string& Name, std::string* Err) {
 #ifdef _WIN64
  const DWORD Flags = LIST_MODULES_64BIT;
 #else
  const DWORD Flags = LIST_MODULES_32BIT;
 #endif

  DWORD Bytes;
  std::string ErrStr;
  llvm::SmallVector<HMODULE, 128> Modules;
  Modules.resize(Modules.capacity());
  if (::EnumProcessModulesEx(::GetCurrentProcess(), &Modules[0],
                           Modules.capacity_in_bytes(), &Bytes, Flags) != 0) {
    // Search the modules we got
    const DWORD NumNeeded = Bytes/sizeof(HMODULE);
    const DWORD NumFirst = Modules.size();
    if (NumNeeded < NumFirst)
      Modules.resize(NumNeeded);

    // In reverse so user loaded modules are searched first
    for (auto It = Modules.rbegin(), End = Modules.rend(); It < End; ++It) {
      if (const void* Addr = ::GetProcAddress(*It, Name.c_str()))
            return Addr;
    }
    if (NumNeeded > NumFirst) {
      // The number of modules was too small to get them all, so call again
      Modules.resize(NumNeeded);
      if (::EnumProcessModulesEx(::GetCurrentProcess(), &Modules[0],
                               Modules.capacity_in_bytes(), &Bytes, Flags) != 0) {
        for (DWORD i = NumNeeded-1; i > NumFirst; --i) {
          if (const void* Addr = ::GetProcAddress(Modules[i], Name.c_str()))
            return Addr;
        }
      } else if (Err)
        GetLastErrorAsString(*Err, "EnumProcessModulesEx");
    }
  } else if (Err)
    GetLastErrorAsString(*Err, "EnumProcessModulesEx");

  return nullptr;
}

void DLClose(const void* Lib, std::string* Err) {
  if (::FreeLibrary(reinterpret_cast<HMODULE>(const_cast<void*>(Lib))) == 0) {
    if (Err)
      GetLastErrorAsString(*Err, "FreeLibrary");
  }
}

bool GetSystemLibraryPaths(llvm::SmallVectorImpl<std::string>& Paths) {
  char Buf[MAX_PATHC];
  // Generic form of C:\Windows\System32
  HRESULT result = ::SHGetFolderPathA(NULL, CSIDL_FLAG_CREATE | CSIDL_SYSTEM,
                                      NULL, SHGFP_TYPE_CURRENT, Buf);
  if (result != S_OK) {
    ReportError(result, "SHGetFolderPathA");
    return false;
  }
  Paths.push_back(Buf);
  Buf[0] = 0; // Reset Buf.
  
  // Generic form of C:\Windows
  result = ::SHGetFolderPathA(NULL, CSIDL_FLAG_CREATE | CSIDL_WINDOWS,
                              NULL, SHGFP_TYPE_CURRENT, Buf);
  if (result != S_OK) {
    ReportError(result, "SHGetFolderPathA");
    return false;
  }
  Paths.push_back(Buf);
  return true;
}

static void CloseHandle(HANDLE H) {
  if (::CloseHandle(H) == 0)
    ReportLastError("CloseHandle");
}

bool Popen(const std::string& Cmd, llvm::SmallVectorImpl<char>& Buf, bool RdE) {
  Buf.resize(0);

  SECURITY_ATTRIBUTES saAttr;
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  HANDLE Process = ::GetCurrentProcess();
  HANDLE hOutputReadTmp, hOutputRead, hOutputWrite, hErrorWrite;

  if (::CreatePipe(&hOutputReadTmp, &hOutputWrite, &saAttr, 0) == 0)
    return false;
  if (RdE) {
    if (::DuplicateHandle(Process, hOutputWrite, Process, &hErrorWrite, 0, TRUE,
                          DUPLICATE_SAME_ACCESS) == 0) {
      ReportLastError("DuplicateHandle");
      ::CloseHandle(hOutputReadTmp);
      ::CloseHandle(hOutputWrite);
      return false;
    }
  }

  // Create new output read handle. Set the Properties to FALSE, otherwise the
  // child inherits the properties and, as a result, non-closeable handles to
  // the pipes are created.
  if (::DuplicateHandle(Process, hOutputReadTmp, Process, &hOutputRead, 0,
                        FALSE, DUPLICATE_SAME_ACCESS) == 0) {
    ReportLastError("DuplicateHandle");
    ::CloseHandle(hOutputReadTmp);
    ::CloseHandle(hOutputWrite);
    if (RdE)
      ::CloseHandle(hErrorWrite);
    return false;
  }

  // Close inheritable copies of the handles you do not want to be inherited.
  CloseHandle(hOutputReadTmp);

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hOutputWrite;
  if (RdE)
    si.hStdError = hErrorWrite;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms682425%28v=vs.85%29.aspx
  // CreateProcessW can write back to second arguement, CreateProcessA not
  BOOL Result = ::CreateProcessA(NULL, (LPSTR)Cmd.c_str(), NULL, NULL, TRUE, 0,
                                 NULL, NULL, &si, &pi);
  DWORD Err = ::GetLastError();

  // Close pipe handles (do not continue to modify the parent) to make sure
  // that no handles to the write end of the output pipe are maintained in this
  // process or else the pipe will not close when the child process exits and
  // the ReadFile will hang.
  CloseHandle(hOutputWrite);
  if (RdE)
    CloseHandle(hErrorWrite);
  
  if (Result != 0) {
    DWORD dwRead;
    const size_t Chunk = Buf.capacity_in_bytes();
    while (true) {
      const size_t Len = Buf.size();
      Buf.resize(Len + Chunk);
      Result = ::ReadFile(hOutputRead, &Buf[Len], Chunk, &dwRead, NULL);
      if (!Result || !dwRead) {
        Err = ::GetLastError();
        if (Err != ERROR_BROKEN_PIPE)
          ReportError(Err, "ReadFile");
        Buf.resize(Len);
        break;
      }
      if (dwRead < Chunk)
        Buf.resize(Len + dwRead);
    }

    // Close process and thread handles.
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else
    ReportError(Err, "CreateProcess");

  CloseHandle(hOutputRead);

  return !Buf.empty();
}

std::string Demangle(const std::string& Symbol) {
  struct AutoFree {
    char* Str;
    AutoFree(char* Ptr) : Str(Ptr) {}
    ~AutoFree() { ::free(Str); };
  };
  AutoFree af(__unDName(0, Symbol.c_str(), 0, ::malloc, ::free, 0));
  return af.Str ? std::string(af.Str) : std::string();
}

namespace windows {
// Use less memory and store the function ranges to watch as a mapping
// between of BaseAddr to Ranges watched.
//
typedef std::vector<std::pair<DWORD, DWORD>> ImageRanges;
typedef std::map<uintptr_t, ImageRanges> ImageBaseMap;

ImageBaseMap& getImageBaseMap() {
  static ImageBaseMap sMap;
  return sMap;
}

// Merge overlaping ranges
static void MergeRanges(ImageRanges &Ranges) {
  std::sort(Ranges.begin(), Ranges.end());

  ImageRanges Merged;
  ImageRanges::iterator It = Ranges.begin();
  auto Current = *(It)++;
  while (It != Ranges.end()) {
    if (Current.second+1 < It->first) {
      Merged.push_back(Current);
      Current = *(It);
    } else
      Current.second = std::max(Current.second, It->second);
    ++It;
  }
  Merged.emplace_back(Current);
  Ranges.swap(Merged);
}

static uintptr_t FindEHFrame(uintptr_t Caller) {
  ImageBaseMap& Unwind = getImageBaseMap();
  for (auto&& Itr : Unwind) {
    const uintptr_t CurAddr = Itr.first;
    for (auto&& Rng : Itr.second) {
      if (Caller >=(CurAddr + Rng.first) &&
          Caller <= (CurAddr + Rng.second)) {
        return CurAddr;
      }
    }
  }
  return 0;
}

void RegisterEHFrames(uint8_t* A, size_t Size, uintptr_t BaseAddr, bool Block) {
  assert(getImageBaseMap().find(DWORD64(A)) == getImageBaseMap().end());

  PRUNTIME_FUNCTION RFunc = reinterpret_cast<PRUNTIME_FUNCTION>(A);
  const size_t N = Size / sizeof(RUNTIME_FUNCTION);
  ImageBaseMap::mapped_type &Ranges = getImageBaseMap()[BaseAddr];
  if (Block) {
    // Merge all unwind adresses into a single contiguous block
    Ranges.emplace_back(std::numeric_limits<DWORD>::max(),
                        std::numeric_limits<DWORD>::min());
    ImageRanges::value_type& Range = Ranges.front();
    for (PRUNTIME_FUNCTION It = RFunc, End = RFunc +N; It < End; ++It) {
      Range.first = std::min(Range.first, It->BeginAddress);
      Range.second = std::max(Range.second, It->EndAddress);
    }
  } else {
    for (PRUNTIME_FUNCTION It = RFunc, End = RFunc +N; It < End; ++It)
      Ranges.emplace_back(It->BeginAddress, It->EndAddress);

    // Initial sort and merge
    MergeRanges(Ranges);
  }

  ::RtlAddFunctionTable(RFunc, N, BaseAddr);
}

void DeRegisterEHFrames(uint8_t* Addr, size_t Size) {
  assert(getImageBaseMap().find(DWORD64(Addr)) != getImageBaseMap().end());

  ImageBaseMap& Unwind = getImageBaseMap();
  Unwind.erase(Unwind.find(DWORD64(Addr)));
  ::RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(Addr));
}

// Adapted from VisualStudio/VC/crt/src/vcruntime/throw.cpp
#ifdef _WIN64
 #define _EH_RELATIVE_OFFSETS 1
#endif
// The NT Exception # that we use
#define EH_EXCEPTION_NUMBER ('msc' | 0xE0000000)
// The magic # identifying this version
#define EH_MAGIC_NUMBER1 0x19930520
#define EH_PURE_MAGIC_NUMBER1 0x01994000
// Number of parameters in exception record
#define EH_EXCEPTION_PARAMETERS 4

// A generic exception record
struct EHExceptionRecord {
  DWORD ExceptionCode;
  DWORD ExceptionFlags;     // Flags determined by NT
  _EXCEPTION_RECORD* ExceptionRecord; // Extra exception record (unused)
  void* ExceptionAddress;   // Address at which exception occurred
  DWORD NumberParameters;   // No. of parameters = EH_EXCEPTION_PARAMETERS
  struct EHParameters {
    DWORD  magicNumber;           // = EH_MAGIC_NUMBER1
    void *pExceptionObject;       // Pointer to the actual object thrown
    struct ThrowInfo *pThrowInfo; // Description of thrown object
#if _EH_RELATIVE_OFFSETS
    DWORD64 pThrowImageBase;      // Image base of thrown object
#endif
  } params;
};

__declspec(noreturn) void
__stdcall ClingRaiseSEHException(void* CxxExcept, void* Info) {

  uintptr_t Caller;
  static_assert(sizeof(Caller) == sizeof(PVOID), "Size mismatch");

  USHORT Frames = CaptureStackBackTrace(1, 1,(PVOID*)&Caller, NULL);
  assert(Frames && "No frames captured");

  const DWORD64 BaseAddr = FindEHFrame(Caller);
  if (BaseAddr == 0)
    _CxxThrowException(CxxExcept, (_ThrowInfo*) Info);

  // A generic exception record
  EHExceptionRecord Exception = {
    EH_EXCEPTION_NUMBER,      // Exception number
    EXCEPTION_NONCONTINUABLE, // Exception flags (we don't do resume)
    nullptr,                  // Additional record (none)
    nullptr,                  // Address of exception (OS fills in)
    EH_EXCEPTION_PARAMETERS,  // Number of parameters
    {
      EH_MAGIC_NUMBER1,
      CxxExcept,
      (struct ThrowInfo*)Info,
#if _EH_RELATIVE_OFFSETS
      BaseAddr
#endif
    }
  };

  // const ThrowInfo* pTI = (const ThrowInfo*)Info;

#ifdef THROW_ISWINRT
  if (pTI && (THROW_ISWINRT((*pTI)))) {
    // The pointer to the ExceptionInfo structure is stored sizeof(void*)
    // infront of each WinRT Exception Info.
    ULONG_PTR* EPtr = *reinterpret_cast<ULONG_PTR**>(CxxExcept);
    EPtr--;

    WINRTEXCEPTIONINFO** ppWei = reinterpret_cast<WINRTEXCEPTIONINFO**>(EPtr);
    pTI = (*ppWei)->throwInfo;
    (*ppWei)->PrepareThrow(ppWei);
  }
#endif

  // If the throw info indicates this throw is from a pure region,
  // set the magic number to the Pure one, so only a pure-region
  // catch will see it.
  //
  // Also use the Pure magic number on Win64 if we were unable to
  // determine an image base, since that was the old way to determine
  // a pure throw, before the TI_IsPure bit was added to the FuncInfo
  // attributes field.
  if (Info != nullptr) {
#ifdef THROW_ISPURE
    if (THROW_ISPURE(*pTI))
      Exception.params.magicNumber = EH_PURE_MAGIC_NUMBER1;
#if _EH_RELATIVE_OFFSETS
    else
#endif // _EH_RELATIVE_OFFSETS
#endif // THROW_ISPURE

#if 0 && _EH_RELATIVE_OFFSETS
    if (Exception.params.pThrowImageBase == 0)
      Exception.params.magicNumber = EH_PURE_MAGIC_NUMBER1;
#endif // _EH_RELATIVE_OFFSETS
  }

// Hand it off to the OS:
#if defined(_M_X64) && defined(_NTSUBSET_)
  RtlRaiseException((PEXCEPTION_RECORD)&Exception);
#else
  RaiseException(Exception.ExceptionCode, Exception.ExceptionFlags,
                 Exception.NumberParameters, (PULONG_PTR)&Exception.params);
#endif
}
} // namespace windows

} // namespace platform
} // namespace utils
} // namespace cling

#endif // LLVM_ON_WIN32
