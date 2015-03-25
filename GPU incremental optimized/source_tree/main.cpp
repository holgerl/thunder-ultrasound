#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <CL/cl.h>
#include "gui.h"
#include "file_io.h"
#include "reconstruction.h"
#include "utils.h"
#include "holger_time.h"
#include "global_const.h"
#include <omp.h>
#include <NVIDIA/vector_math.h>

cl_context context;
cl_kernel fill_volume;
cl_kernel round_off_translate;
cl_kernel transform;
cl_kernel fill_holes;
cl_kernel build_ray_dirs;
cl_kernel cast_rays;
cl_kernel adv_fill_voxels;
cl_kernel trace_intersections;
cl_command_queue reconstruction_cmd_queue;
cl_command_queue ray_casting_cmd_queue;
cl_device_id device;
cl_program program;

extern cl_mem dev_volume;

int volume_w = 512;
int volume_h = 256;
int volume_n = 512;
float volume_spacing = 0.08f;
unsigned char * volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
float4 volume_origo = {92.0, -20.0f, 281.0f}; // ultrasoundSample5
//float volume_origo_pnn[3] = {850, 10, 2800}; // ultrasoundSample5
float volume_origo_pnn[3] = {1100, 0, 3510}; // ultrasoundSample5

omp_lock_t lock;

void ocl_release() {
	clReleaseMemObject(dev_volume);

	clReleaseKernel(fill_volume);
	clReleaseKernel(round_off_translate);
	clReleaseKernel(transform);
	clReleaseKernel(fill_holes);
	clReleaseKernel(build_ray_dirs);
	clReleaseKernel(cast_rays);
	clReleaseKernel(adv_fill_voxels);
	clReleaseKernel(trace_intersections);

	clReleaseProgram(program);
	clReleaseCommandQueue(reconstruction_cmd_queue);
	clReleaseCommandQueue(ray_casting_cmd_queue);
	clReleaseContext(context);
	//clUnloadCompiler();
}

void ocl_print_info() {
	cl_uint platforms_n;
	cl_uint devices_n;
	size_t temp_size;
	cl_platform_id * platforms = (cl_platform_id *) malloc(sizeof(cl_platform_id)*256);
	cl_device_id * devices = (cl_device_id *) malloc(sizeof(cl_device_id)*256);
	char * str = (char *) malloc(sizeof(char)*2048);

	clGetPlatformIDs(256, platforms, &platforms_n);
	for (int i = 0; i < platforms_n; i++) {
		printf("platform %d of %d:\n", i+1, platforms_n);
		clGetPlatformInfo(platforms[i], CL_PLATFORM_VERSION, 2048, str, &temp_size);
		printf("\t CL_PLATFORM_VERSION: %s\n", str);
		clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, 2048, str, &temp_size);
		printf("\t CL_PLATFORM_NAME: %s\n", str);
		clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, 2048, str, &temp_size);
		printf("\t CL_PLATFORM_VENDOR: %s\n", str);

		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 256, devices, &devices_n);
		for (int j = 0; j < devices_n; j++) {
			printf("\t device %d of %d:\n", j+1, devices_n);
			cl_device_type type;
			clGetDeviceInfo(devices[j], CL_DEVICE_TYPE, sizeof(type), &type, &temp_size);
			if (type == CL_DEVICE_TYPE_CPU)
				printf("\t\t CL_DEVICE_TYPE: CL_DEVICE_TYPE_CPU\n");
			else if (type == CL_DEVICE_TYPE_GPU)
				printf("\t\t CL_DEVICE_TYPE: CL_DEVICE_TYPE_GPU\n");
			else if (type == CL_DEVICE_TYPE_ACCELERATOR)
				printf("\t\t CL_DEVICE_TYPE: CL_DEVICE_TYPE_ACCELERATOR\n");
			else if (type == CL_DEVICE_TYPE_DEFAULT)
				printf("\t\t CL_DEVICE_TYPE: CL_DEVICE_TYPE_DEFAULT\n");
			else 
				printf("\t\t CL_DEVICE_TYPE: (combination)\n");

			cl_uint temp_uint;
			cl_ulong temp_ulong;
			size_t temp_size_t;
			size_t * size_t_array = (size_t *) malloc(sizeof(size_t)*3);
			printf("\t\t device id: %d\n", devices[j]);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(temp_uint), &temp_uint, &temp_size);
			printf("\t\t CL_DEVICE_MAX_COMPUTE_UNITS: %d\n", temp_uint);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(temp_uint), &temp_uint, &temp_size);
			printf("\t\t CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS: %d\n", temp_uint);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(size_t)*3, size_t_array, &temp_size);
			printf("\t\t CL_DEVICE_MAX_WORK_ITEM_SIZES: %d %d %d\n", size_t_array[0], size_t_array[1], size_t_array[2]);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(temp_size_t), &temp_size_t, &temp_size);
			printf("\t\t CL_DEVICE_MAX_WORK_GROUP_SIZE: %d\n", temp_size_t);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(temp_uint), &temp_uint, &temp_size);
			printf("\t\t CL_DEVICE_MAX_CLOCK_FREQUENCY: %d\n", temp_uint);
			clGetDeviceInfo(devices[j], CL_DEVICE_ADDRESS_BITS, sizeof(temp_uint), &temp_uint, &temp_size);
			printf("\t\t CL_DEVICE_ADDRESS_BITS: %d\n", temp_uint);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(temp_ulong), &temp_ulong, &temp_size);
			printf("\t\t CL_DEVICE_MAX_MEM_ALLOC_SIZE: %d\n", temp_ulong);
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_PARAMETER_SIZE, sizeof(temp_size_t), &temp_size_t, &temp_size);
			printf("\t\t CL_DEVICE_MAX_PARAMETER_SIZE: %d\n", temp_size_t);
			clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(temp_ulong), &temp_ulong, &temp_size);
			printf("\t\t CL_DEVICE_GLOBAL_MEM_SIZE: %u\n", temp_ulong);
			clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 2048, str, &temp_size);
			printf("\t\t CL_DEVICE_NAME: %s\n", str);
			clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR, 2048, str, &temp_size);
			printf("\t\t CL_DEVICE_VENDOR: %s\n", str);
			clGetDeviceInfo(devices[j], CL_DEVICE_EXTENSIONS, 2048, str, &temp_size);
			printf("\t\t CL_DEVICE_EXTENSIONS: %s\n", str);
		}
	}
	printf("\n");
}

