//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "MSVCSetupApi.h"

#include "cling/Utils/Platform.h"
#include "cling/Utils/Output.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#ifdef USE_MSVC_SETUP_API

#include "llvm/Support/COM.h"
#include "llvm/Support/ConvertUTF.h"

_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));

#endif

namespace cling {
namespace utils {
namespace platform {
namespace windows {

namespace {

static constexpr int GetVisualStudioVersionCompiledWith() {
#if (_MSC_VER < 1900)
  return (_MSC_VER / 100) - 6;
#elif (_MSC_VER < 1910)
  return 14;
#elif (_MSC_VER == 1910)
  return 15;
#else
  #error "Unsupported/Untested _MSC_VER"
#endif
}

static void TrimBackslashes(std::string& Path) {
  while (!Path.empty() && Path.back() == '\\')
    Path.pop_back();
}

class SDKHelper {
  const unsigned m_VSVersion = GetVisualStudioVersionCompiledWith();
  mutable const char* m_VerboseMsg = nullptr;
  llvm::raw_ostream* m_Verbose;

  void logSearch(const char* Name, const std::string& Value,
                 const char* Found = nullptr) const {
    if (!m_Verbose)
      return;

    if (Found)
      (*m_Verbose) << "Found " << Name << " '" << Value << "' that matches "
                   << Found << " version\n";
    else
      (*m_Verbose) << Name << " '" << Value << "' not found.\n";
  }

  void logVersion(const char* Msg, const char* VerStr, uint64_t VerNum) const {
    if (!m_Verbose || !VerStr)
      return;
    
    (*m_Verbose) << Msg << " version " << VerStr << ',' << VerNum << ".\n";
  }

  bool findViaSetupConfig(std::string& Path, const char* FindVersion) const {
  #ifndef USE_MSVC_SETUP_API
    return false;
  #else
    // FIXME: This really should be done once in the top-level program's main
    // function, as it may have already been initialized with a different
    // threading model otherwise.
    llvm::sys::InitializeCOMRAII COM(llvm::sys::COMThreadingMode::SingleThreaded);
    HRESULT HR;

    // _com_ptr_t will throw a _com_error if a COM calls fail.
    // The LLVM coding standards forbid exception handling, so we'll have to
    // stop them from being thrown in the first place.
    // The destructor will put the regular error handler back when we leave
    // this scope.
    struct SuppressCOMErrorsRAII {
      static void __stdcall handler(HRESULT hr, IErrorInfo *perrinfo) {}

      SuppressCOMErrorsRAII() { _set_com_error_handler(handler); }

      ~SuppressCOMErrorsRAII() { _set_com_error_handler(_com_raise_error); }

    } COMErrorSuppressor;

    ISetupConfigurationPtr Query;
    HR = Query.CreateInstance(__uuidof(SetupConfiguration));
    if (FAILED(HR)) {
      logSearch("COM Object", "SetupConfiguration");
      return false;
    }

    IEnumSetupInstancesPtr EnumInstances;
    HR = ISetupConfiguration2Ptr(Query)->EnumAllInstances(&EnumInstances);
    if (FAILED(HR)) {
      logSearch("COM Object", "EnumInstances");
      return false;
    }

    ISetupInstancePtr Instance;
    HR = EnumInstances->Next(1, &Instance, nullptr);
    if (HR != S_OK) {
      logSearch("EnumInstances", "Next");
      return false;
    }

    uint64_t BestVersion = 0;
    if (FindVersion) {
      bstr_t VersionString(FindVersion);
      HR = ISetupHelperPtr(Query)->ParseVersion(VersionString, &BestVersion);
      if (FAILED(HR)) {
        if (m_Verbose)
          (*m_Verbose) << "Version '" << FindVersion
                       << "' could not be parsed.\n";
        return false;
      }
      logVersion("Looking for", FindVersion, BestVersion);
    }

    ISetupInstancePtr NewestInstance;
    do {
      bstr_t VersionString;
      uint64_t VersionNum;
      HR = Instance->GetInstallationVersion(VersionString.GetAddress());
      if (FAILED(HR))
        continue;
      HR = ISetupHelperPtr(Query)->ParseVersion(VersionString, &VersionNum);
      if (FAILED(HR))
        continue;
    
      if (FindVersion) {
        logVersion("Looking at", FindVersion, VersionNum);
        if (VersionNum == BestVersion) {
          NewestInstance = Instance;
          break;
        }
      } else if (!BestVersion || (VersionNum > BestVersion)) {
        NewestInstance = Instance;
        BestVersion = VersionNum;
      }
    } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);

