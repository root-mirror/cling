cmake_minimum_required(VERSION 3.4.3)

# find LLVM
# eventually set CMAKE_PREFIX_PATH=/opt/clang+llvm-5.0.2 or similar
# and remove CMakeCache.txt
#
set(CLING_LLVM_VERSION 5)
message(STATUS "Searching LLVM ${CLING_LLVM_VERSION} in CMAKE_PREFIX_PATH")
find_package(LLVM ${CLING_LLVM_VERSION} REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_INSTALL_PREFIX}")
message(STATUS "Using LLVMConfig.cmake ${LLVM_CONFIG}")

# If we are not building as a part of LLVM, build Cling as an
# standalone project, using LLVM as an external library:
if( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
  project(Cling)

  if(WIN32)
    # We need cmake to support exporting of symbols not only from libraries but
    # from executables too. This way cling can find symbols from its own
    # executable during runtime.
    cmake_minimum_required(VERSION 3.6.2)
  endif(WIN32)

  # Rely on llvm-config.
  set(CONFIG_OUTPUT)
  # LLVM_CONFIG is the path to LLVMConfig.cmake
  find_program(LLVM_CONFIG_EXE "llvm-config" PATHS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
  if(LLVM_CONFIG_EXE)
    message(STATUS "Found LLVM_CONFIG_EXE as ${LLVM_CONFIG_EXE}")
    set(CONFIG_COMMAND ${LLVM_CONFIG_EXE}
      "--src-root")
    execute_process(
      COMMAND ${CONFIG_COMMAND}
      RESULT_VARIABLE HAD_ERROR
      OUTPUT_VARIABLE CONFIG_OUTPUT
    )
    if(NOT HAD_ERROR)
      string(REGEX REPLACE
        "[ \t]*[\r\n]+[ \t]*" ";"
        CONFIG_OUTPUT ${CONFIG_OUTPUT})
    else()
      string(REPLACE ";" " " CONFIG_COMMAND_STR "${CONFIG_COMMAND}")
      message(STATUS "${CONFIG_COMMAND_STR}")
      message(FATAL_ERROR "llvm-config failed with status ${HAD_ERROR}")
    endif()
  else()
    message(FATAL_ERROR "llvm-config not found -- ${LLVM_CONFIG_EXE}")
  endif()

  list(GET CONFIG_OUTPUT 0 MAIN_SRC_DIR)

  if(NOT MSVC_IDE)
    # Assertions should follow llvm-config's.
    mark_as_advanced(LLVM_ENABLE_ASSERTIONS)
  endif()

  set(LLVM_MAIN_SRC_DIR ${MAIN_SRC_DIR} CACHE PATH "Path to LLVM source tree")
  message(STATUS "Using LLVM_MAIN_SRC_DIR ${LLVM_MAIN_SRC_DIR}")

  find_program(LLVM_TABLEGEN_EXE "llvm-tblgen" PATHS ${LLVM_TOOLS_BINARY_DIR}
    NO_DEFAULT_PATH)

  set(LLVM_CMAKE_PATH "${LLVM_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}/cmake/llvm")
  set(LLVMCONFIG_FILE "${LLVM_CMAKE_PATH}/LLVMConfig.cmake")
  if(EXISTS ${LLVMCONFIG_FILE})
    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_PATH}")
    include(${LLVMCONFIG_FILE})
  else()
    message(FATAL_ERROR "Not found: ${LLVMCONFIG_FILE}")
  endif()

  # They are used as destination of target generators.
  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
  if(WIN32 OR CYGWIN)
    # DLL platform -- put DLLs into bin.
    set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
  else()
    set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
  endif()

  option(LLVM_INSTALL_TOOLCHAIN_ONLY
    "Only include toolchain files in the 'install' target." OFF)

  option(LLVM_FORCE_USE_OLD_HOST_TOOLCHAIN
    "Set to ON to force using an old, unsupported host toolchain." OFF)

  include(AddLLVM)
  include(TableGen)
  include(HandleLLVMOptions)
  include(VersionFromVCS)

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")

  if (NOT DEFINED LLVM_INCLUDE_TESTS)
    set(LLVM_INCLUDE_TESTS ON)
  endif()

  include_directories("${LLVM_INCLUDE_DIRS}")
  link_directories("${LLVM_LIBRARY_DIRS}")

  set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
  set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )
  set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )

  if(LLVM_INCLUDE_TESTS)
    set(Python_ADDITIONAL_VERSIONS 2.7)
    include(FindPythonInterp)
    if(NOT PYTHONINTERP_FOUND)
      message(FATAL_ERROR
"Unable to find Python interpreter, required for builds and testing.

Please install Python or specify the PYTHON_EXECUTABLE CMake variable.")
    endif()

    if( ${PYTHON_VERSION_STRING} VERSION_LESS 2.7 )
      message(FATAL_ERROR "Python 2.7 or newer is required")
    endif()

    # Check prebuilt llvm/utils.
    if(EXISTS ${LLVM_TOOLS_BINARY_DIR}/FileCheck${CMAKE_EXECUTABLE_SUFFIX}
        AND EXISTS ${LLVM_TOOLS_BINARY_DIR}/count${CMAKE_EXECUTABLE_SUFFIX}
        AND EXISTS ${LLVM_TOOLS_BINARY_DIR}/not${CMAKE_EXECUTABLE_SUFFIX})
      set(LLVM_UTILS_PROVIDED ON)
    endif()

    if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      set(LLVM_LIT ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      if(NOT LLVM_UTILS_PROVIDED)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/FileCheck utils/FileCheck)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/count utils/count)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/not utils/not)
        set(LLVM_UTILS_PROVIDED ON)
        set(CLING_TEST_DEPS FileCheck count not clang)
      endif()
      set(UNITTEST_DIR ${LLVM_MAIN_SRC_DIR}/utils/unittest)
      if(EXISTS ${UNITTEST_DIR}/googletest/include/gtest/gtest.h
          AND NOT EXISTS ${LLVM_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}
          AND EXISTS ${UNITTEST_DIR}/CMakeLists.txt)
        add_subdirectory(${UNITTEST_DIR} utils/unittest)
      endif()
    else()
      # Seek installed Lit.
      find_program(LLVM_LIT "lit.py" PATHS ${LLVM_MAIN_SRC_DIR}/utils/lit
        DOC "Path to lit.py")
    endif()

    if(LLVM_LIT)
      # Define the default arguments to use with 'lit', and an option for the user
      # to override.
      set(LIT_ARGS_DEFAULT "-sv")
      if (MSVC OR XCODE)
        set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
      endif()
      set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

      # On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
      if( WIN32 AND NOT CYGWIN )
        set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
      endif()
    else()
      set(LLVM_INCLUDE_TESTS OFF)
    endif()
  endif()

  set( CLING_BUILT_STANDALONE 1 )
  set(BACKEND_PACKAGE_STRING "LLVM ${LLVM_PACKAGE_VERSION}")