void ocl_init() {
	cl_int err;
	
	/*cl_platform_id platform = NULL;
	cl_uint numPlatforms = 1;
	clGetPlatformIDs(numPlatforms, &platform, NULL);
	cl_context_properties cps[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};
	
	cl_device_id devices[2];
	clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 2, devices, NULL);
	device = devices[1];
	//clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
	printf("device id: %d\n", device);
	
	context = clCreateContextFromType(cps, CL_DEVICE_TYPE_GPU, NULL, NULL, &err);
	*/

	// Extra new way:
	cl_platform_id platform = NULL;
	clGetPlatformIDs(1, &platform, NULL);
	cl_context_properties cps[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};
	cl_device_id devices[256];
	clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 256, devices, NULL);
	device = devices[0];
	context = clCreateContext(cps, 1, &device, NULL, NULL, &err);

	printf("device id: %d\n", device);

	reconstruction_cmd_queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
	if (err != CL_SUCCESS) {
		printf("ERROR clCreateCommandQueue: %d\n", err);
		exit(err);
	}
	ray_casting_cmd_queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
	if (err != CL_SUCCESS) {
		printf("ERROR clCreateCommandQueue: %d\n", err);
		exit(err);
	}

	size_t temp;
	char * program_src = file2string("kernels.ocl", "", &temp);
	if (program_src == NULL) program_src = file2string("..\\source_tree\\kernels.ocl", "", &temp);
	if (program_src == NULL) program_src = file2string("..\\..\\source_tree\\kernels.ocl", "", &temp);
	program = clCreateProgramWithSource(context, 1, (const char **)&program_src, 0, &err);
	err = clBuildProgram(program, 0, 0, 0, 0, 0);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[512*512];
		memset(buffer, 0, 512*512);
		printf("ERROR: Failed to build program on device %d. Error code: %d\n", device, err);

		clGetProgramBuildInfo(
			program,								// the program object being queried
			device,									// the device for which the OpenCL code was built
			CL_PROGRAM_BUILD_LOG,		// specifies that we want the build log
			sizeof(char)*512*512,		// the size of the buffer
			buffer,									// on return, holds the build log
			&len);									// on return, the actual size in bytes of the data returned

		printf("%s\n", buffer);
		exit(1);
	}
	fill_volume					=	ocl_kernel_build(program, device, "fill_volume");
	round_off_translate = ocl_kernel_build(program, device, "round_off_translate");
	transform						= ocl_kernel_build(program, device, "transform");
	fill_holes					= ocl_kernel_build(program, device, "fill_holes");
	build_ray_dirs			= ocl_kernel_build(program, device, "build_ray_dirs");
	cast_rays						= ocl_kernel_build(program, device, "cast_rays");
	adv_fill_voxels			= ocl_kernel_build(program, device, "adv_fill_voxels");
	trace_intersections	= ocl_kernel_build(program, device, "trace_intersections");

	ocl_print_info();
}

int main(int argc, char ** argv) {
	holger_time_start(0, "Main");	
	ocl_init();
	holger_time(0, "OpenCL initialization");
	read_input(
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.mhd",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.pos",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.tim",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.msk",
		//"C:\\Master Holger\\Simple test input\\Lines\\lines.vol",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.vol",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\calibration_files\\M12L.cal"
		);
	/*read_input(
		"C:\\Master Holger\\Simple test input\\Small size\\small.mhd", 
		"C:\\Master Holger\\Simple test input\\Small size\\small.pos", 
		"C:\\Master Holger\\Simple test input\\Small size\\small.tim", 
		"C:\\Master Holger\\Simple test input\\Small size\\small.msk", 
		//"C:\\Master Holger\\Simple test input\\Small size\\small.vol", 
		"C:\\Master Holger\\Simple test input\\Lines\\lines.vol",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\calibration_files\\M12L.cal"
		);*/
	holger_time(0, "Read input files");
	create_volume();
	omp_init_lock(&lock);
	#pragma omp parallel num_threads(2)
	{
		int thread_idx = omp_get_thread_num();
		if (thread_idx == 0) {
			printf("Reconstruct thread\n");
			//reconstruct_simple();
			reconstruct_adv();
			holger_time(0, "Reconstruction");
			write_output("output.vol");
			holger_time(0, "Write to disk");
		} else if (thread_idx == 1) {
			printf("GUI thread\n");
			gui(argc, argv);
		}
	}
	ocl_release();
	holger_time(0, "OpenCL release");
	host_free();
	holger_time(0, "Host free");
	holger_time_print(0);
	exit(0);
}