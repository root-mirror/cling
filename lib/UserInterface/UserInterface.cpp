//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/UserInterface/UserInterface.h"

#include "cling/Interpreter/Exception.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/Utils/Output.h"
#include "textinput/Callbacks.h"
#include "textinput/TextInput.h"
#include "textinput/StreamReader.h"
#include "textinput/TerminalDisplay.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"

#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/CompilerInstance.h"

namespace {
  ///\brief Class that specialises the textinput TabCompletion to allow Cling
  /// to code complete through its own textinput mechanism which is part of the
  /// UserInterface.
  ///
  class UITabCompletion : public textinput::TabCompletion {
    const cling::Interpreter& m_ParentInterpreter;
  
  public:
    UITabCompletion(const cling::Interpreter& Parent) :
                    m_ParentInterpreter(Parent) {}
    ~UITabCompletion() {}

    bool Complete(textinput::Text& Line /*in+out*/,
                  size_t& Cursor /*in+out*/,
                  textinput::EditorRange& R /*out*/,
                  std::vector<std::string>& Completions /*out*/) override {
      m_ParentInterpreter.codeComplete(Line.GetText(), Cursor, Completions);
      return true;
    }
  };
}

namespace cling {

  ///\brief Delays ~TextInput until after ~StreamReader and ~TerminalDisplay
  ///
  class UserInterface::TextInputHolder {
    textinput::StreamReader* m_Reader;
    textinput::TerminalDisplay* m_Display;
    textinput::TextInput m_Input;

    class HistoryFile {
      llvm::SmallString<512> Path;

    public:
      HistoryFile(const char* Env = "CLING_NOHISTORY",
                  const char* Name = ".cling_history") {
        if (getenv(Env)) return;
        // History file is $HOME/.cling_history
        if (llvm::sys::path::home_directory(Path))
          llvm::sys::path::append(Path, Name);
      }
      const char* c_str() { return Path.empty() ? nullptr : Path.c_str(); }
    };

  public:
    TextInputHolder(HistoryFile HistFile = HistoryFile())
        : m_Reader(textinput::StreamReader::Create()),
          m_Display(textinput::TerminalDisplay::Create()),
          m_Input(*m_Reader, *m_Display, HistFile.c_str()) {
    }

    ~TextInputHolder() {
      delete m_Reader;
      delete m_Display;
    }

    operator textinput::TextInput& () { return m_Input; }
  };

  UserInterface::UserInterface() {
    llvm::install_fatal_error_handler(&CompilationException::throwingHandler);
  }

  UserInterface::~UserInterface() { llvm::remove_fatal_error_handler(); }

  void UserInterface::attach(Interpreter& Interp) {
    m_MetaProcessor.reset(new MetaProcessor(Interp, cling::outs()));
  }

  void UserInterface::runInteractively(bool nologo /* = false */) {
    assert(m_MetaProcessor.get() && "Not attached to an Interpreter.");
    if (!nologo)
      PrintLogo();

    m_TextInput.reset(new TextInputHolder);
    textinput::TextInput& TI = *m_TextInput;

    // Inform text input about the code complete consumer
    // TextInput owns the TabCompletion.
    TI.SetCompletion(new UITabCompletion(m_MetaProcessor->getInterpreter()));

    bool Done = false;
    std::string Line;
    std::string Prompt("[cling]$ ");

    while (!Done) {
      try {
        m_MetaProcessor->getOuts().flush();
        {
          MetaProcessor::MaybeRedirectOutputRAII RAII(*m_MetaProcessor);
          TI.SetPrompt(Prompt.c_str());
          Done = TI.ReadInput() == textinput::TextInput::kRREOF;
          TI.TakeInput(Line);
          if (Done && Line.empty())
            break;
        }

        cling::Interpreter::CompilationResult compRes;
        const int indent = m_MetaProcessor->process(Line, compRes);

        // Quit requested?
        if (indent < 0)
          break;

        Prompt.replace(7, std::string::npos,
           m_MetaProcessor->getInterpreter().isRawInputEnabled() ? "! " : "$ ");

        // Continuation requested?
        if (indent > 0) {
          Prompt.append(1, '?');
          Prompt.append(indent * 3, ' ');
        }
      }
      catch(InterpreterException& e) {
        if (!e.diagnose()) {
          cling::errs() << ">>> Caught an interpreter exception!\n"
                        << ">>> " << e.what() << '\n';
        }
      }
      catch(std::exception& e) {
        cling::errs() << ">>> Caught a std::exception!\n"
                     << ">>> " << e.what() << '\n';
      }
      catch(...) {
        cling::errs() << "Exception occurred. Recovering...\n";
      }
    }
    m_MetaProcessor->getOuts().flush();
  }

  void UserInterface::PrintLogo() {
    llvm::raw_ostream& outs = m_MetaProcessor->getOuts();
    const clang::LangOptions& LangOpts
      = m_MetaProcessor->getInterpreter().getCI()->getLangOpts();
    if (LangOpts.CPlusPlus) {
      outs << "\n"
        "****************** CLING ******************\n"
        "* Type C++ code and press enter to run it *\n"
        "*             Type .q to exit             *\n"
        "*******************************************\n";
    } else {
      outs << "\n"
        "***************** CLING *****************\n"
        "* Type C code and press enter to run it *\n"
        "*            Type .q to exit            *\n"
        "*****************************************\n";
    }
  }
} // end namespace cling
