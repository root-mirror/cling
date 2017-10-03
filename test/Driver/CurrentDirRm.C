//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// Removing the cwd on Unix works but on Windows cannot be done.
// RUN: %mkdir "%T/Remove"
// RUN: cd "%T/Remove"
// RUN: %rmdir "%T/Remove"
// RUN: %cling %s -Xclang -verify 2>&1 | FileCheck %s
// XFAIL: win32, win64

extern "C" {
  int printf(const char*, ...);
  char* getcwd(char *buf, std::size_t size);
}

// Make sure include still works
#include <string.h>
#include <vector>

void CurrentDirRm() {
  char thisDir[1024];
  const char *rslt = getcwd(thisDir, sizeof(thisDir));
  // Make sure cling reported the error
  // CHECK: Could not get current working directory: {{.*}}

  if (rslt)
    printf("Working directory exists\n");
  // CHECK-NOT: Working directory exists

  printf("Script ran\n");
  // CHECK: Script ran
}

//expected-no-diagnostics
