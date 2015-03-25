PROJECT(Thunder)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

INCLUDE_DIRECTORIES("..\\source_tree\\inc")

IF (APPLE)
  #FindOpenGL()
  FIND_PACKAGE(OpenGL)
  FIND_PACKAGE(GLUT)
  FIND_LIBRARY(OPENCL_LIBRARY OpenCL DOC "OpenCL lib for OSX")
  FIND_PATH(OPENCL_INCLUDE_DIR OpenCL/cl.h DOC "Include for OpenCL on OSX")
ELSE (APPLE)
  LINK_LIBRARIES("..\\source_tree\\lib\\OpenCL\\x64\\OpenCL.lib")
  LINK_LIBRARIES("..\\source_tree\\lib\\glut\\glut64.lib")
ENDIF (APPLE)

ADD_EXECUTABLE(Thunder main.cpp kernels.ocl file_io.cpp file_io.h gui.cpp gui.h reconstruction.cpp reconstruction.h utils.cpp utils.h global_const.h holger_time.h holger_time.cpp ray_casting.h ray_casting.cpp input_sets.h)
SET_SOURCE_FILES_PROPERTIES(kernels.ocl PROPERTIES HEADER_FILE_ONLY TRUE)

IF (APPLE)
TARGET_LINK_LIBRARIES(Thunder
  ${OPENGL_LIBRARIES}
  ${OPENCL_LIBRARY}
  ${GLUT_LIBRARIES}
)
ENDIF (APPLE)