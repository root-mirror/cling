//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -std=gnu99 -x c -Xclang -verify 2>&1 | FileCheck %s
// RUN: cat %s | %cling -D__STRICT_ANSI__ -std=gnu++14 -Xclang -verify 2>&1 | FileCheck %s
// RUN: cat %s | %cling -D__STRICT_ANSI__ -std=gnu++1z -Xclang -verify 2>&1 | FileCheck %s

// RUN: cat %s | %cling --noruntime -D__STRICT_ANSI__ -std=gnu++11 -Xclang -verify 2>&1 | FileCheck %s
// --noruntime is for Windows which cannot include VSudio C++ headers in C++11

#ifdef __cplusplus
extern "C" int printf(const char*, ...);
#else
int printf(const char*, ...);
#endif

typeof (int) Val = 22;
typeof (const char*) This = "THIS";

printf("TEST: %d '%s'\n", Val, This);
// CHECK: TEST: 22 'THIS'

// expected-no-diagnostics
.q
