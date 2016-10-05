//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Utils/Platform.h"

#if defined(LLVM_ON_UNIX)

#include "cling/Utils/Paths.h"

#include <array>
#include <atomic>
#include <string>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// PATH_MAX
#ifdef __APPLE__
 #include <sys/syslimits.h>
#else
 #include <limits.h>
#endif

#define PATH_MAXC (PATH_MAX+1)

namespace cling {
namespace utils {
namespace platform {

namespace {
  // A simple round-robin cache: what enters first, leaves first.
  // MRU cache wasn't worth the extra CPU cycles.
  struct Cache {
  private:
    std::array<const void*, 8> lines;
    std::atomic<unsigned> mostRecent = {0};
  public:
    bool contains(const void* P) {
      return std::find(lines.begin(), lines.end(), P) != lines.end();
    }

    // Concurrent writes to the same cache element can result in invalid cache
    // elements, causing pointer address not being available in the cache even
    // though they should be, i.e. false cache misses. While can cause a
    // slow-down, the cost for keeping the cache thread-local or atomic is
    // much higher (yes, this was measured).
    void push(const void* P) {
      unsigned acquiredVal = mostRecent;
      while(!mostRecent.compare_exchange_weak(acquiredVal, (acquiredVal+1)%lines.size())) {
        acquiredVal = mostRecent;
      }
      lines[acquiredVal] = P;
    }
  };

  // Note: not thread safe, see comment above push().
  static Cache& getCache() {
    static Cache threadCache;
    return threadCache;
  }

  static int getNullDevFileDescriptor() {
    struct FileDescriptor {
      int FD;
      const char* file = "/dev/random";
      FileDescriptor() { FD = open(file, O_WRONLY); }
      ~FileDescriptor() {
        close(FD);
      }
    };
    static FileDescriptor nullDev;
    return nullDev.FD;
  }
} // anonymous namespace

bool IsMemoryValid(const void *P) {
  // Look-up the address in the cache.
  Cache& currentCache = getCache();
  if (currentCache.contains(P))
    return true;
  // There is a POSIX way of finding whether an address
  // can be accessed for reading.
  if (write(getNullDevFileDescriptor(), P, 1/*byte*/) != 1) {
    assert(errno == EFAULT && "unexpected write error at address");
    return false;
  }
  currentCache.push(P);
  return true;
}

std::string GetCwd() {
  char Buffer[PATH_MAXC];
  if (::getcwd(Buffer, sizeof(Buffer)))
    return Buffer;

  ::perror("Could not get current working directory");
  return std::string();
}

const void* DLOpen(const std::string& Path, std::string* Err) {
  void* Lib = dlopen(Path.c_str(), RTLD_LAZY|RTLD_GLOBAL);
  if (Err) {
    if (const char* DyLibError = ::dlerror())
      *Err = DyLibError;
  }
  return Lib;
}

void DLClose(const void* Lib, std::string* Err) {
  ::dlclose(const_cast<void*>(Lib));
  if (Err) {
    if (const char* DyLibError = ::dlerror())
      *Err = DyLibError;
  }
}

std::string NormalizePath(const std::string& Path) {
  char Buf[PATH_MAXC];
  if (const char* Result = ::realpath(Path.c_str(), Buf))
    return std::string(Result);

  ::perror("realpath");
  return std::string();
}

bool GetSystemLibraryPaths(llvm::SmallVectorImpl<std::string>& Paths) {
#if defined(__APPLE__) || defined(__CYGWIN__)
  Paths.push_back("/usr/local/lib/");
  Paths.push_back("/usr/X11R6/lib/");
  Paths.push_back("/usr/lib/");
  Paths.push_back("/lib/");

 #ifndef __APPLE__
  Paths.push_back("/lib/x86_64-linux-gnu/");
  Paths.push_back("/usr/local/lib64/");
  Paths.push_back("/usr/lib64/");
  Paths.push_back("/lib64/");
 #endif
#else
  std::string Result;
  if (FILE* F = ::popen("LD_DEBUG=libs LD_PRELOAD=DOESNOTEXIST ls 2>&1", "r")) {
    char Buf[1024];
    while (::fgets(&Buf[0], sizeof(Buf), F))
      Result += Buf;
    ::pclose(F);
  }

  const std::size_t NPos = std::string::npos;
  const std::size_t LD = Result.find("(LD_LIBRARY_PATH)");
  std::size_t From = Result.find("search path=", LD == NPos ? 0 : LD);
  if (From != NPos) {
    const std::size_t To = Result.find("(system search path)", From);
    if (To != NPos) {
      From += 12;
      std::string SysPath = Result.substr(From, To-From);
      SysPath.erase(std::remove_if(SysPath.begin(), SysPath.end(), isspace),
                    SysPath.end());

      llvm::SmallVector<llvm::StringRef, 10> CurPaths;
      SplitPaths(SysPath, CurPaths);
      for (const auto& Path : CurPaths)
        Paths.push_back(Path.str());
    }
  }
#endif
  return true;
}

} // namespace platform
} // namespace utils
} // namespace cling

#endif // LLVM_ON_UNIX
