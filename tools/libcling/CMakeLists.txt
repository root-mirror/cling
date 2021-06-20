set(LLVM_NO_DEAD_STRIP 1)

#get_target_property(clingInterpSrc clingInterpreter SOURCES)
#get_target_property(clingMetaSrc clingMetaProcessor SOURCES)
#get_target_property(clingUtilsSrc clingUtils SOURCES)
set(SOURCES
  $<TARGET_OBJECTS:obj.clingInterpreter>
  $<TARGET_OBJECTS:obj.clingMetaProcessor>
  $<TARGET_OBJECTS:obj.clingUtils>
  ADDITIONAL_HEADERS
  )

set(LIBS
  clangAnalysis
  clangDriver
  clangFrontend
  clangParse
  clangSema
  clangAST
  clangLex
  clangSerialization
  clangCodeGen
  clangBasic
  clangEdit

  clingUtils
)

set( LLVM_LINK_COMPONENTS
  analysis
  binaryformat
  core
  coverage
  executionengine
  ipo
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

find_library(DL_LIBRARY_PATH dl)
if (DL_LIBRARY_PATH)
  list(APPEND LIBS dl)
endif()

option(LIBCLING_BUILD_STATIC
  "Build libcling as a static library (in addition to a shared one)" OFF)

# We do C++...
# set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libclang.exports)

#if(MSVC)
  # Avoid LNK4197 not to spceify libclang.def here.
  # Each functions is exported as "dllexport" in include/clang-c.
  # KB835326
#  set(LLVM_EXPORTED_SYMBOL_FILE)
#endif()

if( LLVM_ENABLE_PIC )
  set(ENABLE_SHARED SHARED)
endif()

if((NOT LLVM_ENABLE_PIC OR LIBCLING_BUILD_STATIC) AND NOT WIN32)
  set(ENABLE_STATIC STATIC)
endif()

if(WIN32)
  set(output_name "libcling")
else()
  set(output_name "cling")
endif()

add_cling_library(libcling ${ENABLE_SHARED} ${ENABLE_STATIC}
  OUTPUT_NAME ${output_name}
  ${SOURCES}
#  DEPENDS clang-headers

  LINK_LIBS
  ${LIBS}

  LINK_COMPONENTS
#  Core
#  Support
  )

set_target_properties(libcling
        PROPERTIES ENABLE_EXPORTS 1)

if(ENABLE_SHARED)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(LIBCLING_LINK_FLAGS " -Wl,-compatibility_version -Wl,1")
    if (DEFINED ${LLVM_SUBMIT_VERSION})
      set(LIBCLING_LINK_FLAGS
        "${LIBCLING_LINK_FLAGS} -Wl,-current_version -Wl,${LLVM_SUBMIT_VERSION}.${LLVM_SUBMIT_SUBVERSION}")
    endif()

    set_property(TARGET libcling APPEND_STRING PROPERTY
                 LINK_FLAGS ${LIBCLING_LINK_FLAGS})
  endif()
endif()
