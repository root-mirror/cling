//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/MetaProcessor/MetaProcessor.h"

#include "Display.h"
#include "InputValidator.h"
#include "MetaParser.h"
#include "MetaSema.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/Output.h"

#include "clang/Basic/FileManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"

#include "llvm/Support/Path.h"

#include <fcntl.h>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <stdio.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

using namespace clang;

namespace cling {

  class MetaProcessor::RedirectOutput {

    static int dupOnce(int Fd, int& Bak) {
      // Flush now or can drop the buffer when dup2 is called with Fd later.
      // This seems only neccessary when piping stdout or stderr, but do it
      // for ttys to avoid over complicated code for minimal benefit.
      ::fflush(Fd==STDOUT_FILENO ? stdout : stderr);

      if (Bak == kInvalidFD)
        Bak = ::dup(Fd);

      return Bak;
    }

    struct Redirect {
      int FD;
      MetaProcessor::RedirectionScope Scope;
      bool Close;

      Redirect(std::string file, bool append, RedirectionScope S, int* Baks) :
        FD(-1), Scope(S), Close(false) {
        if (S & kSTDSTRM) {
          // Remove the flag from Scope, we don't need it anymore
          Scope = RedirectionScope(Scope & ~kSTDSTRM);
          if (file == "&1")
            FD = dupOnce(STDOUT_FILENO, Baks[0]);
          else if (file == "&2")
            FD = dupOnce(STDERR_FILENO, Baks[1]);
          // Close = false; Parent manages lifetime
          if (FD != -1)
            return;
          llvm_unreachable("kSTDSTRM passed for unknown stream");
        }
        const int Perm = 0644;
#ifdef LLVM_ON_WIN32
        const int Mode = _O_CREAT | _O_WRONLY | (append ? _O_APPEND : _O_TRUNC);
        FD = ::_open(file.c_str(), Mode, Perm);
#else
        const int Mode = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
        FD = ::open(file.c_str(), Mode, Perm);
#endif
        if (FD == -1) {
          ::perror("Redirect::open");
          return;
        }
        Close = true;
        if (append)
          ::lseek(FD, 0, SEEK_END);
      }
      ~Redirect() {
        if (Close)
          ::close(FD);
      }
    };

    typedef std::vector<std::unique_ptr<Redirect>> RedirectStack;
    enum { kNumRedirects = 2, kInvalidFD = -1 };

    RedirectStack m_Stack;
    int m_Bak[kNumRedirects];
    int m_CurStdOut;
    const bool m_TTY;

    // Exception safe push routine
    int push(Redirect* R) {
      std::unique_ptr<Redirect> Re(R);
      const int FD = R->FD;
      m_Stack.emplace_back(Re.get());
      Re.release();
      return FD;
    }

    // Call ::dup2 and report errMsg on failure
    bool dup2(int oldfd, int newfd, const char* errMsg) {
      if (::dup2(oldfd, newfd) == kInvalidFD) {
        ::perror(errMsg);
        return false;
      }
      return true;
    }

    // Restore stdstream from backup and close the backup
    void close(int &oldfd, int newfd) {
      assert((newfd == STDOUT_FILENO || newfd == STDERR_FILENO) && "Not std FD");
      assert(oldfd == m_Bak[newfd == STDERR_FILENO] && "Not backup FD");
      if (oldfd != kInvalidFD) {
        dup2(oldfd, newfd, "RedirectOutput::close");
        ::close(oldfd);
        oldfd = kInvalidFD;
      }
    }

    int restore(int FD, FILE *F, MetaProcessor::RedirectionScope Flag,
                int &bakFD) {
      // If no backup, we have never redirected the file, so nothing to restore
      if (bakFD != kInvalidFD) {
        // Find the last redirect for the scope, and restore redirection to it
        for (RedirectStack::const_reverse_iterator it = m_Stack.rbegin(),
                                                   e = m_Stack.rend();
             it != e; ++it) {
          const Redirect *R = (*it).get();
          if (R->Scope & Flag) {
            dup2(R->FD, FD, "RedirectOutput::restore");
            return R->FD;
          }
        }

        // No redirection for this scope, restore to backup
        fflush(F);
        close(bakFD, FD);
      }
      return kInvalidFD;
    }