    if (!NewestInstance) {
      logVersion("Could not find", FindVersion, BestVersion);
      return false;
    }

    logVersion("Found", FindVersion, BestVersion);

    bstr_t VCPathWide;
    HR = NewestInstance->ResolvePath(L"VC", VCPathWide.GetAddress());
    if (FAILED(HR)) {
      logSearch("Sub-directory", "VC");
      return false;
    }

    std::string VCRootPath;
    llvm::convertWideToUTF8(std::wstring(VCPathWide), VCRootPath);

    llvm::SmallString<256> ToolsVersionFilePath(VCRootPath);
    llvm::sys::path::append(ToolsVersionFilePath, "Auxiliary", "Build",
                            "Microsoft.VCToolsVersion.default.txt");

    auto ToolsVersionFile = llvm::MemoryBuffer::getFile(ToolsVersionFilePath);
    if (!ToolsVersionFile) {
      logSearch("Expected file", "Microsoft.VCToolsVersion.default.txt");
      return false;
    }

    llvm::SmallString<256> ToolchainPath(VCRootPath);
    llvm::sys::path::append(ToolchainPath, "Tools", "MSVC",
                            ToolsVersionFile->get()->getBuffer().rtrim());
    if (!llvm::sys::fs::is_directory(ToolchainPath)) {
      logSearch("Sub-directory", "Tools\\MSVC");
      return false;
    }

    Path = ToolchainPath.str();
    return true;
  #endif
  }


  // Chop of Remove from Base path and making sure it exists.
  bool getRootInstall(const std::string& Base, const char* Remove,
                      std::string& OutPath, const std::string& Key,
                      const char* Msg) const {
    // "C:\PathToVisualStudio" = "C:\PathToVisualStudio\VC\Tools" - "VC\Tools";
    OutPath = Base.substr(0, Base.rfind(Remove) + 1);
    if (!llvm::sys::fs::is_directory(OutPath)) {
      logSearch(Key.c_str(), OutPath);
      OutPath.clear();
      return false;
    }

    TrimBackslashes(OutPath);
    logSearch(Msg, Key, m_VerboseMsg);
    return true;
  }

  bool getVSRegistryString(int VSVersion, const char* Product,
                           std::string& OutPath) const {
    stdstrstream Strm;
    Strm << "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\" << Product << "\\"
         << VSVersion << ".0";

    std::string Val;
    const std::string K = Strm.str();
    if (!GetSystemRegistryString(K.c_str(), "InstallDir", Val) || Val.empty()) {
      logSearch("Registry", K);
      return false;
    }

    return getRootInstall(Val, "\\Common7\\IDE", OutPath, K, "Registry");
  }

  bool getVSEnvironmentPath(const std::string& Key, const char* Remove,
                            std::string& OutPath) const {
    const char* Val = ::getenv(Key.c_str());
    if (!Val) {
      logSearch("Environment", Key);
      return false;
    }

    return getRootInstall(Val, Remove, OutPath, Key, "Environment");
  }

  // Major.Minor.Micro version didn't exist, find a Major.Minor match.
  bool findBestVersion(std::string& OutPath, const std::string& SubVers) const {
    if (m_Verbose)
      logSearch("Matching version", OutPath);
    std::error_code EC;
    const auto MajMin = SubVers.substr(0, SubVers.find_last_of('.'));
    OutPath = OutPath.substr(0, OutPath.size() - SubVers.size());
    for (llvm::sys::fs::directory_iterator DirIt(OutPath, EC), DirEnd;
         DirIt != DirEnd && !EC; DirIt.increment(EC)) {
      const std::string& Entry = DirIt->path();
      if (llvm::sys::fs::is_directory(Entry) &&
          llvm::sys::path::filename(Entry).startswith(MajMin)) {
        OutPath = Entry;
        logSearch("compatible version", OutPath, "major & minor");
        return true;
      }
    }
    OutPath.clear();
    return false;
  }

