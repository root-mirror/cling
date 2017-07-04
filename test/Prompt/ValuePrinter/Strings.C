//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

//RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

#include <stdlib.h>
#ifdef _WIN32
 extern "C" int SetConsoleOutputCP(unsigned int);
 extern "C" int strcmp(const char*, const char*);
#endif

static void setLang(const char* Lang) {
#ifdef _WIN32
  ::SetConsoleOutputCP(strcmp(Lang, "en_US.UTF-8")==0 ? 65001 : 20127);
#else
  ::setenv("LANG", Lang, 1);
#endif
}
setLang("en_US.UTF-8");

const char* Data = (const char*) 0x01
// CHECK: (const char *) 0x{{.+}} <invalid memory address>

std::string RawData("\x12""\x13");

cling::printValue(&RawData)[1]
// CHECK-NEXT: (char) '0x12'

cling::printValue(&RawData)[2]
// CHECK-NEXT: (char) '0x13'

RawData = "Line1\nLine2\rLine3";
cling::printValue(&RawData)[6]
// CHECK-NEXT: (char) '\n'
cling::printValue(&RawData)[12]
// CHECK-NEXT: (char) '\r'
cling::printValue(&RawData)[13]
// CHECK-NEXT: (char) 'L'

"Line1\nLine2\nLine3"
// CHECK-NEXT: (const char [18]) "Line1
// CHECK-NEXT: Line2
// CHECK-NEXT: Line3"

"\x12""\x13"
// CHECK: (const char [3]) "\x12\x13"

"ABCD" "\x10""\x15" "EFG"
// CHECK-NEXT: (const char [10]) "ABCD\x10\x15" "EFG"

"ENDWITH" "\x11""\x07"
// CHECK-NEXT: (const char [10]) "ENDWITH\x11\x07"

"\x03" "\x09" "BEGANWITH"
// CHECK-NEXT: (const char [12]) "\x03\x09" "BEGANWITH"

"1233123213\n\n\n\f234\x3"
// CHECK-NEXT: (const char [19]) "1233123213\x0a\x0a\x0a\x0c" "234\x03"

// Posing as UTF-8, but invalid
// https://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt

"\xea"
// CHECK-NEXT: (const char [2]) "\xea"

"\xea\xfb"
// CHECK-NEXT: (const char [3]) "\xea\xfb"

"\xfe\xfe\xff\xff"
// CHECK-NEXT: (const char [5]) "\xfe\xfe\xff\xff"

"\xfc\x80\x80\x80\x80\xaf"
// CHECK-NEXT: (const char [7]) "\xfc\x80\x80\x80\x80\xaf"

"\xfc\x83\xbf\xbf\xbf\xbf"
// CHECK-NEXT: (const char [7]) "\xfc\x83\xbf\xbf\xbf\xbf"

"\xed\xa0\x80"
// CHECK-NEXT: (const char [4]) "\xed\xa0\x80"
"\xed\xad\xbf"
// CHECK-NEXT: (const char [4]) "\xed\xad\xbf"
"\xed\xae\x80"
// CHECK-NEXT: (const char [4]) "\xed\xae\x80"
"\xed\xaf\xbf"
// CHECK-NEXT: (const char [4]) "\xed\xaf\xbf"
"\xed\xb0\x80"
// CHECK-NEXT: (const char [4]) "\xed\xb0\x80"
"\xed\xbe\x80"
// CHECK-NEXT: (const char [4]) "\xed\xbe\x80"
"\xed\xbf\xbf"
// CHECK-NEXT: (const char [4]) "\xed\xbf\xbf"