  public:
    RedirectOutput() : m_CurStdOut(kInvalidFD),
      m_TTY(::isatty(STDOUT_FILENO)) {
      for (unsigned i = 0; i < kNumRedirects; ++i)
        m_Bak[i] = kInvalidFD;
    }

    ~RedirectOutput() {
      close(m_Bak[0], STDOUT_FILENO);
      close(m_Bak[1], STDERR_FILENO);
      while (!m_Stack.empty())
        m_Stack.pop_back();

#ifdef LLVM_ON_WIN32
      if (!m_TTY) {
        ::fflush(stderr);
        ::fflush(stdout);
      }
      const int Mode = _IONBF;
      const size_t Size = 0;
#else
      const int Mode = _IOLBF;
      const size_t Size = BUFSIZ;
#endif
      // If redirection took place without writing anything to the terminal
      // beforehand (--nologo) then the dup2 relinking stdout will have caused
      // it to be re-opened without line buffering.
      if (m_TTY)
        ::setvbuf(stdout, NULL, Mode, Size);
    }

    void redirect(llvm::StringRef file, bool apnd,
                  MetaProcessor::RedirectionScope scope) {
      if (file.empty()) {
        // Unredirection, remove last redirection state(s) for given scope(s)
        if (m_Stack.empty()) {
          cling::errs() << "No redirections left to remove\n";
          return;
        }

        MetaProcessor::RedirectionScope lScope = scope;
        SmallVector<RedirectStack::iterator, 2> Remove;
        for (auto it = m_Stack.rbegin(), e = m_Stack.rend(); it != e; ++it) {
          Redirect *R = (*it).get();
          const unsigned Match = R->Scope & lScope;
          if (Match) {
            // Clear the flag so restore below will ignore R for scope
            R->Scope = MetaProcessor::RedirectionScope(R->Scope & ~Match);
            // If no scope left, then R should be removed
            if (!R->Scope) {
              // standard [24.4.1/1] says &*(reverse_iterator(i)) == &*(i - 1)
              Remove.push_back(std::next(it).base());
            }
            // Clear match to reduce lScope (kSTDBOTH -> kSTDOUT or kSTDERR)
            lScope = MetaProcessor::RedirectionScope(lScope & ~Match);
            // If nothing to match anymore, then we're done
            if (!lScope)
              break;
          }
        }
        // std::vector::erase invalidates iterators at or after the point of
        // the erase, so if we reverse iterate on Remove everything is fine
        for (auto it = Remove.rbegin(), e = Remove.rend(); it != e; ++it)
          m_Stack.erase(*it);
      } else {
        // Add new redirection state
        if (push(new Redirect(file.str(), apnd, scope, m_Bak)) != kInvalidFD) {
          // Save a backup for the scope(s), if not already done
          if (scope & MetaProcessor::kSTDOUT)
            dupOnce(STDOUT_FILENO, m_Bak[0]);
          if (scope & MetaProcessor::kSTDERR)
            dupOnce(STDERR_FILENO, m_Bak[1]);
        } else
          return; // Failure
      }

      if (scope & MetaProcessor::kSTDOUT)
        m_CurStdOut =
            restore(STDOUT_FILENO, stdout, MetaProcessor::kSTDOUT, m_Bak[0]);
      if (scope & MetaProcessor::kSTDERR)
        restore(STDERR_FILENO, stderr, MetaProcessor::kSTDERR, m_Bak[1]);
    }

    void resetStdOut(bool toBackup = false) {
      // When not outputing to a TTY there is no need to unredirect as
      // TerminalDisplay handles writing to the console FD already.
      if (!m_TTY)
        return;

      if (toBackup) {
        if (m_Bak[0] != kInvalidFD) {
          fflush(stdout);
          dup2(m_Bak[0], STDOUT_FILENO, "RedirectOutput::resetStdOut");
        }
      } else if (m_CurStdOut != kInvalidFD)
        dup2(m_CurStdOut, STDOUT_FILENO, "RedirectOutput::resetStdOut");
    }

    bool empty() const {
      return m_Stack.empty();
    }
  };

  MetaProcessor::MaybeRedirectOutputRAII::MaybeRedirectOutputRAII(
                                                             MetaProcessor &P) :
    m_MetaProcessor(P) {
    if (m_MetaProcessor.m_RedirectOutput)
      m_MetaProcessor.m_RedirectOutput->resetStdOut(true);
  }

