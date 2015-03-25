#ifndef _GLOBAL_CONST
#define _GLOBAL_CONST
#include "input_sets.h"

#ifdef __APPLE__
	#define INPUT_FILES input_set_mac
	#define ORIGO_X input_set_mac_origo_x
	#define ORIGO_Y input_set_mac_origo_y
	#define ORIGO_Z input_set_mac_origo_z
#else
	#define INPUT_FILES input_set0
	#define ORIGO_X input_set0_origo_x
	#define ORIGO_Y input_set0_origo_y
	#define ORIGO_Z input_set0_origo_z
#endif

#define MAX_MEMORY 4294639616 // TODO: use
#define MAX_MALLOC 1073659904 // TODO: use

///* // Normal size:
#define VOL_W 512
#define VOL_H 256
#define VOL_N 512
#define VOXEL_SPACING 0.08
//*/

/* // Mac size:
#define VOL_W 400
#define VOL_H 200
#define VOL_N 400
#define VOXEL_SPACING 0.115
*/

/* // Small size:
#define VOL_W 200
#define VOL_H 100
#define VOL_N 200
#define VOXEL_SPACING 0.225
*/

#define CONSOLE_MODE 1
#define PROFILER_MODE 2
#define GUI_MODE 3
#define MODE GUI_MODE

#define PROFILER_RAYCASTS 0

#define RECONSTRUCTION_PNN 1
#define RECONSTRUCTION_VNN 2
#define RECONSTRUCTION_METHOD RECONSTRUCTION_VNN

#define MAX_KERNEL_PATHS 10
const char KERNEL_PATHS[MAX_KERNEL_PATHS][256] = {
	"kernels.ocl", 
	"..\\source_tree\\kernels.ocl", 
	"..\\..\\source_tree\\kernels.ocl", 
	"..\\..\\source_tree\\kernels.ocl", 
	"../source_tree/kernels.ocl",
	"./source_tree/kernels.ocl" , 
	"./../source_tree/kernels.ocl", 
	"./../../source_tree/kernels.ocl", 
	"./../../../source_tree/kernels.ocl", 
	"/Users/holger/thunder-ultrasound/GPU optimized/source_tree/kernels.ocl"
};

#endif