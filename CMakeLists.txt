project(HEVCpiclimiter)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
   message(FATAL_ERROR "In-source builds are not permitted. Make a separate folder for building:\nmkdir build; cd build; cmake ..\nBefore that, remove the files already created:\nrm -rf CMakeCache.txt CMakeFiles.")
endif()


SET( _sources
  src/main.c
  src/piclimiter.h
)

# executable
add_executable(piclimiter_app ${_sources})

