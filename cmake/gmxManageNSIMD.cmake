
# Manage the NSIMD setup. 

include(gmxDetectCpu)
include(gmxDetectSimd)

# Set variables path 
set(GMX_NSIMD_INCLUDE_PATH "${GMX_NSIMD_ROOT}/include")
set(GMX_NSIMD_LIBRARY_PATH "${GMX_NSIMD_ROOT}/lib")

# Detection of architecture
# Section to set compiler flags for NSIMD.

# FIND LIBRARY AND SET COMPILE FLAGS/ LINKER FLAGS
# Find SIMD

if ("${GMX_NSIMD_FOR}" STREQUAL "SSE2")
  set(GMX_SUGGESTED_SIMD "SSE2")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "SSE4.1")
  set(GMX_SUGGESTED_SIMD "SSE4.1")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "AVX")
  set(GMX_SUGGESTED_SIMD "AVX_256")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "AVX2")
  set(GMX_SUGGESTED_SIMD "AVX2_256")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "AVX_512")
  set(GMX_SUGGESTED_SIMD "AVX_512")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "AVX_512_KNL")
  set(GMX_SUGGESTED_SIMD "AVX_512_KNL")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "AARCH64")
  set(GMX_SUGGESTED_SIMD "ARM_NEON_ASIMD")
elseif ("${GMX_NSIMD_FOR}" STREQUAL "ARM_NEON")
  set(GMX_SUGGESTED_SIMD "ARM_NEON")
else()
  gmx_suggest_simd(GMX_SUGGESTED_SIMD)
endif()

message("-- NSIMD for ${GMX_SUGGESTED_SIMD}")

if ((${GMX_SUGGESTED_SIMD} STREQUAL "AVX2_256") OR (${GMX_SUGGESTED_SIMD} STREQUAL "AVX2_128"))
  set(NSIMD_COMPILE_FLAGS "-DAVX2 -mavx2 -mfma")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_x86_64")

elseif(${GMX_SUGGESTED_SIMD} STREQUAL "SSE2")
  set(NSIMD_COMPILE_FLAGS "-DSSE2 -msse2")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_x86_64")

elseif(${GMX_SUGGESTED_SIMD} STREQUAL "SSE4.1")
  set(NSIMD_COMPILE_FLAGS "-DSSE42 -msse4.2")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_x86_64")

elseif ((${GMX_SUGGESTED_SIMD} STREQUAL "AVX_128_FMA") OR (${GMX_SUGGESTED_SIMD} STREQUAL "AVX_256"))
  set(NSIMD_COMPILE_FLAGS "-DAVX -mavx")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_x86_64")

elseif(${GMX_SUGGESTED_SIMD} STREQUAL "AVX_512_KNL")
  set(NSIMD_COMPILE_FLAGS "-DAVX512_KNL -mavx512_knl -mfma")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_x86_64")

elseif(${GMX_SUGGESTED_SIMD} STREQUAL "AVX_512")
  set(NSIMD_COMPILE_FLAGS "-DAVX512_SKYLAKE -mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl -mfma")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_x86_64")

elseif(${GMX_SUGGESTED_SIMD} STREQUAL "ARM_NEON_ASIMD")
  set(NSIMD_COMPILE_FLAGS "-DAARCH64")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_aarch64")

elseif(${GMX_SUGGESTED_SIMD} STREQUAL "ARM_NEON")
  set(NSIMD_COMPILE_FLAGS "-DNEON128 -mfpu=neon")
  find_library(NSIMD_LIBRARY "libnsimd_armv7")
  set(NSIMD_LINKER_FLAGS "-L${GMX_NSIMD_LIBRARY_PATH} -lnsimd_armv7")

else()
  message(FATAL_ERROR "Unsupported SIMD instruction sets by NSIMD")
endif()

set(NSIMD_COMPILE_FLAGS "${NSIMD_COMPILE_FLAGS} -I${GMX_NSIMD_INCLUDE_PATH}")

# SET LINLER FLAGS
set(CMAKE_EXE_LINKER_FLAGS "${NSIMD_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${NSIMD_LINKER_FLAGS} ${CMAKE_SHARED_LINKER_FLAGS}")
set(GMX_PUBLIC_LIBRARIES "${NSIMD_COMPILE_FLAGS} ${NSIMD_LINKER_FLAGS}${GMX_PUBLIC_LIBRARIES}")

if (NSIMD_LIBRARY)
  list(APPEND GMX_EXTRA_LIBRARIES ${NSIMD_LIBRARY})
  list(APPEND GMX_COMMON_LIBRARIES ${NSIMD_LIBRARY})
  set(GMX_SIMD_NSIMD 1)
endif()

