//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %built_cling -fno-rtti | FileCheck %s

// The test verifies the expected behavior in cling::utils::Transform class,
// which is supposed to provide different transformation of AST nodes and types.

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/LookupHelper.h"
#include "cling/Utils/AST.h"
#include "cling/Utils/Output.h"
#include "clang/AST/Type.h"
#include "clang/AST/ASTContext.h"
#include "llvm/ADT/SmallSet.h"
#include "clang/Sema/Sema.h"

.rawInput 1

#include <vector>
#include <iostream>

typedef double Double32_t;
typedef int Int_t;
typedef long Long_t;
typedef Int_t* IntPtr_t;
typedef Int_t& IntRef_t;

template <typename T> class A {};
template <typename T, typename U> class B {};
template <typename T, typename U> class C {};
typedef C<A<B<Double32_t, Int_t> >, Double32_t > CTD;
typedef C<A<B<const Double32_t, const Int_t> >, Double32_t > CTDConst;

template <typename key, typename value, typename compare_operation = std::less<key>, typename alloc = std::allocator<std::pair<const key, value> > > class cmap { key fKey; const value fValue; alloc fAlloc; public: cmap() : fValue(0) {} };
   // : public std::map<key, value, compare_operation, alloc> {

template <typename key, typename value = const key> class mypair { public: key fKey; value fValue; };

#include <string>

namespace Details {
  class Impl {};
}

// To insure instantiation.
// typedef std::pair<Details::Impl,std::vector<Details::Impl> > details_pairs;

namespace NS {
  template <typename T, int size = 0> class ArrayType {};
  template <typename T> class Array {};

  template <typename T> class Container {
  public:
    class Content {};
    typedef T Value_t;
    typedef Content Content_t;
    typedef ::Details::Impl Impl_t;
  };

  template <typename T> class TDataPoint {};
  typedef TDataPoint<float> TDataPointF;
  typedef TDataPoint<Double32_t> TDataPointD32;

  const int typeN =1;
  typedef ArrayType<float, typeN + 1> FArray;

  typedef int IntNS_t;

}

// Anonymous namespace
namespace {
  class InsideAnonymous {
  };
}

using namespace std;

class Embedded_objects {
public:
  enum Eenum {
    kEnumConst=16
  };

  class EmbeddedClasses;
  typedef EmbeddedClasses EmbeddedTypedef;

  class EmbeddedClasses {
  public:
    class Embedded1 {};
    class Embedded2 {};
    class Embedded3 {};
    class Embedded4 {};
    class Embedded5 {};
    class Embedded6 {};

  };
  EmbeddedClasses m_embedded;
  EmbeddedClasses::Embedded1 m_emb1;
  EmbeddedClasses::Embedded2 m_emb2;
  EmbeddedClasses::Embedded3 m_emb3;

  EmbeddedTypedef::Embedded4 m_emb4;
  Embedded_objects::EmbeddedClasses::Embedded5 m_emb5;
  Embedded_objects::EmbeddedTypedef::Embedded6 m_emb6;

  typedef std::vector<int> vecint;
  vecint* m_iter;
  const Eenum m_enum;
  typedef vector<int> vecint2;
  vecint2* m_iter2;
  vector<Double32_t> vd32a;
  typedef vector<Double32_t> vecd32t1;
  vecd32t1 vd32b;

  using vecd32t2 = vector<Double32_t>;
  vecd32t2 vd32c;

  template <typename T> using myvector = std::vector<T>;
  myvector<float> vfa;
  // Not yet, the desugar of template alias do not keep the opaque typedef.
  // myvector<Double32_t> vd32d;

  Double32_t *p1;

  template<class T> using ptr = T*;
  ptr<float> p2;
  // Not yet, the desugar of template alias do not keep the opaque typedef.
  // ptr<Double32_t> p3;

  typedef B<Int_t,Double32_t> t1;
  typedef t1* t2;
  typedef t2* t3;
  typedef t3 t4[3];
  typedef t4 t5[4];
  typedef t5& t6;
  typedef t1& t7;
  typedef t2& t8;
  typedef t2  t9;
  typedef t5* t10;

