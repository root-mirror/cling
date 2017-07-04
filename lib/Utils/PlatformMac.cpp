//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// FIXME: This should probably be handled in CMake so as not to overwrite what
// the user may have specifically requested.
// Make sure MAC_OS_X_VERSION_MAX_ALLOWED is not user defined, so that it will
// match the SDK version we are compiling with.
#ifdef MAC_OS_X_VERSION_MAX_ALLOWED
 #undef MAC_OS_X_VERSION_MAX_ALLOWED
#endif

#include "cling/Utils/Platform.h"

#ifdef __APPLE__

#include "cling/Utils/Output.h"
#include "cling/Utils/Paths.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <sstream>

#include <CoreFoundation/CFBase.h> // For MAC_OS_X_VERSION_X_X macros

// gcc on Mac can only include CoreServices.h up to 10.9 SDK, which means
// we cannot use Gestalt to get the running OS version when >= 10.10
#if defined(__clang__) || !defined(MAC_OS_X_VERSION_10_10)
#include <dlfcn.h> // dlopen to avoid linking with CoreServices
#include <CoreServices/CoreServices.h>
#else
#define CLING_SWVERS_PARSE_ONLY 1
#endif

namespace cling {
namespace utils {
namespace platform {
inline namespace osx {

namespace {

static bool getISysRootVersion(const std::string& SDKs, int Major,
                               int Minor, std::string& SysRoot,
                               llvm::raw_ostream* Verbose, const char* Msg) {
  std::ostringstream os;
  os << SDKs << "MacOSX" << Major << "." << Minor << ".sdk";

  std::string SDKv = os.str();
  if (llvm::sys::fs::is_directory(SDKv)) {
    SysRoot.swap(SDKv);
    if (Verbose) {
      (*Verbose) << "SDK version matching " << Major << "." << Minor
                    << " found, this does " << Msg << "\n";
    }
    return true;
  }

  if (Verbose)
    (*Verbose) << "SDK version matching " << Major << "." << Minor
                  << " not found, this would " << Msg << "\n";

  return false;
}

static std::string ReadSingleLine(const char* Cmd) {
  if (FILE* PF = ::popen(Cmd, "r")) {
    char Buf[1024];
    char* BufPtr = ::fgets(Buf, sizeof(Buf), PF);
    ::pclose(PF);
    if (BufPtr && Buf[0]) {
      const llvm::StringRef Result(Buf);
      assert(Result[Result.size()-1] == '\n' && "Single line too large");
      return Result.trim().str();
    }
  }
  return "";
}

static std::pair<int, int> SplitSDKVersion(int SDKVers) {
  if (SDKVers > 100000)
    return std::make_pair(SDKVers/10000, (SDKVers-100000)/100);
  return std::make_pair(SDKVers/100, (SDKVers-1000)/10);
}

} // anonymous namespace

bool MacOSSDK::getISysRoot(std::string& sysRoot) {
  using namespace llvm::sys;

  // Some versions of OS X and Server have headers installed
  if (fs::is_regular_file("/usr/include/stdlib.h"))
    return false;

  std::string SDKs("/Applications/Xcode.app/Contents/Developer");

  // Is XCode installed where it usually is?
  if (!fs::is_directory(SDKs)) {
    // Nope, use xcode-select -p to get the path
    SDKs = ReadSingleLine("xcode-select -p");
    if (SDKs.empty())
      return false;  // Nothing more we can do
  }

  SDKs.append("/Platforms/MacOSX.platform/Developer/SDKs/");
  if (!fs::is_directory(SDKs))
    return false;


  // Try to get the SDK for whatever version of OS X is currently running
  // Seems to make more sense to get the currently running SDK so headers
  // and any loaded libraries will match.

  int32_t majorVers = -1, minorVers = -1;
#ifndef CLING_SWVERS_PARSE_ONLY
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wdeprecated-declarations"
  if (void *core = ::dlopen(
          "/System/Library/Frameworks/CoreServices.framework/CoreServices",
          RTLD_LAZY)) {
    typedef ::OSErr (*GestaltProc)(::OSType, ::SInt32 *);
    if (GestaltProc Gestalt = (GestaltProc)::dlsym(core, "Gestalt")) {
      if (Gestalt(gestaltSystemVersionMajor, &majorVers) == ::noErr) {
        if (Gestalt(gestaltSystemVersionMinor, &minorVers) != ::noErr)
          minorVers = -1;
      } else
        majorVers = -1;
    }
    ::dlclose(core);
  }
 #pragma clang diagnostic pop
#endif

  if (majorVers == -1 || minorVers == -1) {
    const std::string SWVers = ReadSingleLine("sw_vers | grep ProductVersion"
                                              " | awk '{print $2}'");
    if (!SWVers.empty()) {
      if (::sscanf(SWVers.c_str(), "%d.%d", &majorVers, &minorVers) != 2) {
        majorVers = -1;
        minorVers = -1;
      }
    }
  }

  if (majorVers != -1 && minorVers != -1) {
    if (getISysRootVersion(SDKs, majorVers, minorVers, sysRoot, Verbose,
                           Verbose ? "match the version of OS X running"
                                   : nullptr)) {
      return true;
    }
  }

  // MAC_OS_X_VERSION_MAX_ALLOWED is -probably- the SDK being compiled with
  const std::pair<int,int> Vers = SplitSDKVersion(MAC_OS_X_VERSION_MAX_ALLOWED);
  if (Vers.first != majorVers || Vers.second != minorVers) {
    if (getISysRootVersion(SDKs, Vers.first, Vers.second, sysRoot, Verbose,
                           Verbose ? "match what cling was compiled with"
                                   : nullptr))
      return true;
  }

  // Nothing left to do but iterate the SDKs directory
  // Using a generic numerical sorting could easily break down, so we match
  // against 'MacOSX10.' as this is how they are installed, and fallback to
  // lexicographical sorting if things didn't work out.
  if (Verbose)
      (*Verbose) << "Looking in '" << SDKs << "' for highest version SDK.\n";
  sysRoot.clear();
  int SdkVers = 0;
  const std::string Match("MacOSX10.");
  std::vector<std::string> LexicalSdks;
  std::error_code ec;
  for (fs::directory_iterator it(SDKs, ec), e; !ec && it != e;
       it.increment(ec)) {
    const std::string SDKName = it->path().substr(SDKs.size());
    if (SDKName.find(Match) == 0) {
      const int CurVer = ::atoi(SDKName.c_str() + Match.size());
      if (CurVer > SdkVers) {
        sysRoot = it->path();
        SdkVers = CurVer;
      }
    } else if (sysRoot.empty())
      LexicalSdks.push_back(it->path());
  }
  if (sysRoot.empty() && !LexicalSdks.empty()) {
    if (Verbose)
      (*Verbose) << "Selecting SDK based on a lexical sort.\n";
    std::sort(LexicalSdks.begin(), LexicalSdks.end());
    sysRoot.swap(LexicalSdks.back());
  }

  return !sysRoot.empty();
}


} // namespace osx
} // namespace platform
} // namespace utils
} // namespace cling

#endif // __APPLE__
