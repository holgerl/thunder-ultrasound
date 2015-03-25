#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef __APPLE__
#include <OpenCL/OpenCL.h>
#else
#include <CL/cl.h>
#endif //__APPLE__
#include "utils.h"

extern cl_context context;
extern cl_kernel build_ray_dirs;
extern cl_kernel cast_rays;
extern cl_command_queue cmd_queue;
extern cl_device_id device;

static cl_mem dev_volume;
static int volume_w;
static int volume_h;
static int volume_n;
static unsigned char * bitmap;
static int bitmap_w;
static int bitmap_h;

cl_mem dev_printings;
cl_mem dev_ray_dirs;
cl_mem dev_bitmap;

int printings_size;
int bitmap_size;
int ray_dirs_size;

size_t * global_work_size;
size_t * local_work_size;

void init_ray_casting(cl_mem _dev_volume, int _volume_w, int _volume_h, int _volume_n, unsigned char * _bitmap, int _bitmap_w, int _bitmap_h) {
	dev_volume = _dev_volume;
	volume_w = _volume_w;
	volume_h = _volume_h;
	volume_n = _volume_n;
	bitmap = _bitmap;
	bitmap_w = _bitmap_w;
	bitmap_h = _bitmap_h;

	//printings_size = sizeof(float)*bitmap_w*bitmap_h*3;
	//dev_printings = ocl_create_buffer(context, CL_MEM_READ_WRITE, printings_size, NULL);
	
	bitmap_size = sizeof(cl_uchar)*bitmap_w*bitmap_h;

	// with byte adressable memory:
	dev_bitmap = ocl_create_buffer(context, CL_MEM_WRITE_ONLY, bitmap_size, NULL);

	// AMD does not have byte adressable memory:
	//dev_bitmap = ocl_create_buffer(context, CL_MEM_WRITE_ONLY, bitmap_size*4, NULL);

	ray_dirs_size = sizeof(cl_float4)*bitmap_w*bitmap_h;
	dev_ray_dirs = ocl_create_buffer(context, CL_MEM_READ_WRITE, ray_dirs_size, NULL);

	ocl_check_error(clFinish(cmd_queue));
}

void ray_cast(cl_float4 camera_pos, cl_float4 camera_lookat) {
	//printf("Raycasting\n");
	clSetKernelArg(build_ray_dirs, 0, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(build_ray_dirs, 1, sizeof(cl_int), &volume_w);
	clSetKernelArg(build_ray_dirs, 2, sizeof(cl_int), &volume_h);
	clSetKernelArg(build_ray_dirs, 3, sizeof(cl_int), &volume_n);
	clSetKernelArg(build_ray_dirs, 4, sizeof(cl_mem), &dev_ray_dirs);
	clSetKernelArg(build_ray_dirs, 5, sizeof(cl_int), &bitmap_w);
	clSetKernelArg(build_ray_dirs, 6, sizeof(cl_int), &bitmap_h);
	clSetKernelArg(build_ray_dirs, 7, sizeof(float)*4, &camera_pos);
	clSetKernelArg(build_ray_dirs, 8, sizeof(float)*4, &camera_lookat);
	//clSetKernelArg(build_ray_dirs, 9, sizeof(cl_mem), &dev_printings);
	global_work_size = (size_t *) malloc(sizeof(size_t)*1);
	global_work_size[0] = (int)(bitmap_w*bitmap_h/256+1)*256;
	local_work_size = (size_t *) malloc(sizeof(size_t)*1);
	local_work_size[0] = 256;
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, build_ray_dirs, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));

	/*static bool have_printed = true;
	if (!have_printed) {
		float * printings = (float *) malloc(printings_size);
		ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_printings, CL_TRUE, 0, printings_size, printings, 0, 0, 0));
		for (int i = 0; i < bitmap_h; i+=8) {
			for (int j = 0; j < bitmap_w; j+=8) {
				for (int k = 0; k < 3; k++)
					printf("%4.2f ", printings[i*bitmap_w*3 + j*3 + k]);
				printf("  ");
			}
			printf("\n");
		}
		printf("\n");
		free(printings);
	}*/

	clSetKernelArg(cast_rays, 0, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(cast_rays, 1, sizeof(cl_int), &volume_w);
	clSetKernelArg(cast_rays, 2, sizeof(cl_int), &volume_h);
	clSetKernelArg(cast_rays, 3, sizeof(cl_int), &volume_n);
	clSetKernelArg(cast_rays, 4, sizeof(cl_mem), &dev_ray_dirs);
	clSetKernelArg(cast_rays, 5, sizeof(cl_mem), &dev_bitmap);
	clSetKernelArg(cast_rays, 6, sizeof(cl_int), &bitmap_w);
	clSetKernelArg(cast_rays, 7, sizeof(cl_int), &bitmap_h);
	clSetKernelArg(cast_rays, 8, sizeof(float)*4, &camera_pos);
	clSetKernelArg(cast_rays, 9, sizeof(float)*4, &camera_lookat);
	//clSetKernelArg(cast_rays, 10, sizeof(cl_mem), &dev_printings);
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, cast_rays, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));

	// with byte adressable memory:
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_bitmap, CL_TRUE, 0, bitmap_size, bitmap, 0, 0, 0));

	/* // AMD does not have byte adressable memory:
	unsigned int * int_bitmap = (unsigned int *) malloc(bitmap_size*4);
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_bitmap, CL_TRUE, 0, bitmap_size*4, int_bitmap, 0, 0, 0));
	for (int i = 0; i < bitmap_h*bitmap_w; i++)
		bitmap[i] = int_bitmap[i];
	free(int_bitmap);
	*/

	/*if (!have_printed) {
		float * printings = (float *) malloc(printings_size);
		ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_printings, CL_TRUE, 0, printings_size, printings, 0, 0, 0));
		for (int i = 0; i < bitmap_h; i+=5) {
			for (int j = 0; j < bitmap_w; j+=5) {
				printf("%5.0f ", printings[i*bitmap_w + j]);
			}
			printf("\n");
		}
		free(printings);
	}
	have_printed = true;*/
}

void release_ray_casting() {
	printf("release_ray_casting\n");
	clReleaseMemObject(dev_bitmap);
	clReleaseMemObject(dev_ray_dirs);
	clReleaseMemObject(dev_printings);
}