  MetaProcessor::MaybeRedirectOutputRAII::~MaybeRedirectOutputRAII() {
    if (m_MetaProcessor.m_RedirectOutput)
      m_MetaProcessor.m_RedirectOutput->resetStdOut();
  }

  MetaProcessor::MetaProcessor(Interpreter& interp, raw_ostream& outs)
    : m_Interp(interp), m_Outs(&outs) {
    m_InputValidator.reset(new InputValidator());
    m_MetaParser.reset(new MetaParser(new MetaSema(interp, *this)));
  }

  MetaProcessor::~MetaProcessor() {
  }

  int MetaProcessor::process(llvm::StringRef input_line,
                             Interpreter::CompilationResult& compRes,
                             Value* result,
                             bool disableValuePrinting /* = false */) {
    if (result)
      *result = Value();
    compRes = Interpreter::kSuccess;
    int expectedIndent = m_InputValidator->getExpectedIndent();

    if (expectedIndent)
      compRes = Interpreter::kMoreInputExpected;

    if (input_line.empty() ||
        (input_line.size() == 1 && input_line.front() == '\n')) {
      // just a blank line, nothing to do.
      return expectedIndent;
    }

    //  Check for and handle meta commands.
    m_MetaParser->enterNewInputLine(input_line);
    MetaSema::ActionResult actionResult = MetaSema::AR_Success;
    if (!m_InputValidator->inBlockComment() &&
         m_MetaParser->isMetaCommand(actionResult, result)) {

      if (m_MetaParser->isQuitRequested())
        return -1;

      if (actionResult != MetaSema::AR_Success)
        compRes = Interpreter::kFailure;
       // ExpectedIndent might have changed after meta command.
       return m_InputValidator->getExpectedIndent();
    }

    // Check if the current statement is now complete. If not, return to
    // prompt for more.
    if (m_InputValidator->validate(input_line) == InputValidator::kIncomplete) {
      compRes = Interpreter::kMoreInputExpected;
      return m_InputValidator->getExpectedIndent();
    }

    //  We have a complete statement, compile and execute it.
    std::string input;
    m_InputValidator->reset(&input);
    // if (m_Options.RawInput)
    //   compResLocal = m_Interp.declare(input);
    // else
    compRes = m_Interp.process(input, result, /*Transaction*/ nullptr,
                               disableValuePrinting);

    return 0;
  }

  void MetaProcessor::cancelContinuation() const {
    m_InputValidator->reset();
  }

  int MetaProcessor::getExpectedIndent() const {
    return m_InputValidator->getExpectedIndent();
  }

  static Interpreter::CompilationResult reportIOErr(llvm::StringRef File,
                                                    const char* What) {
    cling::errs() << "Error in cling::MetaProcessor: "
          "cannot " << What << " input: '" << File << "'\n";
    return Interpreter::kFailure;
  }