  t1 d1;
  t2 d2;
  t2 d2_1[5];
  t3 d3;
  t4 d4;
  t5 d5;
  t6 d6;
  t7 d7;
  t8 d8;
  t9 d9;
  t10 d10;
};

namespace NS1 {
  namespace NS2 {
    namespace NS3 {
      inline namespace InlinedNamespace {
        class InsideInline {};
      }
      class Point {};
      class Inner3 {
      public:
        Point p1;
        NS3::Point p2;
        ::NS1::NS2::NS3::Point p3;
        InsideInline p4;
      };
    }
  }
}

.rawInput 0

const cling::LookupHelper& lookup = gCling->getLookupHelper();
cling::LookupHelper::DiagSetting diags = cling::LookupHelper::WithDiagnostics;
clang::Sema* Sema = &gCling->getSema();
const clang::ASTContext& Ctx = gCling->getSema().getASTContext();
cling::utils::Transform::Config transConfig;
using namespace cling::utils;

transConfig.m_toSkip.insert(Lookup::Named(Sema, "Double32_t"));
using namespace std;
transConfig.m_toSkip.insert(Lookup::Named(Sema, "string"));
transConfig.m_toSkip.insert(Lookup::Named(Sema, "string", Lookup::Namespace(Sema, "std")));

const clang::Type* t = 0;
clang::QualType QT;
using namespace cling::utils;

// Test the behavior on a simple class
lookup.findScope("Details::Impl", diags, &t);
QT = clang::QualType(t, 0);
//QT.getAsString().c_str()
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "Details::Impl"

// Test the behavior for a class inside an anonymous namespace
lookup.findScope("InsideAnonymous", diags, &t);
QT = clang::QualType(t, 0);
//QT.getAsString().c_str()c
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "class (anonymous namespace)::InsideAnonymous"

// The above result is not quite want we want, so the client must using
// the following:
// The scope suppression is required for getting rid of the anonymous part of the name of a class defined in an anonymous namespace.
// This gives us more control vs not using the clang::ElaboratedType and relying on the Policy.SuppressUnwrittenScope which would
// strip both the anonymous and the inline namespace names (and we probably do not want the later to be suppressed).
clang::PrintingPolicy Policy(Ctx.getPrintingPolicy());
Policy.SuppressTagKeyword = true; // Never get the class or struct keyword
Policy.SuppressScope = true;      // Force the scope to be coming from a clang::ElaboratedType.
std::string name;
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsStringInternal(name,Policy);
name.c_str()
// CHECK: ({{[^)]+}}) "InsideAnonymous"

// Test desugaring pointers types:
QT = lookup.findType("Int_t*", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK:({{[^)]+}}) "int *"

QT = lookup.findType("const IntPtr_t*", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK:({{[^)]+}}) "int *const *"


// Test desugaring reference (both r- or l- value) types:
QT = lookup.findType("const IntPtr_t&", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK:({{[^)]+}}) "int *const &"

//TODO: QT = lookup.findType("IntPtr_t[32], diags");

// To do: findType does not return the const below:
// Test desugaring reference (both r- or l- value) types:
// QT = lookup.findType("const IntRef_t", diags);
// Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// should print:({{[^)]+}}) "int &const"
// but this is actually an illegal type:

// C++ [dcl.ref]p1:
//   Cv-qualified references are ill-formed except when the
//   cv-qualifiers are introduced through the use of a typedef
//   (7.1.3) or of a template type argument (14.3), in which
//   case the cv-qualifiers are ignored.

// So the following is the right behavior:

// Will issue
// "'const' qualifier on reference type 'IntRef_t' (aka 'int &') has no effect"
// thus suppress diagnostics
QT = lookup.findType("const IntRef_t", cling::LookupHelper::NoDiagnostics);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "int &"

// Test desugaring reference (both r- or l- value) types:
QT = lookup.findType("IntRef_t", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK:({{[^)]+}}) "int &"


//Desugar template parameters:
lookup.findScope("A<B<Double32_t, Int_t*> >", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK:({{[^)]+}}) "A<B<Double32_t, int *> >"

