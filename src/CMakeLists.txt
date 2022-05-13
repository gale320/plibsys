include (${PROJECT_SOURCE_DIR}/cmake/PlatformDetect.cmake)
include (${PROJECT_SOURCE_DIR}/cmake/VisibilityDetect.cmake)
include (${PROJECT_SOURCE_DIR}/cmake/StdargDetect.cmake)
include (${PROJECT_SOURCE_DIR}/cmake/ThreadNameDetect.cmake)

plibsys_detect_target_platform (PLIBSYS_TARGET_PLATFORM)
plibsys_detect_c_compiler (PLIBSYS_C_COMPILER)
plibsys_detect_target_os (PLIBSYS_TARGET_OS)
plibsys_detect_os_bits (PLIBSYS_OS_BITS)

add_subdirectory(lib)