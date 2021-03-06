if (APPLE)
  execute_process(COMMAND xcode-select --print-path OUTPUT_VARIABLE xcode_developer_path OUTPUT_STRIP_TRAILING_WHITESPACE)
  find_library(LibClang_LIBRARY NAMES clang PATHS "${xcode_developer_path}/Toolchains/XcodeDefault.xctoolchain/usr/lib" NO_DEFAULT_PATH)
else()
  find_library(LibClang_LIBRARY NAMES clang libclang HINTS "${LibClang_ROOT}/lib")
  set(LibClang_DLL "${LibClang_ROOT}/bin/libclang.dll")
endif()

find_path(LibClang_INCLUDE_DIRS NAMES clang-c/Index.h HINTS ${PROJECT_SOURCE_DIR}/contrib)

set(LibClang_LIBRARIES ${LibClang_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibClang DEFAULT_MSG LibClang_LIBRARY LibClang_INCLUDE_DIRS)

mark_as_advanced(LibClang_INCLUDE_DIR LibClang_LIBRARY)
