# cmake/CompilerWarnings.cmake
add_library(project_warnings INTERFACE)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wno-unused-parameter
  )
elseif(MSVC)
  target_compile_options(project_warnings INTERFACE
    /W4
    /WX
  )
endif()
