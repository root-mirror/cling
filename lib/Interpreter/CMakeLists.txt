#------------------------------------------------------------------------------
# CLING - the C++ LLVM-based InterpreterG :)
#
# This file is dual-licensed: you can choose to license it under the University
# of Illinois Open Source License or the GNU Lesser General Public License. See
# LICENSE.TXT for details.
#------------------------------------------------------------------------------

set(LIBS
  clingUtils
  clangCodeGen
  clangDriver
  clangFrontend
  clangParse
  clangSema
  clangAnalysis
  clangEdit
  clangRewrite
  clangRewriteFrontend
  clangSerialization
  clangAST
  clangBasic
  clangLex
)

set(LLVM_LINK_COMPONENTS
  analysis
  binaryformat
  core
  coroutines
  coverage
  executionengine
  ipo
  lto
  mc
  object
  option
  orcjit
  runtimedyld
  scalaropts
  support
  target
  transformutils
  binaryformat
  ${LLVM_TARGETS_TO_BUILD}
)

# clingInterpreter depends on Options.inc to be tablegen-ed
# (target ClangDriverOptions) from in-tree builds.
set(CLING_DEPENDS)
if(TARGET ClangDriverOptions)
  set(CLING_DEPENDS ClangDriverOptions)
endif()
# clangSema will make sure all of the dependencies of clingInterpreter are met.
if(TARGET clangSema)
  set(CLING_DEPENDS "${CLING_DEPENDS};clangSema")
endif()


add_cling_library(clingInterpreter OBJECT
  AutoSynthesizer.cpp
  AutoloadCallback.cpp
  ASTTransformer.cpp
  BackendPasses.cpp
  CheckEmptyTransactionTransformer.cpp
  CIFactory.cpp
  ClangInternalState.cpp
  ClingCodeCompleteConsumer.cpp
  ClingPragmas.cpp
  DeclCollector.cpp
  DeclExtractor.cpp
  DefinitionShadower.cpp
  DeclUnloader.cpp
  DeviceKernelInliner.cpp
  DynamicLibraryManager.cpp
  DynamicLibraryManagerSymbol.cpp
  DynamicLookup.cpp
  DynamicExprInfo.cpp
  Exception.cpp
  ExternalInterpreterSource.cpp
  ForwardDeclPrinter.cpp
  IncrementalCUDADeviceCompiler.cpp
  IncrementalExecutor.cpp
  IncrementalJIT.cpp
  IncrementalParser.cpp
  Interpreter.cpp
  InterpreterCallbacks.cpp
  InvocationOptions.cpp
  LookupHelper.cpp
  NullDerefProtectionTransformer.cpp
  RequiredSymbols.cpp
  Transaction.cpp
  TransactionUnloader.cpp
  ValueExtractionSynthesizer.cpp
  Value.cpp
  ValuePrinter.cpp
  ValuePrinterSynthesizer.cpp

  DEPENDS
  ${CLING_DEPENDS}

  LINK_LIBS
  ${LIBS}
  ${CMAKE_DL_LIBS}
  )


