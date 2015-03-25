PROJECT(Thunder)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

INCLUDE_DIRECTORIES("..\\source_tree\\inc")
LINK_LIBRARIES("..\\source_tree\\lib\\OpenCL\\x64\\OpenCL.lib")
LINK_LIBRARIES("..\\source_tree\\lib\\glut\\glut32.lib")
LINK_LIBRARIES("..\\source_tree\\lib\\glut\\glut64.lib")

FIND_PACKAGE(OpenMP)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /openmp")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /openmp")

ADD_EXECUTABLE(Thunder main.cpp kernels.ocl file_io.cpp file_io.h gui.cpp gui.h reconstruction.cpp reconstruction_adv.cpp reconstruction.h utils.cpp utils.h global_const.h holger_time.h holger_time.cpp ultrasound_simulator.cpp ultrasound_simulator.h ray_casting.h ray_casting.cpp)
SET_SOURCE_FILES_PROPERTIES(kernels.ocl PROPERTIES HEADER_FILE_ONLY TRUE)