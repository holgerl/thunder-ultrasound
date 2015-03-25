#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <math.h>
#ifdef __APPLE__
  #include <OpenCL/OpenCL.h>
#else
  #include <CL/cl.h>
#endif //__APPLE__
#if MODE == CONSOLE_MODE
	#include "gui.h"
#endif
#include "file_io.h"
#include "reconstruction.h"
#include "utils.h"
#include "global_const.h"
#include "holger_time.h"

cl_context context;
cl_kernel fill_volume;
cl_kernel round_off_translate;
cl_kernel transform;
cl_kernel fill_pixel_ill_pos;
cl_kernel fill_holes;
cl_kernel build_ray_dirs;
cl_kernel cast_rays;
cl_kernel vnn;
cl_command_queue cmd_queue;
cl_device_id device;
cl_program program;

extern cl_mem dev_volume;
extern unsigned char * volume;

void ocl_release() {
	printf("ocl_release\n");
	clReleaseMemObject(dev_volume);

	clReleaseKernel(fill_volume);
	clReleaseKernel(round_off_translate);
	clReleaseKernel(transform);
	clReleaseKernel(fill_pixel_ill_pos);
	clReleaseKernel(fill_holes);
	clReleaseKernel(vnn);
	if (MODE == PROFILER_MODE || MODE == GUI_MODE) {
		clReleaseKernel(build_ray_dirs);
		clReleaseKernel(cast_rays);
	}

	clReleaseProgram(program);
	clReleaseCommandQueue(cmd_queue);
	clReleaseContext(context);
	clUnloadCompiler();
}

void ocl_init() {
	cl_int err;

	/* // Old way:
	context = clCreateContextFromType(0, CL_DEVICE_TYPE_GPU, 0, 0, &err);
	ocl_check_error(err, "clCreateContextFromType");
	size_t context_descriptor_size;
	clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, 0, &context_descriptor_size);
	cl_device_id * devices = (cl_device_id *) malloc(context_descriptor_size);
	clGetContextInfo(context, CL_CONTEXT_DEVICES, context_descriptor_size, devices, 0);
	device = devices[0];
	*/

	// New way: (not supported anymore)
	//clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
	//context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);

	///* // Extra new way:
	cl_platform_id platform = NULL;
	ocl_check_error(clGetPlatformIDs(1, &platform, NULL));
	cl_context_properties cps[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};
	cl_device_id devices[256];
	ocl_check_error(clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 256, devices, NULL));
	device = devices[0];
	context = clCreateContext(cps, 1, &device, NULL, NULL, &err);
	//*/

	/* // AMD tutorial way:
	cl_platform_id platform = NULL;
	cl_uint numPlatforms = 1;
	clGetPlatformIDs(numPlatforms, &platform, NULL);
	cl_context_properties cps[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};
	cl_device_id devices[256];
	clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 256, devices, NULL);
	device = devices[0];
	context = clCreateContextFromType(cps, CL_DEVICE_TYPE_GPU, NULL, NULL, &err);
	*/

	printf("device id: %d\n", device);

	ocl_check_error(err, "clCreateContext");
	cmd_queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
	ocl_check_error(err, "clCreateCommandQueue");

	char * program_src = file2string(KERNEL_PATHS[0]);
	for (int i = 0; i < MAX_KERNEL_PATHS; i++)
		if (program_src == NULL) program_src = file2string(KERNEL_PATHS[i]);
	if (program_src == NULL) {
		printf("ERROR: did not find kernels.ocl\n");
		exit(-1);
	}

	program = clCreateProgramWithSource(context, 1, (const char **)&program_src, 0, &err);
	ocl_check_error(err, "clCreateProgramWithSource");
	//ocl_check_error(clBuildProgram(program, 0, NULL, 0, 0, 0), "clBuildProgram");
	err = clBuildProgram(program, 0, NULL, 0, 0, 0);
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

		printf("%d %s\n", len, buffer);
		for (int i = 0; i < len; i++)
			printf("%c", buffer[i]);
		printf("\n");
		exit(1);
	}
	//ocl_check_error(clBuildProgram(program, 1, &device, 0, 0, 0), "clBuildProgram"); // Alternatively ...
	fill_volume					=	ocl_kernel_build(program, device, "fill_volume");
	round_off_translate = ocl_kernel_build(program, device, "round_off_translate");
	transform						= ocl_kernel_build(program, device, "transform");
	fill_pixel_ill_pos	=	ocl_kernel_build(program, device, "fill_pixel_ill_pos");
	fill_holes					= ocl_kernel_build(program, device, "fill_holes");
	vnn									= ocl_kernel_build(program, device, "vnn");

	if (MODE == PROFILER_MODE || MODE == GUI_MODE) {
		build_ray_dirs			= ocl_kernel_build(program, device, "build_ray_dirs");
		cast_rays						= ocl_kernel_build(program, device, "cast_rays");
	}
}

void cleanup() {
	holger_time(0, "(GUI)");
	ocl_release();
	holger_time(0, "OpenCL releases");
	free(volume);
	holger_time(0, "Host free");
	holger_time_print(0);
}

extern int bscan_n;

int main(int argc, char ** argv) {
	holger_time_start(0, "Main");

	ocl_print_info();
	holger_time(0, "OpenCL print info");

	ocl_init();
	holger_time(0, "OpenCL initialization");

	read_input(INPUT_FILES);
	holger_time(0, "Read input files");

	if (RECONSTRUCTION_METHOD == RECONSTRUCTION_PNN)
		reconstruct_pnn();
	else if (RECONSTRUCTION_METHOD == RECONSTRUCTION_VNN)
		reconstruct_vnn();
	holger_time(0, "Reconstruction");

	write_output("output.vol");

	holger_time(0, "Writing volume to disk");
	
#if MODE != CONSOLE_MODE
	if (MODE == PROFILER_MODE) {
		profiler_gui();
		cleanup();
	} else if (MODE == GUI_MODE)
		gui(argc, argv, cleanup);
#else
	cleanup();
#endif
}