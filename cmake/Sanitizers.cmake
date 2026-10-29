# cmake/Sanitizers.cmake
add_library(project_sanitizers INTERFACE)

option(NORTH_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(NORTH_SANITIZE)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(project_sanitizers INTERFACE
      $<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined>
      $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer>
      $<$<COMPILE_LANGUAGE:C>:-g>
      $<$<COMPILE_LANGUAGE:C>:-O1>
      $<$<COMPILE_LANGUAGE:C>:-fsanitize-address-use-after-scope>
    )
    target_link_options(project_sanitizers INTERFACE
      $<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined>
    )
  endif()
endif()