  // Checks the registry for an installed VStudio that matches the passed in
  // version. If not found then checks if VSCOMNTOOLS environemntal variable.
  bool getVisualStudioVer(unsigned VSVersion, std::string& OutPath,
                          const std::string& VSVer, const char* CmdVer) const {
    // Visual Studio 2017 (and above?) won't add itself to the registry.
    if (VSVersion < 15) {
      if (getVSRegistryString(VSVersion, "VisualStudio", OutPath))
        return true;

      if (getVSRegistryString(VSVersion, "VCExpress", OutPath))
        return true;
    } else {
      if (findViaSetupConfig(OutPath, CmdVer))
        return true;
    }

    // FIXME: Should this be done first to provide a mechanism to force the
    // Visual Studio installation to be used?
    //
    // VSxxxCOMNTOOLS.
    stdstrstream Strm;
    Strm << "VS" << VSVersion * 10 << "COMNTOOLS";
    if (getVSEnvironmentPath(Strm.str(), "\\Common7\\Tools", OutPath)) {
      if (VSVersion >= 15) {
        using namespace llvm::sys;
        OutPath += "\\VC\\Tools\\MSVC\\" + VSVer;
        if (!fs::is_directory(OutPath) && !findBestVersion(OutPath, VSVer))
            return false;
      }
      return true;
    }

    return false;
  }

  // Find the most recent version of Universal CRT or Windows 10 SDK.
  // vcvarsqueryregistry.bat from Visual Studio 2015 sorts entries in the include
  // directory by name and uses the last one of the list.
  // So we compare entry names lexicographically to find the greatest one.
  bool getWindows10SDKVersion(std::string& SDKPath,
                              std::string& SDKVersion) const {
    // Save input SDKVersion to match, and clear SDKVersion for > comparsion
    std::string UcrtCompiledVers;
    UcrtCompiledVers.swap(SDKVersion);

    std::error_code EC;
    llvm::SmallString<MAX_PATH+1> IncludePath(SDKPath);
    llvm::sys::path::append(IncludePath, "Include");
    for (llvm::sys::fs::directory_iterator DirIt(IncludePath, EC), DirEnd;
         DirIt != DirEnd && !EC; DirIt.increment(EC)) {
      if (!llvm::sys::fs::is_directory(DirIt->path()))
        continue;
      llvm::StringRef Candidate = llvm::sys::path::filename(DirIt->path());
      // There could be subfolders like "wdf" in the "Include" directory, so only
      // test names that start with "10." or match input.
      const bool Match = Candidate == UcrtCompiledVers;
      if (Match || (Candidate.startswith("10.") && Candidate > SDKVersion)) {
        SDKPath = DirIt->path();
        Candidate.str().swap(SDKVersion);
        if (Match)
          return true;
      }
    }
    return !SDKVersion.empty();
  }

public:
  /// \brief Helper class to a Visual Studio installation. Some of this is
  /// redundant with code in clang itself, but the goals are different.  Clang
  /// will always try to get the most recent version installed, where as cling
  /// wants a version that best matches what it was compiled against.
  ///
  /// 1. Check the registry for values that match the version compiled with.
  /// 2. Check the VSxxxCOMNTOOLS environment variable, which should also be set
  ///    when just the build tools are installed (not the IDE).
  /// 3. Check VCINSTALLDIR (or VCToolsInstallDir >= 2017) for the location
  ///    of the active IDE dev environment.
  ///
  SDKHelper(llvm::raw_ostream* V) : m_Verbose(V) {}

  // CompiledEnv will be either the value from VCToolsInstallDir or VCINSTALLDIR
  bool getVisualStudioDir(const char* CompiledEnv, const char* CmdVers,
                          std::string& OutPath) const {
    // For VStudio 2017, make sure a matching version is installed
    std::string AppVers;
    if (m_VSVersion >= 15) {
      AppVers = CompiledEnv;
      TrimBackslashes(AppVers);
      AppVers = AppVers.substr(AppVers.find_last_of('\\') + 1);
    }

    // Try for the version compiled against first.
    m_VerboseMsg = m_Verbose ? "compiled" : nullptr;
    if (getVisualStudioVer(m_VSVersion, OutPath, AppVers, CmdVers))
      return true;

    // Check the VCINSTALLDIR or VCToolsInstallDir environment variables.
    const auto Names = m_VSVersion >= 15
                           ? std::make_pair("VCToolsInstallDir", "")
                           : std::make_pair("VCINSTALLDIR", "\\VC");
    if (getVSEnvironmentPath(Names.first, Names.second, OutPath)) {
      if (m_VSVersion >= 15) {
        if (OutPath.rfind(AppVers) + AppVers.size() != OutPath.size()) {
          if (!findBestVersion(OutPath, AppVers))
            return false;
        }
      }
      return true;
    }

    // Try for any other version we can get
    m_VerboseMsg = m_Verbose ? "highest" : nullptr;
    std::array<unsigned, 3>  kVers = { 15, 14, 12 }; // 11, 10, 9, 8, 0 };
    for (unsigned Vers : kVers) {
      if (Vers != m_VSVersion &&
          getVisualStudioVer(Vers, OutPath, AppVers, nullptr)) {
        return true;
      }
    }
    return false;
  }