if (UNIX)
  set_source_files_properties(Exception.cpp COMPILE_FLAGS "-fexceptions -frtti")
  set_source_files_properties(Interpreter.cpp COMPILE_FLAGS "-fexceptions")

  # Remove all -I from CMAKE_CXX_FLAGS
  string(REPLACE ";" " " __flags "${CMAKE_CXX_FLAGS}")
  string(REGEX REPLACE "-I[^ ]+" "" CLING_COMPILER_FLAGS_NO_I "${__flags}")

  option(CLING_CXX_PATH "Compiler cling will invoke for c++ headers." "")
  option(CLING_CXX_HEADERS "Path cling will use for c++ headers." "")

  function(stripNewLine strVal varName)
    string(STRIP "${strVal}" strVal)
    string(REGEX REPLACE "\\n$" "" strVal "${strVal}")
    SET(${varName} ${strVal} PARENT_SCOPE)
  endfunction()

  if(NOT CLING_CXX_PATH)
    # Remove absolute path from CMAKE_CXX_COMPILER
    get_filename_component(_name ${CMAKE_CXX_COMPILER} NAME)
    get_filename_component(_path ${CMAKE_CXX_COMPILER} PATH)
    # This should probably be more general...but how?
    if(_name STREQUAL "ccache" OR _name STREQUAL "distcc")
      separate_arguments(_arg_list UNIX_COMMAND "${CMAKE_CXX_COMPILER_ARG1}")
      if (_arg_list)
        list(GET _arg_list 0 _name)
        string(STRIP "${_name}" _name)
        if (APPLE)
          execute_process(COMMAND xcrun -f ${_name}
                          OUTPUT_VARIABLE CLING_CXX_FOUND
                          OUTPUT_STRIP_TRAILING_WHITESPACE)
          stripNewLine("${CLING_CXX_FOUND}" CLING_CXX_FOUND)
        else()
          find_program(_cling_cxx_path "${_name}")
          execute_process(COMMAND ${_cling_cxx_path} -xc++ -E -v /dev/null
                          OUTPUT_QUIET ERROR_VARIABLE _cling_cxx_path)

          if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            execute_process(
              COMMAND echo ${_cling_cxx_path}
              COMMAND grep "COLLECT_GCC="
              OUTPUT_VARIABLE _cling_cxx_path)
              string(REPLACE "COLLECT_GCC=" "" _cling_cxx_path "${_cling_cxx_path}")

          elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            execute_process(
              COMMAND echo ${_cling_cxx_path}
              COMMAND grep "/${_name}.*\" -cc1"
              OUTPUT_VARIABLE _cling_clng_path)

            if(NOT _cling_clng_path)
              execute_process(
                COMMAND echo ${_cling_cxx_path}
                COMMAND grep "/clang.*\" -cc1"
                OUTPUT_VARIABLE _cling_clng_path)
            endif()

            separate_arguments(_arg_list UNIX_COMMAND "${_cling_clng_path}")
            if (_arg_list)
              list(GET _arg_list 0 _cling_cxx_path)
            endif()
          endif()

          stripNewLine("${_cling_cxx_path}" _cling_cxx_path)
          set(CLING_CXX_FOUND "${_cling_cxx_path}")
        endif()

        if (NOT EXISTS "${CLING_CXX_FOUND}")
          find_program(CLING_CXX_FOUND "${_name}")
        endif()
      else()
        set(CLING_CXX_FOUND "")
        set(_name "")
      endif()

      if (EXISTS ${CLING_CXX_FOUND})
        set(CLING_CXX_PATH ${CLING_CXX_FOUND})
        get_filename_component(_name ${CLING_CXX_PATH} NAME)
        get_filename_component(_path ${CLING_CXX_PATH} PATH)
      else()
        set(CLING_CXX_PATH "${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ARG1}")
        if(_name)
          set(CLING_CXX_RLTV "${_name}")
        endif()
        set(_path "__THISREALLYBETTERNOTBEINPATH_THANKS__")
      endif()
    else()
      # FIXME: In some ccache setups we can have a soft link pointing to ccache
      # binary. Eg. /usr/local/gcc -> /usr/bin/ccache. Resolving the realpath
      # we will get to the ccache and not the intended compiler binary. This
      # could be fixed if we run 'gcc -###' which will give us the correct info.
      get_filename_component(_realpath ${CMAKE_CXX_COMPILER} REALPATH)
      get_filename_component(_name ${_realpath} NAME)
      get_filename_component(_path ${_realpath} PATH)
    endif()

    # Test if path compiler is on PATH.
    string(REPLACE ":" ";" _pathlist $ENV{PATH})
    foreach (_pathcomp ${_pathlist})
      get_filename_component(_pathcomp ${_pathcomp} REALPATH)
      if (_path STREQUAL _pathcomp)
        # This adds a lot of unneccessary flags, but may be useful if there's
        # a flag that should be passed to cling.
        set(CLING_CXX_RLTV ${_name})
        break()
      endif()
    endforeach()

    # FIXME: Perhaps CLING_CXX_RLTV should have a better name?
    if(NOT CLING_CXX_RLTV AND NOT CLING_CXX_PATH)
      # We got nothing, just use whatever CMake is using.
      set(CLING_CXX_PATH ${CMAKE_CXX_COMPILER})
    endif()

    # If CMAKE_CXX_FLAGS contains --gcc-toolchain= then that should be passed on
    string(FIND "${CMAKE_CXX_FLAGS}" "--gcc-toolchain=" cling_gcc_toolchain)
    if ("${cling_gcc_toolchain}" GREATER -1)
      # TODO Refactor these two into common function
      if (CLING_CXX_PATH)
        string(FIND "${CLING_CXX_PATH}" "--gcc-toolchain=" cling_gcc_toolchain)
        if ("${cling_gcc_toolchain}" EQUAL -1)
          set(CLING_CXX_PATH_ARGS "--gcc-toolchain=${gcctoolchain}")
        endif()
      endif()
      if (CLING_CXX_RLTV)
        string(FIND "${CLING_CXX_RLTV}" "--gcc-toolchain=" cling_gcc_toolchain)
        if ("${cling_gcc_toolchain}" EQUAL -1)
          set(CLING_CXX_RLTV "${CLING_CXX_RLTV} --gcc-toolchain=${gcctoolchain}")
        endif()
      endif()
    endif()
  endif()

  if(NOT CLING_CXX_HEADERS)
    if (CLING_CXX_PATH)
      execute_process(COMMAND ${CLING_CXX_PATH} ${CLING_CXX_PATH_ARGS} -xc++ -E -v /dev/null
                      OUTPUT_QUIET ERROR_VARIABLE CLING_CXX_HEADERS)
      set(CLING_CXX_PATH "${CLING_CXX_PATH} ${CLING_CXX_PATH_ARGS}")
    else()
      # convert CMAKE_CXX_FLAGS to a list for execute_process
      string(REPLACE "-fdiagnostics-color=always" "" cling_tmp_arg_list ${CMAKE_CXX_FLAGS})
      string(REPLACE "-fcolor-diagnosics" "" cling_tmp_arg_list ${cling_tmp_arg_list})
      string(REPLACE " " ";" cling_tmp_arg_list ${cling_tmp_arg_list})
      execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ARG1} ${cling_tmp_arg_list} -xc++ -E -v /dev/null
                      OUTPUT_QUIET ERROR_VARIABLE CLING_CXX_HEADERS)
    endif()

    execute_process(
      COMMAND echo ${CLING_CXX_HEADERS}
      COMMAND sed -n -e /^.include/,\$\{ -e /^\\\ \\\/.*++/p -e \}
      OUTPUT_VARIABLE CLING_CXX_HEADERS)

    stripNewLine("${CLING_CXX_HEADERS}" CLING_CXX_HEADERS)
  endif()

  if (NOT EXISTS ${CLING_CXX_HEADERS})
    string(REPLACE "\n" ";" _cxx_inc_paths ${CLING_CXX_HEADERS})
    foreach(_cxx_inc_path ${_cxx_inc_paths})
      string(STRIP "${_cxx_inc_path}" _cxx_inc_path)
      if (NOT EXISTS ${_cxx_inc_path})
        set(_cxx_inc_join "")
        break()
      endif()
      if(_cxx_inc_join)
        set(_cxx_inc_join "${_cxx_inc_join}:${_cxx_inc_path}")
      else()
        set(_cxx_inc_join "${_cxx_inc_path}")
      endif()
    endforeach()
    set(CLING_CXX_HEADERS "${_cxx_inc_join}")
    if (NOT CLING_CXX_HEADERS)
      MESSAGE(WARNING "Cannot determine location of C++ headers for runtime.")
    endif()
  endif()

  MESSAGE(STATUS "Cling will look for C++ headers in '${CLING_CXX_HEADERS}' at runtime.")

  # In modules builds we 'mount' our own stl modulemap for libstdc++. In order to do this,
  # we need to know where is ROOT/cling STL.
  set_property(GLOBAL PROPERTY ROOT_CLING_CXX_HEADERS_LOCATION "${CLING_CXX_HEADERS}")

  # FIXME: We should use file(GENERATE) cmake command.
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
    "
    #define CLING_CXX_INCL \"${CLING_CXX_HEADERS}\"
    #define CLING_INCLUDE_PATHS \"${CLING_INCLUDE_PATHS}\"
  ")
  if (CMAKE_OSX_SYSROOT)
    # CMAKE_OSX_SYSROOT hardcodes the concrete version of the sdk
    # (eg .../MacOSX11.1.sdk) which changes after every update of XCode. We use
    # the assumption that in the parent folder there is a symlink MacOSX.sdk
    # which points to the current active sdk. This change allows releases
    # to work when the users update their sdks.
    # FIXME: That is a horrible hack and we should teach CIFactory to pick up
    # the SDK directory at runtime, just as we do for the include paths to C++.
    set (OSX_SYSROOT_DEFAULT_SDK ${CMAKE_OSX_SYSROOT})
    if (${OSX_SYSROOT_DEFAULT_SDK} MATCHES "MacOSX[.0-9]+\.sdk")
      get_filename_component(OSX_SYSROOT_DEFAULT_SDK ${OSX_SYSROOT_DEFAULT_SDK} DIRECTORY)
      set (OSX_SYSROOT_DEFAULT_SDK ${OSX_SYSROOT_DEFAULT_SDK}/MacOSX.sdk/)
    endif()

    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
      "
      #define CLING_OSX_SYSROOT \"${OSX_SYSROOT_DEFAULT_SDK}\"
    ")
  endif()
  if (CLING_CXX_PATH)
    MESSAGE(STATUS "And if not found, will invoke: '${CLING_CXX_PATH}' for them.")
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
      "
      #define CLING_CXX_PATH \"${CLING_CXX_PATH} ${CMAKE_CXX_FLAGS_NO_I} ${CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE}}\"
    ")
  endif()
  if (CLING_CXX_RLTV)
    MESSAGE(STATUS "And then fallback to: '${CLING_CXX_RLTV}'")
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
      "
      #define CLING_CXX_RLTV \"${CLING_CXX_RLTV} ${CMAKE_CXX_FLAGS_NO_I} ${CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE}}\"
    ")
  endif()
else()
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
      "
      #define CLING_INCLUDE_PATHS \"${CLING_INCLUDE_PATHS}\"
      #define CLING_UCRT_VERSION  \"$ENV{UCRTVersion}\"
    ")
endif()

# Make sure this goes last so so we can pick up any changes that occured
# Also means cling-compiledata.h.in should be edited never cling-compiledata.h

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                   ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
                   ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h
                   MAIN_DEPENDENCY ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h.in
                   COMMENT "Updating cling-compiledata.h")

add_file_dependencies(${CMAKE_CURRENT_SOURCE_DIR}/CIFactory.cpp
                      ${CMAKE_CURRENT_BINARY_DIR}/cling-compiledata.h)
