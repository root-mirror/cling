#------------------------------------------------------------------------------
# CLING - the C++ LLVM-based InterpreterG :)
#
# This file is dual-licensed: you can choose to license it under the University
# of Illinois Open Source License or the GNU Lesser General Public License. See
# LICENSE.TXT for details.
#------------------------------------------------------------------------------

set( LLVM_LINK_COMPONENTS
  binaryformat
  support
)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/textinput)
  set(TEXTINPUTSRC ${CMAKE_SOURCE_DIR}/core/textinput/src/)
  include_directories(${TEXTINPUTSRC})
else()
  # For cling, install textinput *.h in include/cling/UserInterface/textinput.
  install(DIRECTORY ${TEXTINPUTSRC}textinput
    DESTINATION include/cling/UserInterface
    FILES_MATCHING
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "*.cpp" EXCLUDE
    PATTERN "doc" EXCLUDE
    PATTERN "*.h"
    )
endif()

add_cling_library(clingUserInterface
  UserInterface.cpp
  ${TEXTINPUTSRC}textinput/Editor.cpp
  ${TEXTINPUTSRC}textinput/History.cpp
  ${TEXTINPUTSRC}textinput/KeyBinding.cpp
  ${TEXTINPUTSRC}textinput/Range.cpp
  ${TEXTINPUTSRC}textinput/SignalHandler.cpp
  ${TEXTINPUTSRC}textinput/StreamReader.cpp
  ${TEXTINPUTSRC}textinput/StreamReaderUnix.cpp
  ${TEXTINPUTSRC}textinput/StreamReaderWin.cpp
  ${TEXTINPUTSRC}textinput/TerminalConfigUnix.cpp
  ${TEXTINPUTSRC}textinput/TerminalDisplay.cpp
  ${TEXTINPUTSRC}textinput/TerminalDisplayUnix.cpp
  ${TEXTINPUTSRC}textinput/TerminalDisplayWin.cpp
  ${TEXTINPUTSRC}textinput/TextInput.cpp
  ${TEXTINPUTSRC}textinput/TextInputContext.cpp

  LINK_LIBS
  clingMetaProcessor
  clingInterpreter
  clingUtils
)

if( MSVC )
  # Don't use Unicode in the User Interface (command prompt)
  remove_definitions(-DUNICODE -D_UNICODE)
endif()

if(UNIX)
  set_source_files_properties(UserInterface.cpp COMPILE_FLAGS "-fexceptions")
endif()
