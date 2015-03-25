#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <CL/cl.h>
#include "utils.h"
#include <omp.h>

extern omp_lock_t lock;

extern cl_context context;
extern cl_kernel build_ray_dirs;
extern cl_kernel cast_rays;
extern cl_command_queue ray_casting_cmd_queue;
extern cl_device_id device;

extern cl_mem dev_volume;
extern int volume_w;
extern int volume_h;
extern int volume_n;
extern unsigned char * bitmap;
extern int bitmap_w;
extern int bitmap_h;
extern int bitmap_size;

cl_mem dev_printings;
cl_mem dev_ray_dirs;
cl_mem dev_bitmap;

int printings_size;
int ray_dirs_size;

size_t * global_work_size;
size_t * local_work_size;

void init_ray_casting() {
	printings_size = sizeof(float)*bitmap_w*bitmap_h*3;
	dev_printings = ocl_create_buffer(context, CL_MEM_READ_WRITE, printings_size, NULL);
	
	dev_bitmap = ocl_create_buffer(context, CL_MEM_READ_WRITE, bitmap_size, bitmap);

	ray_dirs_size = sizeof(cl_float4)*bitmap_w*bitmap_h;
	dev_ray_dirs = ocl_create_buffer(context, CL_MEM_READ_WRITE, ray_dirs_size, NULL);

	ocl_check_error(clFinish(ray_casting_cmd_queue));
}

void ray_cast(cl_float4 camera_pos, cl_float4 camera_lookat) {
	printf("Ray casting\n");

	clSetKernelArg(build_ray_dirs, 0, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(build_ray_dirs, 1, sizeof(cl_int), &volume_w);
	clSetKernelArg(build_ray_dirs, 2, sizeof(cl_int), &volume_h);
	clSetKernelArg(build_ray_dirs, 3, sizeof(cl_int), &volume_n);
	clSetKernelArg(build_ray_dirs, 4, sizeof(cl_mem), &dev_ray_dirs);
	clSetKernelArg(build_ray_dirs, 5, sizeof(cl_int), &bitmap_w);
	clSetKernelArg(build_ray_dirs, 6, sizeof(cl_int), &bitmap_h);
	clSetKernelArg(build_ray_dirs, 7, sizeof(float)*4, camera_pos);
	clSetKernelArg(build_ray_dirs, 8, sizeof(float)*4, camera_lookat);
	clSetKernelArg(build_ray_dirs, 9, sizeof(cl_mem), &dev_printings);
	global_work_size = (size_t *) malloc(sizeof(size_t)*1);
	// AMD max 256
	global_work_size[0] = (int)(bitmap_w*bitmap_h/256+1)*256;
	local_work_size = (size_t *) malloc(sizeof(size_t)*1);
	local_work_size[0] = 256;
	omp_set_lock(&lock);
	ocl_check_error(clEnqueueNDRangeKernel(ray_casting_cmd_queue, build_ray_dirs, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
	omp_unset_lock(&lock);

	static bool have_printed = true;
	if (!have_printed) {
		float * printings = (float *) malloc(printings_size);
		//ocl_check_error(clEnqueueReadBuffer(ray_casting_cmd_queue, dev_printings, CL_TRUE, 0, printings_size, printings, 0, 0, 0));
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
	}

	clSetKernelArg(cast_rays, 0, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(cast_rays, 1, sizeof(cl_int), &volume_w);
	clSetKernelArg(cast_rays, 2, sizeof(cl_int), &volume_h);
	clSetKernelArg(cast_rays, 3, sizeof(cl_int), &volume_n);
	clSetKernelArg(cast_rays, 4, sizeof(cl_mem), &dev_ray_dirs);
	clSetKernelArg(cast_rays, 5, sizeof(cl_mem), &dev_bitmap);
	clSetKernelArg(cast_rays, 6, sizeof(cl_int), &bitmap_w);
	clSetKernelArg(cast_rays, 7, sizeof(cl_int), &bitmap_h);
	clSetKernelArg(cast_rays, 8, sizeof(float)*4, camera_pos);
	clSetKernelArg(cast_rays, 9, sizeof(float)*4, camera_lookat);
	clSetKernelArg(cast_rays, 10, sizeof(cl_mem), &dev_printings);
	omp_set_lock(&lock);
	ocl_check_error(clEnqueueNDRangeKernel(ray_casting_cmd_queue, cast_rays, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
	omp_unset_lock(&lock);

	clFinish(ray_casting_cmd_queue);
	clFinish(ray_casting_cmd_queue);

	omp_set_lock(&lock);
	ocl_check_error(clEnqueueReadBuffer(ray_casting_cmd_queue, dev_bitmap, CL_TRUE, 0, bitmap_size, bitmap, 0, 0, 0));
	omp_unset_lock(&lock);

	clFinish(ray_casting_cmd_queue);
	clFinish(ray_casting_cmd_queue);

	if (bitmap[0] != 0) printf("bitmap[0]:%d\n", bitmap[0]);

	if (!have_printed) {
		float * printings = (float *) malloc(printings_size);
		//ocl_check_error(clEnqueueReadBuffer(ray_casting_cmd_queue, dev_printings, CL_TRUE, 0, printings_size, printings, 0, 0, 0));
		for (int i = 0; i < bitmap_h; i+=5) {
			for (int j = 0; j < bitmap_w; j+=5) {
				printf("%5.0f ", printings[i*bitmap_w + j]);
			}
			printf("\n");
		}
		free(printings);
	}

	have_printed = true;
}

void release_ray_casting() {
	clReleaseMemObject(dev_bitmap);
	clReleaseMemObject(dev_ray_dirs);
	clReleaseMemObject(dev_printings);
}