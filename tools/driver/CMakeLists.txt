#------------------------------------------------------------------------------
# CLING - the C++ LLVM-based InterpreterG :)
#
# This file is dual-licensed: you can choose to license it under the University
# of Illinois Open Source License or the GNU Lesser General Public License. See
# LICENSE.TXT for details.
#------------------------------------------------------------------------------

# Keep symbols for JIT resolution
set(LLVM_NO_DEAD_STRIP 1)

if(BUILD_SHARED_LIBS)
  set(LIBS
    LLVMSupport
    clangFrontendTool
    clingInterpreter
    clingMetaProcessor
    clingUserInterface
    clingUtils
  )
  add_cling_executable(cling
    cling.cpp
  )
else()
  set(LIBS
    LLVMSupport
    clangFrontendTool
    clingUserInterface
  )
  add_cling_executable(cling
    cling.cpp
    $<TARGET_OBJECTS:obj.clingInterpreter>
    $<TARGET_OBJECTS:obj.clingMetaProcessor>
    $<TARGET_OBJECTS:obj.clingUtils>
  )
endif(BUILD_SHARED_LIBS)

set_target_properties(cling
  PROPERTIES ENABLE_EXPORTS 1)

if(MSVC)
  set_target_properties(cling PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS 1)

  # Internal string
  set(cling_exports ?kEmptyCollection@valuePrinterInternal@cling@@3QEBDEB)

  # RTTI/C++ symbols
  set(cling_exports ${cling_exports} ??_7type_info@@6B@
    ?__type_info_root_node@@3U__type_info_node@@A
    ?nothrow@std@@3Unothrow_t@1@B
  )

  # Compiler added symbols for static variables. NOT for VStudio < 2015
  set(cling_exports ${cling_exports} _Init_thread_abort
    _Init_thread_footer _Init_thread_header
  )

  # new/delete variants needed when linking to static msvc runtime (esp. Debug)
  set(cling_exports ${cling_exports}
    #??2@YAPEAX_KPEBDH@Z  ## not used in cling
    #??_U@YAPEAX_KPEBDH@Z ## not used in cling
    ??2@YAPEAX_K@Z
    ??3@YAXPEAX@Z
    ??_U@YAPEAX_K@Z
    ??_V@YAXPEAX@Z
  )

  # Most (if not all) of these MSVC decided are inlines that aren't exported
  set(cling_exports ${cling_exports} ?print@Decl@clang@@QEBAXAEAVraw_ostream@llvm@@I_N@Z
    ?getAsString@QualType@clang@@SA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEBVType@2@VQualifiers@2@@Z
    ??6raw_ostream@llvm@@QEAAAEAV01@PEBX@Z
    ?getQualifiedNameAsString@NamedDecl@clang@@QEBA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ
    ?insert_imp_big@SmallPtrSetImplBase@llvm@@AEAA?AU?$pair@PEBQEBX_N@std@@PEBX@Z
    ?getAsStringInternal@QualType@clang@@SAXPEBVType@2@VQualifiers@2@AEAV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AEBUPrintingPolicy@2@@Z
    ?decls_begin@DeclContext@clang@@QEBA?AVdecl_iterator@12@XZ
    ?field_begin@RecordDecl@clang@@QEBA?AV?$specific_decl_iterator@VFieldDecl@clang@@@DeclContext@2@XZ
    ?errs@llvm@@YAAEAVraw_ostream@1@XZ
    ?grow_pod@SmallVectorBase@llvm@@IEAAXPEAX_K1@Z
    ?write@raw_ostream@llvm@@QEAAAEAV12@E@Z
    ?write@raw_ostream@llvm@@QEAAAEAV12@PEBD_K@Z
    ?castFromDeclContext@Decl@clang@@SAPEAV12@PEBVDeclContext@2@@Z
    ??1raw_ostream@llvm@@UEAA@XZ
    ??1raw_string_ostream@llvm@@UEAA@XZ
    ?flush_nonempty@raw_ostream@llvm@@AEAAXXZ
    ?getASTContext@Decl@clang@@QEBAAEAVASTContext@2@XZ
    ?handle@raw_ostream@llvm@@EEAAXXZ
    ?preferred_buffer_size@raw_ostream@llvm@@MEBA_KXZ
    ?write_impl@raw_string_ostream@llvm@@EEAAXPEBD_K@Z
    ?castToDeclContext@Decl@clang@@SAPEAVDeclContext@2@PEBV12@@Z
    ?classof@DeclContext@clang@@SA_NPEBVDecl@2@@Z
    ?dump@Decl@clang@@QEBAXAEAVraw_ostream@llvm@@_N@Z
    ?current_pos@raw_svector_ostream@llvm@@EEBA_KXZ
    ?write_impl@raw_svector_ostream@llvm@@EEAAXPEBD_K@Z
    ?pwrite_impl@raw_svector_ostream@llvm@@EEAAXPEBD_K1@Z
    ?SetBufferAndMode@raw_ostream@llvm@@AEAAXPEAD_KW4BufferKind@12@@Z
  )

  if ($<CONFIG:Debug>)
    set(cling_exports ${cling_exports}
      ??$dyn_cast@VValueDecl@clang@@$$CBVDecl@2@@llvm@@YAPEBVValueDecl@clang@@PEBVDecl@2@@Z
    )
  endif()

  # List to '/EXPORT:sym0 /EXPORT:sym1 /EXPORT:sym2 ...'
  foreach(sym ${cling_exports})
    set(cling_link_str "${cling_link_str} /EXPORT:${sym}")
  endforeach(sym ${cling_exports})

  set_property(TARGET cling APPEND_STRING PROPERTY LINK_FLAGS ${cling_link_str})

endif(MSVC)

target_link_libraries(cling
  ${LIBS}
  )

install(TARGETS cling
  RUNTIME DESTINATION bin)