  bool getWindowsSDKDir(std::string& WindowsSDK, uint16_t& Major) const {
    std::string SDKVers;
    if (GetSystemRegistryString("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\"
                                "Microsoft SDKs\\Windows\\$VERSION",
                                "InstallationFolder", WindowsSDK, &SDKVers)) {
      TrimBackslashes(WindowsSDK);
      std::sscanf(SDKVers.c_str(), "v%hu.", &Major);
      return true;
    } else if (m_Verbose)
      (*m_Verbose) << "Could not get Windows SDK path\n";
    return false;
  }

  bool getUniversalCRTSdkDir(std::string& Path, std::string& UCRTVers) const {
    // vcvarsqueryregistry.bat for Visual Studio 2015 queries the registry
    // for the specific key "KitsRoot10". So do we.
    if (!GetSystemRegistryString("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\"
                                 "Windows Kits\\Installed Roots", "KitsRoot10",
                                 Path))
      return false;

    return getWindows10SDKVersion(Path, UCRTVers);
  }

  unsigned VSVersion() const { return m_VSVersion; }
};

// Save an environmental variable and set or remove it.
static void SetEnvVar(const char* Name, const char* OldValue,
                      const char* NewValue, llvm::raw_ostream* Verbose,
                      std::string** SaveTo) {
  *SaveTo = new std::string(OldValue);
  if (Verbose) {
    if (NewValue[0])
      (*Verbose) << "      setting to '" << NewValue << "'\n";
    else
      (*Verbose) << "        removing\n";
  }
  ::_putenv_s(Name, NewValue);
}

// Check if an environmental an return if different than expected, optionally
// saving the old value and setting (or removing) to the new one.
static bool SaveAndSet(const char* Name, const char* Value,
                       llvm::raw_ostream* Verbose,
                       std::string** SaveTo = nullptr) {
  assert(Value && "Value must not be NULL.");
  assert((!SaveTo || !*SaveTo) && "String exists.");

  const char* const Env = ::getenv(Name);
  if (Verbose) {
    (*Verbose) << "Looking at '" << Name << "'\n";
    if (Value) (*Verbose) << "   compiled with '" << Value << "'\n";
    (*Verbose) << "  environment is '" << (Env ? Env : "") << "'\n";
  }

  if (Env) {
    const bool Same = strcmp(Env, Value) == 0;
    if (SaveTo && !Same) SetEnvVar(Name, Env, Value, Verbose, SaveTo);
    return Same;
  } else if (SaveTo && Value[0])
    SetEnvVar(Name, "", Value, Verbose, SaveTo);

  // Matching "" to "" does not count as real match.
  return false;
}

static void DirAppend(const std::string& Root,
                      llvm::SmallVectorImpl<std::string>& Dirs,
                      const llvm::Twine& Dir1, const llvm::Twine& Dir2 = "",
                      const llvm::Twine& Dir3 = "") {
  llvm::SmallString<MAX_PATH> Path(Root);
  llvm::sys::path::append(Path, Dir1, Dir2, Dir3);
  Dirs.push_back(Path.str());
}

enum {
  kUCRTVersion,
  kVisualStudioVersion,
  kVCINSTALLDIR,
  kVCToolsInstallDir,
  kVSCMD_VER,
  kINCLUDE,
  kNumVStudioKeys = kINCLUDE,
  kNumVSEnvVars
};

static const char* const kVStudioVars[kNumVSEnvVars] = {
  "UCRTVersion", "VisualStudioVersion", "VCINSTALLDIR", "VCToolsInstallDir",
  "VSCMD_VER", "INCLUDE"
};

} // anonymous namespace

WindowsSDK::WindowsSDK(std::array<const char* const, kNumVStudioKeys> SDKDirs,
                       llvm::raw_ostream* Verbose)
  : Major(0), m_Reset(false) {
  static_assert(SDKDirs.size() == kNumVStudioKeys, "End mismatch");
  static_assert(kNumSavedEnvVars == kNumVSEnvVars, "Size mismatch");

  // If the first two match, clang will set everything up properly anyway.
  bool VersionMatch = false;
  for (size_t I = kUCRTVersion, N = kVisualStudioVersion; I <= N; ++I) {
    VersionMatch = SaveAndSet(kVStudioVars[I], SDKDirs[I], Verbose);
    if (!VersionMatch)
      break;
  }

  const SDKHelper Helper(Verbose);
  if (!VersionMatch) {
    // Two possiblities: a clean cmd-line not setup with vcvars.bat, or worse
    // a cmd-line setup for a version of Visual Studio that doesn't match what
    // cling was compiled with.
    m_Reset = true;
    if (!llvm::sys::fs::is_directory(SDKDirs[kVCToolsInstallDir]) &&
        !llvm::sys::fs::is_directory(SDKDirs[kVCINSTALLDIR])) {
      // Nothing matches or exists matching how cling was built, do it by hand.
      if (Verbose)
        (*Verbose) << "Using registry to get system include paths.\n";

      const unsigned EnvVar =
          Helper.VSVersion() >= 15 ? kVCToolsInstallDir : kVCINSTALLDIR;

      // VCRuntime path, and possibly path to stdlib headers
      //
      // Call getVisualStudioDir with environment var stored at build time
      // for a hint if the path cannot be pulled from the Registry (2017).
      std::string VStudio;
      if (Helper.getVisualStudioDir(SDKDirs[EnvVar], SDKDirs[kVSCMD_VER],
                                    VStudio)) {
        TrimBackslashes(VStudio);
        // For 2017 (and above?) don't need to insert the "VC" directory.
        DirAppend(VStudio, StdInclude, EnvVar == kVCToolsInstallDir ? "" : "VC",
                  "include");
      }

      // The UCRT version compiled with.
      const char* const UcrtIn = SDKDirs[kUCRTVersion];
      const bool WantUCRT = UcrtIn && UcrtIn[0];
      std::string UCRTVersion;

      if (WantUCRT) {
        std::string UCRTPath;
        UCRTVersion = UcrtIn;
        if (Helper.getUniversalCRTSdkDir(UCRTPath, UCRTVersion)) {
          // UCRT appended to StdInclude (stdio.h, cstdio, etc.)
          TrimBackslashes(UCRTPath);
          DirAppend(UCRTPath, StdInclude, "ucrt");
        }
      }

      // SDK paths ... Windows.h
      if (Helper.getWindowsSDKDir(Root, Major)) {
        if (Major >= 8) {
          if (Major < 10)
            UCRTVersion.clear(); // PlatformSDK < 10 don't have a UCRTVersion
          else if (Verbose && WantUCRT && UCRTVersion.empty())
            (*Verbose) << "Could not get Universal SDK path\n";
          
          DirAppend(Root, SdkIncludes, "include", UCRTVersion, "shared");
          DirAppend(Root, SdkIncludes, "include", UCRTVersion, "um");
          DirAppend(Root, SdkIncludes, "include", UCRTVersion, "winrt");
        } else
          DirAppend(VStudio, SdkIncludes, "VC", "PlatformSDK", "Include");
      } else if (Verbose)
        (*Verbose) << "Could not get Windows or Universal SDK paths.\n";

#if 0
      // AddHostArguments passes -nostdinc which is a nicer way of blocking
      // clang doing setup again.
      //
      // Remove all environemnt variables to short-circuit clang liking
	  // the highest version of VisualStudio it can find.
      SaveAndSet(kVStudioVars[EnvVar], "CLING-FAKE", Verbose, &m_Env[EnvVar]);

      // Clear out all other environment variables that clang may use later
      // which are known to be incorrect or non-existant.
      for (size_t I = 0; I < kNumVSEnvVars; ++I) {
        if (I != EnvVar)
          SaveAndSet(kVStudioVars[I], "", Verbose, &m_Env[I]);
      }
#endif
    } else {
      if (Verbose)
        (*Verbose) << "Using stored environment for system include paths.\n";

      // What cling was built with exists, save all relevant environment vars,
      // set them to the stored values, and let clang sort it out.
      for (size_t I = 0; I < kNumVStudioKeys; ++I)
        SaveAndSet(kVStudioVars[I], SDKDirs[I], Verbose, &m_Env[I]);
      // Unset the INCLUDE environment variable, it is wrong for this setup.
      SaveAndSet(kVStudioVars[kINCLUDE], "", Verbose, &m_Env[kINCLUDE]);
      // Set this so that Major is filled in below.
      VersionMatch = true;
    }
  }

  if (VersionMatch)
    Helper.getWindowsSDKDir(Root, Major);
}

WindowsSDK::~WindowsSDK() {
  if (m_Reset) {
    for (size_t I = 0, N = m_Env.size(); I < N; ++I) {
      if (std::string* Value = m_Env[I]) {
        ::_putenv_s(kVStudioVars[I], Value->c_str());
        delete Value;
      }
    }
  }
}

}
}
}
}