"\xed\xa0\x80\xed\xb0\x80"
// CHECK-NEXT: (const char [7]) "\xed\xa0\x80\xed\xb0\x80"
"\xed\xa0\x80\xed\xbf\xbf"
// CHECK-NEXT: (const char [7]) "\xed\xa0\x80\xed\xbf\xbf"
"\xed\xad\xbf\xed\xb0\x80"
// CHECK-NEXT: (const char [7]) "\xed\xad\xbf\xed\xb0\x80"
"\xed\xad\xbf\xed\xbf\xbf"
// CHECK-NEXT: (const char [7]) "\xed\xad\xbf\xed\xbf\xbf"
"\xed\xae\x80\xed\xb0\x80"
// CHECK-NEXT: (const char [7]) "\xed\xae\x80\xed\xb0\x80"
"\xed\xae\x80\xed\xbf\xbf"
// CHECK-NEXT: (const char [7]) "\xed\xae\x80\xed\xbf\xbf"
"\xed\xaf\xbf\xed\xb0\x80"
// CHECK-NEXT: (const char [7]) "\xed\xaf\xbf\xed\xb0\x80"
"\xed\xaf\xbf\xed\xbf\xbf"
// CHECK-NEXT: (const char [7]) "\xed\xaf\xbf\xed\xbf\xbf"

std::string(u8"UTF-8")
// CHECK-NEXT: (std::string) "UTF-8"

std::u16string(u"UTF-16 " u"\x394" u"\x3a6" u"\x3a9")
// CHECK-NEXT: (std::u16string) u"UTF-16 ΔΦΩ"

std::u32string(U"UTF-32 " U"\x262D" U"\x2615" U"\x265F")
// CHECK-NEXT: (std::u32string) U"UTF-32 ☭☕♟"

std::u32string(U"UTF-32 " U"\u2616\u2615\u2614")
// CHECK-NEXT: (std::u32string) U"UTF-32 ☖☕☔"

std::wstring(L"wide")
// CHECK-NEXT: (std::wstring) L"wide"

u"16strliteral"
// CHECK-NEXT: (const char16_t [13]) u"16strliteral"

U"32literalstr"
// CHECK-NEXT: (const char32_t [13]) U"32literalstr"

L"wcharliteral"
// CHECK-NEXT: (const wchar_t [13]) L"wcharliteral"

// Unicode shouldn't do character level access, return the raw codepages

const char16_t* utf16 = u"16str";
utf16[0]
// CHECK-NEXT: (const char16_t) u'\u0031'

const char32_t* utf32 = U"32str";
utf32[1]
// CHECK-NEXT: (const char32_t) U'\U00000032'

// wchar_t doesn't guarantee a size
const wchar_t* wides = L"wide";
wides[3]
// CHECK-NEXT: (const wchar_t) L'\x{{0+}}65'

// ASCII output

setLang("");

u"UTF-16 " u"\x394" u"\x3a6" u"\x3a9"
// CHECK-NEXT: (const char16_t [11]) u"UTF-16 \u0394\u03a6\u03a9"

U"UTF-32\x262D\x2615\x265F"
// CHECK-NEXT: (const char32_t [10]) U"UTF-32\u262d\u2615\u265f"

U"UTF-32\x2616\x2615\x2614"
// CHECK-NEXT: (const char32_t [10]) U"UTF-32\u2616\u2615\u2614"

"\u20ac"
// CHECk-NEXT: (const char [4]) "\xe2\x82\xac"

"\u2620\u2603\u2368"
// CHECk-NEXT: (const char [10]) "\xe2\x98\xa0\xe2\x98\x83\xe2\x8d\xa8"


#include <stdio.h>
#include <vector>

static void ReadData(std::vector<char>& FData) {
  FILE *File = ::fopen("Strings.dat", "r");
  if (File) {
    ::fseek(File, 0L, SEEK_END);
    const size_t N = ::ftell(File);
    FData.reserve(N+1);
    FData.resize(N);
    ::fseek(File, 0L, SEEK_SET);
    ::fread(&FData[0], N, 1, File);
    ::fclose(File);
  }
  FData.push_back(0);
}

std::vector<char> FDat;
ReadData(FDat);
(char*)FDat.data()
// CHECk-NEXT: (char *) "deadbeeffeedfacec0ffeedebac1eecafebabe\xde\xad\xbe\xef\xfe\xed\xfa\xce\xc0\xff\xee\xde\xba\xc1\xee\xca\xfe\xba\xbe"

// expected-no-diagnostics
.q
