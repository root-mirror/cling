//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

// Test interception of _Facet_Register:
// VStudio 2015 & 2017 register facets in a linked list during std::use_facet.
//
// This will crash after main exits, when the VC runtime tries to delete all
// of the registered instances, who's vtable has already been destroyed.
// std::endl above should also test this, but doing it explicitly doesn't hurt.
//
// Would be nice to also test this in a child Interpreter, but can't import all
// the templates.
//

#include <locale>
#include <stdio.h>

std::locale Loc = std::locale("");
std::use_facet<std::moneypunct<char, true>>(Loc).curr_symbol();

.rawInput 1
struct TestFacet : std::locale::facet {
  TestFacet(std::size_t refs = 0) : facet(refs) { printf("TestFacet\n"); }
  ~TestFacet() { printf("~TestFacet\n"); }
  static std::locale::id id;
};
std::locale::id TestFacet::id;
.rawInput 0

Loc = std::locale(std::locale(), new TestFacet);
//      CHECK: TestFacet

std::has_facet<TestFacet>(Loc)
// CHECK-NEXT: (bool) true

// CHECK-NEXT: ~TestFacet

// expected-no-diagnostics
.q