  Interpreter::CompilationResult
  MetaProcessor::readInputFromFile(llvm::StringRef filename,
                                   Value* result,
                                   size_t posOpenCurly,
                                   bool lineByLine) {

    // FIXME: This will fail for Unicode BOMs (and seems really weird)
    {
      // check that it's not binary:
      std::ifstream in(filename.str().c_str(), std::ios::in | std::ios::binary);
      if (in.fail())
        return reportIOErr(filename, "open");

      char magic[1024] = {0};
      in.read(magic, sizeof(magic));
      size_t readMagic = in.gcount();
      // Binary files < 300 bytes are rare, and below newlines etc make the
      // heuristic unreliable.
      if (!in.fail() && readMagic >= 300) {
        llvm::StringRef magicStr(magic,in.gcount());
        llvm::sys::fs::file_magic fileType
          = llvm::sys::fs::identify_magic(magicStr);
        if (fileType != llvm::sys::fs::file_magic::unknown)
          return reportIOErr(filename, "read from binary");

        unsigned printable = 0;
        for (size_t i = 0; i < readMagic; ++i)
          if (isprint(magic[i]))
            ++printable;
        if (10 * printable <  5 * readMagic) {
          // 50% printable for ASCII files should be a safe guess.
          return reportIOErr(filename, "won't read from likely binary");
        }
      }
    }

    std::ifstream in(filename.str().c_str());
    if (in.fail())
      return reportIOErr(filename, "open");

    in.seekg(0, std::ios::end);
    if (in.fail())
      return reportIOErr(filename, "seek");

    size_t size = in.tellg();
    if (in.fail())
      return reportIOErr(filename, "tell");

    in.seekg(0);
    if (in.fail())
      return reportIOErr(filename, "rewind");

    std::string content(size, ' ');
    in.read(&content[0], size);
    if (in.fail())
      return reportIOErr(filename, "read");

    static const char whitespace[] = " \t\r\n";
    if (content.length() > 2 && content[0] == '#' && content[1] == '!') {
      // Convert shebang line to comment. That's nice because it doesn't
      // change the content size, leaving posOpenCurly untouched.
      content[0] = '/';
      content[1] = '/';
    }

    if (posOpenCurly != (size_t)-1 && !content.empty()) {
      assert(content[posOpenCurly] == '{'
             && "No curly at claimed position of opening curly!");
      // hide the curly brace:
      content[posOpenCurly] = ' ';
      // and the matching closing '}'
      size_t posCloseCurly = content.find_last_not_of(whitespace);
      if (posCloseCurly != std::string::npos) {
        if (content[posCloseCurly] == ';' && content[posCloseCurly-1] == '}') {
          content[posCloseCurly--] = ' '; // replace ';' and enter next if
        }
        if (content[posCloseCurly] == '}') {
          content[posCloseCurly] = ' '; // replace '}'
        } else {
          std::string::size_type posBlockClose = content.find_last_of('}');
          if (posBlockClose != std::string::npos) {
            content[posBlockClose] = ' '; // replace '}'
          }
          std::string::size_type posComment
            = content.find_first_not_of(whitespace, posBlockClose);
          if (posComment != std::string::npos
              && content[posComment] == '/' && content[posComment+1] == '/') {
            // More text (comments) are okay after the last '}', but
            // we can not easily find it to remove it (so we need to upgrade
            // this code to better handle the case with comments or
            // preprocessor code before and after the leading { and
            // trailing })
            while (posComment <= posCloseCurly) {
              content[posComment++] = ' '; // replace '}' and comment
            }
          } else {
            content[posCloseCurly] = '{';
            // By putting the '{' back, we keep the code as consistent as
            // the user wrote it ... but we should still warn that we not
            // goint to treat this file an unamed macro.
            cling::errs()
              << "Warning in cling::MetaProcessor: can not find the closing '}', "
              << llvm::sys::path::filename(filename)
              << " is not handled as an unamed script!\n";
          } // did not find "//"
        } // remove comments after the trailing '}'
      } // find '}'
    } // ignore outermost block

    m_CurrentlyExecutingFile = filename;
    bool topmost = !m_TopExecutingFile.data();
    if (topmost)
      m_TopExecutingFile = m_CurrentlyExecutingFile;

    content.insert(0, "#line 2 \"" + filename.str() + "\" \n");
    // We don't want to value print the results of a unnamed macro.
    if (content.back() != ';')
      content.append(";");

    Interpreter::CompilationResult ret = Interpreter::kSuccess;
    if (lineByLine) {
      int rslt = 0;
      std::string line;
      std::stringstream ss(content);
      while (std::getline(ss, line, '\n')) {
        rslt = process(line, ret, result);
        if (ret == Interpreter::kFailure)
          break;
      }
      if (rslt) {
        cling::errs() << "Error in cling::MetaProcessor: file "
                     << llvm::sys::path::filename(filename)
                     << " is incomplete (missing parenthesis or similar)!\n";
      }
    } else
      ret = m_Interp.process(content, result);

    m_CurrentlyExecutingFile = llvm::StringRef();
    if (topmost)
      m_TopExecutingFile = llvm::StringRef();
    return ret;
  }

  void MetaProcessor::setStdStream(llvm::StringRef file, RedirectionScope scope,
                                   bool append) {
    assert((scope & kSTDOUT || scope & kSTDERR) && "Invalid RedirectionScope");
    if (!m_RedirectOutput)
      m_RedirectOutput.reset(new RedirectOutput);

    m_RedirectOutput->redirect(file, append, scope);
    if (m_RedirectOutput->empty())
      m_RedirectOutput.reset();
  }

  void MetaProcessor::registerUnloadPoint(const Transaction* T,
                                          llvm::StringRef filename) {
    m_MetaParser->getActions().registerUnloadPoint(T, filename);
  }

} // end namespace cling