lookup.findScope("A<B<Double32_t, std::size_t*> >", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK:({{[^)]+}}) "A<B<Double32_t, unsigned {{long|int|long long}} *> >"

lookup.findScope("CTD", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "C<A<B<Double32_t, int> >, Double32_t>"

lookup.findScope("CTDConst", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "C<A<B<const Double32_t, const int> >, Double32_t>"

lookup.findScope("std::pair<const std::string,int>", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "std::pair<const std::string, int>"

lookup.findScope("NS::Array<NS::ArrayType<double> >", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::Array<NS::ArrayType<double> >"

lookup.findScope("NS::Array<NS::ArrayType<Double32_t> >", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::Array<NS::ArrayType<Double32_t> >"

lookup.findScope("NS::Container<Long_t>::Content", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::Container<long>::Content"

QT = lookup.findType("NS::Container<Long_t>::Value_t", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "long"

lookup.findScope("NS::Container<Long_t>::Content_t", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::Container<long>::Content"

lookup.findScope("NS::Container<Long_t>::Impl_t", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "Details::Impl"

lookup.findScope("NS::Container<Double32_t>::Content", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::Container<Double32_t>::Content"

QT = lookup.findType("NS::Container<Double32_t>::Value_t", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "double"
// Really we would want it to say Double32_t but oh well.

lookup.findScope("NS::Container<Double32_t>::Content_t", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::Container<Double32_t>::Content"

lookup.findScope("NS::Container<Double32_t>::Impl_t", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "Details::Impl"

lookup.findScope("NS::TDataPointF", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::TDataPoint<float>"

lookup.findScope("NS::TDataPointD32", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::TDataPoint<Double32_t>"

lookup.findScope("NS::ArrayType<float,1>", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::ArrayType<float, 1>"

lookup.findScope("NS::FArray", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "NS::ArrayType<float, 2>"

QT = lookup.findType("const NS::IntNS_t", diags);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "const int"

lookup.findScope("vector<Details::Impl>::value_type", diags, &t);
QT = clang::QualType(t, 0);
Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str()
// CHECK: ({{[^)]+}}) "Details::Impl"

const clang::Decl*decl=lookup.findScope("Embedded_objects", diags,&t);
if (decl) {
  const clang::CXXRecordDecl *cxxdecl
    = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
  if (cxxdecl) {
    clang::DeclContext::decl_iterator iter = cxxdecl->decls_begin();
    while ( *iter ) {
      const clang::Decl *mdecl = *iter;
      if (const clang::ValueDecl *vd = llvm::dyn_cast<clang::ValueDecl>(mdecl)) {
        clang::QualType vdType = vd->getType();
        name.clear();
        Transform::GetPartiallyDesugaredType(Ctx,vdType,transConfig).getAsStringInternal(name,Policy);
        cling::outs() << name.c_str() << '\n';
      }
      ++iter;
    }
  }
}
// CHECK: Embedded_objects::EmbeddedClasses
// CHECK: Embedded_objects::EmbeddedClasses::Embedded1
// CHECK: Embedded_objects::EmbeddedClasses::Embedded2
// CHECK: Embedded_objects::EmbeddedClasses::Embedded3
// CHECK: Embedded_objects::EmbeddedClasses::Embedded4
// CHECK: Embedded_objects::EmbeddedClasses::Embedded5
// CHECK: Embedded_objects::EmbeddedClasses::Embedded6
// CHECK: std::vector<int> *
// CHECK: const Embedded_objects::Eenum
// CHECK: std::vector<int> *
// CHECK: std::vector<Double32_t>
// CHECK: std::vector<Double32_t>
// CHECK: std::vector<Double32_t>
// CHECK: std::vector<float>
// NOT-YET-CHECK: std::vector<Double32_t>
// CHECK: Double32_t *
// CHECK: float *
// NOT-YET-CHECK: Double32_t *
// CHECK: B<int, Double32_t>
// CHECK: B<int, Double32_t> *
// CHECK: B<int, Double32_t> *[5]
// CHECK: B<int, Double32_t> **
// CHECK: B<int, Double32_t> **[3]
// CHECK: B<int, Double32_t> **[4][3]
// CHECK: B<int, Double32_t> **(&)[4][3]
// CHECK: B<int, Double32_t> &
// CHECK: B<int, Double32_t> *&
// CHECK: B<int, Double32_t> *
// CHECK: B<int, Double32_t> **(*)[4][3]

// In the partial desugaring add support for the case where we have a type
// that point to an already completely desugared template instantiation in
// which case the type is a RecordDecl rather than a TemplateInstantationType
decl = lookup.findScope("std::pair<Details::Impl,std::vector<Details::Impl> >", diags,&t);
QT = clang::QualType(t, 0);
cling::outs() << Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str() << '\n';
// CHECK: std::pair<Details::Impl, std::vector<Details::Impl> >

if (const clang::RecordDecl *rdecl = llvm::dyn_cast_or_null<clang::RecordDecl>(decl)) {
  clang::RecordDecl::field_iterator field_iter = rdecl->field_begin();
  // For some reason we can not call field_end:
  // cling: root/interpreter/llvm/src/tools/clang/lib/CodeGen/CGCall.cpp:1839: void checkArgMatches(llvm::Value*, unsigned int&, llvm::FunctionType*): Assertion `Elt->getType() == FTy->getParamType(ArgNo)' failed.
  // so just 'guess' the size
  int i = 0;
  while( i < 2 )  {
    name.clear();
    clang::QualType fdType = field_iter->getType();
    Transform::GetPartiallyDesugaredType(Ctx,fdType,transConfig).getAsStringInternal(name,Policy);
    cling::outs() << name.c_str() << '\n';
    ++field_iter;
    ++i;
  }
}
// CHECK: Details::Impl
// CHECK: std::vector<Details::Impl, std::allocator<Details::Impl> >


decl=lookup.findScope("NS1::NS2::NS3::Inner3", diags,&t);
if (decl) {
  const clang::CXXRecordDecl *cxxdecl
  = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
  if (cxxdecl) {
    clang::DeclContext::decl_iterator iter = cxxdecl->decls_begin();
    while ( *iter ) {
      const clang::Decl *mdecl = *iter;
      if (const clang::ValueDecl *vd = llvm::dyn_cast<clang::ValueDecl>(mdecl)) {
        clang::QualType vdType = vd->getType();
        name.clear();
        Transform::GetPartiallyDesugaredType(Ctx,vdType,transConfig).getAsStringInternal(name,Policy);
        cling::outs() << name.c_str() << '\n';
      }
      ++iter;
    }
  }
}
// CHECK: NS1::NS2::NS3::Point
// CHECK: NS1::NS2::NS3::Point
// CHECK: NS1::NS2::NS3::Point

decl = lookup.findScope("cmap<volatile int,volatile int>", diags,&t);
QT = clang::QualType(t, 0);
cling::outs() << Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str() << '\n';
if (const clang::RecordDecl *rdecl = llvm::dyn_cast_or_null<clang::RecordDecl>(decl)) {
  QT = clang::QualType(rdecl->getTypeForDecl(), 0);
  cling::outs() << Transform::GetPartiallyDesugaredType(Ctx, QT, transConfig).getAsString().c_str() << '\n';
  clang::RecordDecl::field_iterator field_iter = rdecl->field_begin();
  // For some reason we can not call field_end:
  // cling: root/interpreter/llvm/src/tools/clang/lib/CodeGen/CGCall.cpp:1839: void checkArgMatches(llvm::Value*, unsigned int&, llvm::FunctionType*): Assertion `Elt->getType() == FTy->getParamType(ArgNo)' failed.
  // so just 'guess' the size
  int i = 0;
  while( i < 2 )  {
    name.clear();
    clang::QualType fdType = field_iter->getType();
    Transform::GetPartiallyDesugaredType(Ctx,fdType,transConfig).getAsStringInternal(name,Policy);
    cling::outs() << name.c_str() << '\n';
    ++field_iter;
    ++i;
  }
}
// CHECK: cmap<volatile int, volatile int>
// CHECK: cmap<volatile int, volatile int, std::less<volatile int>, std::allocator<std::pair<const volatile int, volatile int> > >
// CHECK: volatile int
// CHECK: const volatile int
