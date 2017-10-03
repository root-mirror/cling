//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %built_cling -Xclang -verify 2>&1 | FileCheck %s

// Various failing Windows tests when the Windows format uses ELF.
// ELF COMDATs only support SelectionKind::Any, ' ??_7raw_string_ostream@llvm@@6B@' cannot be lowered.
// ELF COMDATs only support SelectionKind::Any, ' ??_7_Iostream_error_category@std@@6B@' cannot be lowered.

#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <string>

std::string S;
llvm::raw_string_ostream OS(S);
OS << "Simple Test";

OS.str()
//     CHECK: (std::string &) "Simple Test"

std::cout << "wrote to std::cout" << std::endl;
// CHECK-NEXT: wrote to std::cout

std::cout << "wrote to std::cerr" << std::endl;
// CHECK-NEXT: wrote to std::cerr

// expected-no-diagnostics
.q
