set(example_as_test_suite)
if(${ENABLE_EXAMPLES})
  if(${ENABLE_MPI})
    set(example_as_test_suite
        test/hybrid-test-suite.cc
    )
  else()
    set(example_as_test_suite
        test/mtp-test-suite.cc
    )
  endif()
endif()

# 当启用 MPI 时，让 mtp 这个库也链接 MPI::MPI_CXX，
# 这样就能继承 NS3_MPI 宏和 MPI 的 include / link 选项
set(mtp_libraries ${libnetwork})
if(${ENABLE_MPI})
  list(APPEND mtp_libraries MPI::MPI_CXX)
endif()

build_lib(
  LIBNAME mtp
  SOURCE_FILES
    model/logical-process.cc
    model/mtp-interface.cc
    model/multithreaded-simulator-impl.cc
  HEADER_FILES
    model/logical-process.h
    model/mtp-interface.h
    model/multithreaded-simulator-impl.h
  LIBRARIES_TO_LINK ${mtp_libraries}
  TEST_SOURCES ${example_as_test_suite}
)