else() # Building as part of LLVM
  set(BACKEND_PACKAGE_STRING "${PACKAGE_STRING}")
  # Try finding the LLVMConfig.cmake if we build against prebuilt LLVM
  set(LLVM_CMAKE_PATH "${LLVM_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}/cmake/llvm")
  set(LLVMCONFIG_FILE "${LLVM_CMAKE_PATH}/LLVMConfig.cmake")
  if(EXISTS ${LLVMCONFIG_FILE})
    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_PATH}")
    include(${LLVMCONFIG_FILE})
  endif()
  if (NOT CLANG_INCLUDE_DIRS)
    set (CLANG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/tools/clang/include"
      "${CMAKE_BINARY_DIR}/tools/clang/include")
  endif()
endif()

if( NOT "NVPTX" IN_LIST LLVM_TARGETS_TO_BUILD)
  message(FATAL_ERROR "NVPTX backend is not activated\n"
    "Please enable it via -DLLVM_TARGETS_TO_BUILD=\"host;NVPTX\"")
endif()

set(CLING_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CLING_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LLVM_TOOLS_BINARY_DIR "${LLVM_BINARY_DIR}/bin")
if(DEFINED ROOT_BINARY_DIR)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${LLVM_TOOLS_BINARY_DIR}")
endif()

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed. CMake would overwrite "
"the makefiles distributed with LLVM. Please create a directory and run cmake "
"from there, passing the path to this source directory as the last argument. "
"This process created the file `CMakeCache.txt' and the directory "
"`CMakeFiles'. Please delete them.")
endif()

# From ROOT:
function(cling_add_cxx_flag var flag)
  string(REGEX REPLACE "[-.+/:= ]" "_" flag_esc "${flag}")
  CHECK_CXX_COMPILER_FLAG("${flag}" CXX_HAS${flag_esc})
  if(CXX_HAS${flag_esc})
    set(${var} "${${var}} ${flag}" PARENT_SCOPE)
  endif()
endfunction()

if(CLING_ENABLE_WARNINGS AND NOT LLVM_ENABLE_WARNINGS AND (LLVM_COMPILER_IS_GCC_COMPATIBLE OR CLANG_CL))
  # from HandleLLCMOptions.cmake:
  append("-Wall -W -Wno-unused-parameter -Wwrite-strings" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
  append("-Wcast-qual" CMAKE_CXX_FLAGS)

  # Turn off missing field initializer warnings for gcc to avoid noise from
  # false positives with empty {}. Turn them on otherwise (they're off by
  # default for clang).
  check_cxx_compiler_flag("-Wmissing-field-initializers" CXX_SUPPORTS_MISSING_FIELD_INITIALIZERS_FLAG)
  if (CXX_SUPPORTS_MISSING_FIELD_INITIALIZERS_FLAG)
    if (CMAKE_COMPILER_IS_GNUCXX)
      append("-Wno-missing-field-initializers" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    else()
      append("-Wmissing-field-initializers" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    endif()
  endif()

  if (LLVM_ENABLE_PEDANTIC AND LLVM_COMPILER_IS_GCC_COMPATIBLE)
    append("-pedantic" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    append("-Wno-long-long" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
  endif()

  add_flag_if_supported("-Wcovered-switch-default" COVERED_SWITCH_DEFAULT_FLAG)
  append_if(USE_NO_UNINITIALIZED "-Wno-uninitialized" CMAKE_CXX_FLAGS)
  append_if(USE_NO_MAYBE_UNINITIALIZED "-Wno-maybe-uninitialized" CMAKE_CXX_FLAGS)

  # Check if -Wnon-virtual-dtor warns even though the class is marked final.
  # If it does, don't add it. So it won't be added on clang 3.4 and older.
  # This also catches cases when -Wnon-virtual-dtor isn't supported by
  # the compiler at all.  This flag is not activated for gcc since it will
  # incorrectly identify a protected non-virtual base when there is a friend
  # declaration. Don't activate this in general on Windows as this warning has
  # too many false positives on COM-style classes, which are destroyed with
  # Release() (PR32286).
  if (NOT CMAKE_COMPILER_IS_GNUCXX AND NOT WIN32)
    set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++11 -Werror=non-virtual-dtor")
    CHECK_CXX_SOURCE_COMPILES("class base {public: virtual void anchor();protected: ~base();};
                               class derived final : public base { public: ~derived();};
                               int main() { return 0; }"
                              CXX_WONT_WARN_ON_FINAL_NONVIRTUALDTOR)
    set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
    append_if(CXX_WONT_WARN_ON_FINAL_NONVIRTUALDTOR
              "-Wnon-virtual-dtor" CMAKE_CXX_FLAGS)
  endif()

  # Enable -Wdelete-non-virtual-dtor if available.
  add_flag_if_supported("-Wdelete-non-virtual-dtor" DELETE_NON_VIRTUAL_DTOR_FLAG)

  # Check if -Wcomment is OK with an // comment ending with '\' if the next
  # line is also a // comment.
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -Wcomment")
  CHECK_C_SOURCE_COMPILES("// \\\\\\n//\\nint main() {return 0;}"
                          C_WCOMMENT_ALLOWS_LINE_WRAP)
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
  if (NOT C_WCOMMENT_ALLOWS_LINE_WRAP)
    append("-Wno-comment" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
  endif()

  # Enable -Wstring-conversion to catch misuse of string literals.
  add_flag_if_supported("-Wstring-conversion" STRING_CONVERSION_FLAG)
endif()

# Add appropriate flags for GCC
if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-common -Woverloaded-virtual")
  if (NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-strict-aliasing")
  endif ()

  foreach (check_flag "-Wno-nested-anon-types" "-Wno-covered-switch-default" "-Wno-unused-local-typedef")
    cling_add_cxx_flag(CMAKE_CXX_FLAGS ${check_flag})
  endforeach ()
endif ()

# The package needs to be compiler without RTTI information
if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
  if(NOT (MSVC_VERSION LESS 1910))
    # Add the /std:c++14 flag for Visual Studio 2017 (and up)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++14")
  endif()
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

if(MSVC)
  add_definitions(/DNOMINMAX)
endif()

if (APPLE)
  set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-flat_namespace -Wl,-undefined -Wl,suppress")
endif ()

include(CMakeParseArguments)

macro(add_cling_subdirectory name)
  add_llvm_subdirectory(CLING TOOL ${name})
endmacro()

file (STRINGS "VERSION" CLING_VERSION)
string(REGEX REPLACE "([0-9]).[0-9]+~[a-zA-Z]+" "\\1" CLING_VERSION_MAJOR ${CLING_VERSION})
string(REGEX REPLACE "[0-9].([0-9]+)~[a-zA-Z]+" "\\1" CLING_VERSION_MINOR ${CLING_VERSION})

if(DEFINED ROOT_BINARY_DIR)
  # Building as part of ROOT; visibility is "inherited" from ROOT/interpreter.
  set(CLING_VERSION ROOT_${CLING_VERSION})
else()
  # Lots of cling is needed at runtime (e.g. Value, or Interpreter::Evaluate()).
  # The JIT needs to be able to resolve these symbols from cling; unhide them.
  set(CMAKE_CXX_VISIBILITY_PRESET default)
  set(CMAKE_C_VISIBILITY_PRESET default)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN "ON")
endif()
message(STATUS "Cling version (from VERSION file): ${CLING_VERSION}")

set(CLING_VERSION_PATCHLEVEL 0)

macro(set_cling_windows_version_resource_properties name)
  if(DEFINED windows_resource_file)
    set_windows_version_resource_properties(${name} ${windows_resource_file}
      VERSION_MAJOR ${CLING_VERSION_MAJOR}
      VERSION_MINOR ${CLING_VERSION_MINOR}
      VERSION_PATCHLEVEL ${CLING_VERSION_PATCHLEVEL}
      VERSION_STRING "${CLING_VERSION} (${BACKEND_PACKAGE_STRING})"
      PRODUCT_NAME "cling")
  endif()
endmacro()

include_directories(BEFORE SYSTEM ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})

macro(add_cling_library name)
  cmake_parse_arguments(ARG
    "SHARED"
    ""
    "ADDITIONAL_HEADERS"
    ${ARGN})
  set(srcs)
  if(MSVC_IDE OR XCODE)
    # Add public headers
    file(RELATIVE_PATH lib_path
      ${CLING_SOURCE_DIR}/lib/
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if(NOT lib_path MATCHES "^[.][.]")
      file( GLOB_RECURSE headers
        ${CLING_SOURCE_DIR}/include/cling/${lib_path}/*.h
        ${CLING_SOURCE_DIR}/include/cling/${lib_path}/*.def
      )
      set_source_files_properties(${headers} PROPERTIES HEADER_FILE_ONLY ON)

      file( GLOB_RECURSE tds
        ${CLING_SOURCE_DIR}/include/cling/${lib_path}/*.td
      )
      source_group("TableGen descriptions" FILES ${tds})
      set_source_files_properties(${tds}} PROPERTIES HEADER_FILE_ONLY ON)

      if(headers OR tds)
        set(srcs ${headers} ${tds})
      endif()
    endif()
  endif(MSVC_IDE OR XCODE)
  if(srcs OR ARG_ADDITIONAL_HEADERS)
    set(srcs
      ADDITIONAL_HEADERS
      ${srcs}
      ${ARG_ADDITIONAL_HEADERS} # It may contain unparsed unknown args.
      )
  endif()
  if(ARG_SHARED)
    set(ARG_ENABLE_SHARED SHARED)
  endif()

  if (MSVC)
    # On Windows exceptions aren’t as generic as an x64 ABI.
    # Stack unwinding code must be generated for every function between the
    # throw and catch blocks.
    if (${name} STREQUAL "clingInterpreter")
      # All of libClingInterpreter is compiled with exceptions, mostly because
      # llvm_unreachable throws an exception. Otherwise it could be reduced:
      #    Exception.cpp, Interpreter.cpp, IncrementalParser.cpp,
      #    IncrementalExecutor.cpp
      set(cling_ex_file_match ".cpp$")
    elseif(${name} STREQUAL "clingUserInterface")
      # For libClingUserInterface, only UserInterface.cpp uses exceptions.
      set(cling_ex_file_match "^UserInterface.cpp$")
    endif()
    if(cling_ex_file_match)
      # needs to be on before llvm_add_library so flags can be set below
      set(LLVM_REQUIRES_EH ON)
      set(LLVM_REQUIRES_RTTI ON)
    endif()
  endif()

  llvm_add_library(${name} ${ARG_ENABLE_SHARED} ${ARG_UNPARSED_ARGUMENTS} ${srcs})

  if (MSVC AND cling_ex_file_match)
    # /EHs because cling_runtime_internal_throwIfInvalidPointer is extern “C”
    if (cling_ex_file_match)
      foreach(file_var ${ARGN})
        if (file_var MATCHES ${cling_ex_file_match})
          set_property(SOURCE ${file_var} APPEND_STRING PROPERTY COMPILE_FLAGS
                       " /D _HAS_EXCEPTIONS=1 /EHs /GR /wd4714 ")
        elseif (file_var MATCHES ".cpp$")
          set_property(SOURCE ${file_var} APPEND_STRING PROPERTY COMPILE_FLAGS
                       " /D _HAS_EXCEPTIONS=0 /EHs-c- /GR- ")
        endif()
      endforeach()
    endif()
  endif()

  if(TARGET ${name})
    target_link_libraries(${name} INTERFACE ${LLVM_COMMON_LIBS})

    if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY OR ${name} STREQUAL "libcling")
      install(TARGETS ${name}
        COMPONENT ${name}
        EXPORT ClingTargets
        LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
        ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
        RUNTIME DESTINATION bin)

      if (${ARG_SHARED} AND NOT CMAKE_CONFIGURATION_TYPES)
        add_custom_target(install-${name}
                          DEPENDS ${name}
                          COMMAND "${CMAKE_COMMAND}"
                                  -DCMAKE_INSTALL_COMPONENT=${name}
                                  -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
      endif()
    endif()
    set_property(GLOBAL APPEND PROPERTY CLING_EXPORTS ${name})
  else()
    # Add empty "phony" target
    add_custom_target(${name})
  endif()

  set_target_properties(${name} PROPERTIES FOLDER "Cling libraries")
  set_cling_windows_version_resource_properties(${name})
endmacro(add_cling_library)

macro(add_cling_executable name)
  add_llvm_executable( ${name} ${ARGN} )
  set_target_properties(${name} PROPERTIES FOLDER "Cling executables")
  set_cling_windows_version_resource_properties(${name})
endmacro(add_cling_executable)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/cling include/cling-c
    DESTINATION include
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "config.h" EXCLUDE
    PATTERN ".svn" EXCLUDE
    )

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/cling
    DESTINATION include
    FILES_MATCHING
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "*.inc"
    PATTERN "*.h"
    PATTERN "*.modulemap"
    )
endif()

add_definitions( -D_GNU_SOURCE -DCLING_VERSION=${CLING_VERSION})

option(CLING_INCLUDE_TESTS
       "Generate build targets for the Cling unit tests."
       ${LLVM_INCLUDE_TESTS})

if (NOT WIN32)
  set(cling_path_delim ":")
else()
  set(cling_path_delim ";")
endif()

if( CLING_INCLUDE_TESTS )
  set(cling_include_deflt ${CMAKE_INSTALL_PREFIX}/include
                          ${CMAKE_CURRENT_SOURCE_DIR}/include
                          ${CLANG_INCLUDE_DIRS}
                          ${LLVM_INCLUDE_DIRS}
  )

  # CLANG_INCLUDE_DIRS and LLVM_INCLUDE_DIRS can be a semicolon separated lists.
  string(REPLACE ";" "${cling_path_delim}" cling_include_deflt "${cling_include_deflt}")
endif()

if(NOT CLING_INCLUDE_PATHS)
 set(CLING_INCLUDE_PATHS "${cling_include_deflt}")
else()
 set(CLING_INCLUDE_PATHS "${CLING_INCLUDE_PATHS}${cling_path_delim}${cling_include_deflt}")
endif()

# All targets below may depend on all tablegen'd files.
get_property(CLANG_TABLEGEN_TARGETS GLOBAL PROPERTY CLANG_TABLEGEN_TARGETS)
list(APPEND LLVM_COMMON_DEPENDS ${CLANG_TABLEGEN_TARGETS})

# And llvm-intrinsics.
if (TARGET intrinsics_gen)
  list(APPEND LLVM_COMMON_DEPENDS intrinsics_gen)
endif()
if (TARGET clang-headers)
  list(APPEND LLVM_COMMON_DEPENDS clang-headers)
endif()

add_subdirectory(lib)

if( CLING_INCLUDE_TESTS )
  add_subdirectory(test)

  if(CLING_BUILT_STANDALONE)
    # Add a global check rule now that all subdirectories have been traversed
    # and we know the total set of lit testsuites.
    get_property(LLVM_LIT_TESTSUITES GLOBAL PROPERTY LLVM_LIT_TESTSUITES)
    get_property(LLVM_LIT_PARAMS GLOBAL PROPERTY LLVM_LIT_PARAMS)
    get_property(LLVM_LIT_DEPENDS GLOBAL PROPERTY LLVM_LIT_DEPENDS)
    get_property(LLVM_LIT_EXTRA_ARGS GLOBAL PROPERTY LLVM_LIT_EXTRA_ARGS)
    add_lit_target(check-all
      "Running all regression tests"
      ${LLVM_LIT_TESTSUITES}
      PARAMS ${LLVM_LIT_PARAMS}
      DEPENDS ${LLVM_LIT_DEPENDS}
      ARGS ${LLVM_LIT_EXTRA_ARGS}
      )
  endif()
endif()

add_subdirectory(tools)

option(CLING_INCLUDE_DOCS "Generate build targets for the Cling docs."
  ${LLVM_INCLUDE_DOCS})
if( CLING_INCLUDE_DOCS )
  add_subdirectory(docs)
endif()

if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cling.order")
  file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/cling.order")
endif()

if(CLING_ORDER_FILE STREQUAL "${CMAKE_CURRENT_BINARY_DIR}/cling.order")
  unset(CLING_ORDER_FILE CACHE)
  unset(CLING_ORDER_FILE)
endif()

set(CLING_ORDER_FILE "" CACHE FILEPATH
  "Order file to use when compiling cling in order to improve startup time.")

add_subdirectory(cmake/modules)
