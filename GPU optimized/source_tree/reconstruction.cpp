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
#include "global_const.h"
#include "holger_time.h"

//#define PRINT
#define PRINT_RES0 5
#define PRINT_RES1 29

#define pixel_pos_c(n,i,c) (pixel_pos[n*3*mask_size + i*3 + c])
#define pos_matrices_a(n,x,y) (pos_matrices[n*12 + y*4 + x])

extern unsigned char * bscans;
extern unsigned char * mask;
extern int bscan_w, bscan_h, bscan_n, pos_n;
extern float * pos_timetags, * pos_matrices, * bscan_timetags, * cal_matrix;
extern float bscan_spacing_x, bscan_spacing_y;

unsigned char * volume;

extern cl_context context;
extern cl_kernel round_off_translate;
extern cl_kernel fill_volume;
extern cl_kernel transform;
extern cl_kernel fill_pixel_ill_pos;
extern cl_kernel fill_holes;
extern cl_kernel vnn;
extern cl_command_queue cmd_queue;
extern cl_device_id device;

cl_mem dev_pixel_pos0;
cl_mem dev_pixel_pos1;
cl_mem dev_pixel_pos2;
cl_mem dev_pixel_pos3;
cl_mem dev_pixel_pos4;
cl_mem dev_pixel_pos5;
cl_mem dev_pixel_ill;
cl_mem dev_volume;
cl_mem dev_pos_matrices;
cl_mem dev_bscans0;
cl_mem dev_bscans1;
cl_mem dev_mask;

void interpolate_input() {
	// First normalize timetags:
	float first = pos_timetags[0];
	for (int i = 0; i < pos_n; i++)
		pos_timetags[i] -= first;
	first = bscan_timetags[0];
	for (int i = 0; i < bscan_n; i++)
		bscan_timetags[i] -= first;
	for (int i = 0; i < pos_n; i++)
		pos_timetags[i] *= bscan_timetags[bscan_n-1]/pos_timetags[pos_n-1];

	// Set first and last interpolated value to uninterpolated values:
	float * pos_matrices_interpolated = (float *) malloc(sizeof(float)*12*bscan_n);
	float * pos_timetags_interpolated = (float *) malloc(sizeof(float)*bscan_n);
	pos_timetags_interpolated[0] = pos_timetags[0];
	pos_timetags_interpolated[bscan_n-1] = pos_timetags[pos_n-1];
	for (int matrix_i = 0; matrix_i < 12; matrix_i++) {
		pos_matrices_interpolated[matrix_i] = pos_matrices[matrix_i];
		pos_matrices_interpolated[(bscan_n-1)*12 + matrix_i] = pos_matrices[(pos_n-1)*12 + matrix_i];
	}

	// Do the linear interpolation:
	for (int i = 1; i < bscan_n-1; i++) {
		int pos_i = max(i*pos_n/(float)bscan_n, pos_n-1);
		for (int matrix_i = 0; matrix_i < 12; matrix_i++)
			pos_matrices_interpolated[i*12 + matrix_i] = 
			pos_matrices[pos_i*12 + matrix_i] + 
			(bscan_timetags[i]-pos_timetags[pos_i]) *
			(pos_matrices[(pos_i+1)*12 + matrix_i] - pos_matrices[pos_i*12 + matrix_i]) /
			(pos_timetags[pos_i+1] - pos_timetags[pos_i]);
		pos_timetags_interpolated[i] = bscan_timetags[i];
	}
	pos_n = bscan_n;
	memcpy(pos_matrices, pos_matrices_interpolated, bscan_n);
	memcpy(pos_timetags, pos_timetags_interpolated, bscan_n);
	free(pos_matrices_interpolated);
	free(pos_timetags_interpolated);
}

void calibrate() {
	float * new_matrix = (float *) malloc(sizeof(float)*12);
	for (int a = 0; a < pos_n; a++) {	
		for (int b = 0; b < 3; b++) {
			for (int c = 0; c < 4; c++) {
				float sum = 0;
				for (int k = 0; k < 3; k++) 
					sum += cal_matrix[b*4 + k]*pos_matrices[a*12 + k*4 + c];	
				new_matrix[b*4 + c] = sum;
			}
		}
		memcpy(&pos_matrices[a*12], new_matrix, 12*sizeof(float));
	}
	free(new_matrix);
}

void reconstruct_pnn() {
	holger_time_start(1, "Reconstruction (PNN)");

	int volume_w = VOL_W;
	int volume_h = VOL_H;
	int volume_n = VOL_N;
	float volume_spacing = VOXEL_SPACING;
	float volume_origo[3] = {ORIGO_X, ORIGO_Y, ORIGO_Z};
	volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
	memset(volume, 0, sizeof(unsigned char)*volume_w*volume_h*volume_n);

	// Count mask pixels
	int mask_size = 0;
	for (int y = 0; y < bscan_h; y++)
		for (int x = 0; x < bscan_w; x++)
			if (mask[x + y*bscan_w] > 0)
				mask_size++;

	printf("mask_size: %d\n", mask_size);
	printf("bscan_w/h/n: %d %d %d\n", bscan_w, bscan_h, bscan_n);
	printf("pos_n: %d\n\n", pos_n);
	printf("volume_w/h/n: %d %d %d\n", volume_w, volume_h, volume_n);
	printf("volume_spacing: %f\n", volume_spacing);
	printf("volume_origo: %f %f %f\n", volume_origo[0], volume_origo[1], volume_origo[2]);
	printf("\n");

	// Multiply cal_matrix into pos_matrices
	calibrate();

	// Interpolate pos_timetags and pos_matrices between bscan_timetags so that pos_n == bscan_n
	// (Assumes constant frequency of pos_timetags and bscan_timetags and bscan_n < pos_n.)
	interpolate_input();

	// Fill pixel_ill, pixel_pos and compressed_mask
	int bscans_size = bscan_n * bscan_h * bscan_w * sizeof(cl_uchar);
	int compressed_mask_byte_size = sizeof(unsigned char)*bscan_h * bscan_w / 8; // Assume mask size divisible by 8
	unsigned char * compressed_mask = (unsigned char *) malloc(compressed_mask_byte_size);
	for (int i = 0; i < compressed_mask_byte_size; i++) {
		unsigned char byte = 0;
		for (int j = 0; j < 8; j++)
			if (mask[i*8 + j] != 0)
				byte = byte | (1 << j);
		compressed_mask[i] = byte;
	}

	int pixel_ill_size = bscan_n * mask_size * sizeof(cl_uchar);

	ocl_check_error(clFinish(cmd_queue), "clFinish 0");
	holger_time(1, "Initialization 1");

	dev_bscans0 = ocl_create_buffer(context, CL_MEM_READ_ONLY, bscans_size/2, bscans);
	dev_bscans1 = ocl_create_buffer(context, CL_MEM_READ_ONLY, bscans_size/2, bscans + bscans_size/2);

	///* // with byte adressable memory:
	dev_mask = ocl_create_buffer(context, CL_MEM_READ_ONLY, compressed_mask_byte_size, compressed_mask);
	dev_pixel_ill = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_ill_size, NULL);
	//*/

	/* // AMD does not have byte adressable memory:
	unsigned int * int_compressed_mask = (unsigned int *) malloc(compressed_mask_byte_size*4);
	for (int i = 0; i < bscan_h*bscan_w/8; i++)
		int_compressed_mask[i] = compressed_mask[i];
	dev_mask = ocl_create_buffer(context, CL_MEM_READ_ONLY, compressed_mask_byte_size*4, int_compressed_mask);
	dev_pixel_ill = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_ill_size*4, NULL);
	*/

	ocl_check_error(clFinish(cmd_queue), "clFinish 1");
	holger_time(1, "Transfer bcans and compressed_mask");

	ocl_check_error(clFinish(cmd_queue));
	int pixel_pos_size = bscan_n * mask_size * 3 * sizeof(cl_float);
	dev_pixel_pos0 = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size/6, NULL);
	dev_pixel_pos1 = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size/6, NULL);
	dev_pixel_pos2 = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size/6, NULL);
	dev_pixel_pos3 = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size/6, NULL);
	dev_pixel_pos4 = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size/6, NULL);
	dev_pixel_pos5 = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size/6, NULL);
	/*
	dev_pixel_pos1 = dev_pixel_pos0;
	dev_pixel_pos2 = dev_pixel_pos0;
	dev_pixel_pos3 = dev_pixel_pos0;
	dev_pixel_pos4 = dev_pixel_pos0;
	dev_pixel_pos5 = dev_pixel_pos0;
	*/
	cl_mem pixel_pos[6] = {dev_pixel_pos0, dev_pixel_pos1, dev_pixel_pos2, dev_pixel_pos3, dev_pixel_pos4, dev_pixel_pos5};

	free(compressed_mask);

	ocl_check_error(clFinish(cmd_queue), "clFinish 2");
	holger_time(1, "Initializations 2");

	clSetKernelArg(fill_pixel_ill_pos, 0, sizeof(cl_mem), &dev_bscans0);
	clSetKernelArg(fill_pixel_ill_pos, 1, sizeof(cl_mem), &dev_bscans1);
	clSetKernelArg(fill_pixel_ill_pos, 2, sizeof(cl_mem), &dev_mask);
	clSetKernelArg(fill_pixel_ill_pos, 3, sizeof(cl_mem), &dev_pixel_pos0);
	clSetKernelArg(fill_pixel_ill_pos, 4, sizeof(cl_mem), &dev_pixel_pos1);
	clSetKernelArg(fill_pixel_ill_pos, 5, sizeof(cl_mem), &dev_pixel_pos2);
	clSetKernelArg(fill_pixel_ill_pos, 6, sizeof(cl_mem), &dev_pixel_pos3);
	clSetKernelArg(fill_pixel_ill_pos, 7, sizeof(cl_mem), &dev_pixel_pos4);
	clSetKernelArg(fill_pixel_ill_pos, 8, sizeof(cl_mem), &dev_pixel_pos5);
	clSetKernelArg(fill_pixel_ill_pos, 9, sizeof(cl_mem), &dev_pixel_ill);
	clSetKernelArg(fill_pixel_ill_pos, 10, sizeof(cl_int), &mask_size);
	clSetKernelArg(fill_pixel_ill_pos, 11, sizeof(cl_int), &bscan_n);
	clSetKernelArg(fill_pixel_ill_pos, 12, sizeof(cl_int), &bscan_h);
	clSetKernelArg(fill_pixel_ill_pos, 13, sizeof(cl_int), &bscan_w);
	clSetKernelArg(fill_pixel_ill_pos, 14, sizeof(cl_float), &bscan_spacing_x);
	clSetKernelArg(fill_pixel_ill_pos, 15, sizeof(cl_float), &bscan_spacing_y);
	size_t * fill_global_work_size = (size_t *) malloc(sizeof(size_t)*1);
	// Best on FX5800 and C2050 and HD5870: work group of 4
	fill_global_work_size[0] = (bscan_n/4+1)*4;
	size_t * fill_local_work_size = (size_t *) malloc(sizeof(size_t)*1);
	fill_local_work_size[0] = 4;
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, fill_pixel_ill_pos, 1, NULL, fill_global_work_size, fill_local_work_size, NULL, NULL, NULL));

	ocl_check_error(clFinish(cmd_queue), "clFinish 3");
	holger_time(1, "Kernel: fill_pixel_ill_pos");
	printf("Kernel: fill_pixel_ill_pos\n");

	/*float * pixel_pos0 = (float *) malloc(pixel_pos_size/6);
	float * pixel_pos1 = (float *) malloc(pixel_pos_size/6);
	float * pixel_pos2 = (float *) malloc(pixel_pos_size/6);
	float * pixel_pos3 = (float *) malloc(pixel_pos_size/6);
	float * pixel_pos4 = (float *) malloc(pixel_pos_size/6);
	float * pixel_pos5 = (float *) malloc(pixel_pos_size/6);
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos0, CL_TRUE, 0, pixel_pos_size/6, pixel_pos0, 0, 0, 0));	
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos1, CL_TRUE, 0, pixel_pos_size/6, pixel_pos1, 0, 0, 0));	
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos2, CL_TRUE, 0, pixel_pos_size/6, pixel_pos2, 0, 0, 0));	
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos3, CL_TRUE, 0, pixel_pos_size/6, pixel_pos3, 0, 0, 0));	
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos4, CL_TRUE, 0, pixel_pos_size/6, pixel_pos4, 0, 0, 0));	
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos5, CL_TRUE, 0, pixel_pos_size/6, pixel_pos5, 0, 0, 0));	
	for (int n = 0; n < bscan_n/6; n++) {
		printf("n:%d ", n);
		for (int i = 0; i < 3; i++)
			printf("%f ", pixel_pos0[n*mask_size*3 + 42*3 + i]);
		printf("\n");
	}
	for (int n = 0; n < bscan_n/6; n++) {
		printf("n:%d ", n);
		for (int i = 0; i < 3; i++)
			printf("%f ", pixel_pos1[n*mask_size*3 + 42*3 + i]);
		printf("\n");
	}
	for (int n = 0; n < bscan_n/6; n++) {
		printf("n:%d ", n);
		for (int i = 0; i < 3; i++)
			printf("%f ", pixel_pos2[n*mask_size*3 + 42*3 + i]);
		printf("\n");
	}
	for (int n = 0; n < bscan_n/6; n++) {
		printf("n:%d ", n);
		for (int i = 0; i < 3; i++)
			printf("%f ", pixel_pos3[n*mask_size*3 + 42*3 + i]);
		printf("\n");
	}
	for (int n = 0; n < bscan_n/6; n++) {
		printf("n:%d ", n);
		for (int i = 0; i < 3; i++)
			printf("%f ", pixel_pos4[n*mask_size*3 + 42*3 + i]);
		printf("\n");
	}
	for (int n = 0; n < bscan_n/6; n++) {
		printf("n:%d ", n);
		for (int i = 0; i < 3; i++)
			printf("%f ", pixel_pos5[n*mask_size*3 + 42*3 + i]);
		printf("\n");
	}*/

	// Transform pixel_pos
	int pos_matrices_size = sizeof(cl_float)*12*pos_n;
	dev_pos_matrices = ocl_create_buffer(context, CL_MEM_READ_ONLY, pos_matrices_size, pos_matrices);

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Transder pos_matrices");

	clSetKernelArg(transform, 0, sizeof(cl_mem), &dev_pixel_pos0);
	clSetKernelArg(transform, 1, sizeof(cl_mem), &dev_pixel_pos1);
	clSetKernelArg(transform, 2, sizeof(cl_mem), &dev_pixel_pos2);
	clSetKernelArg(transform, 3, sizeof(cl_mem), &dev_pixel_pos3);
	clSetKernelArg(transform, 4, sizeof(cl_mem), &dev_pixel_pos4);
	clSetKernelArg(transform, 5, sizeof(cl_mem), &dev_pixel_pos5);
	clSetKernelArg(transform, 6, sizeof(cl_mem), &dev_pos_matrices);
	clSetKernelArg(transform, 7, sizeof(cl_int), &mask_size);
	clSetKernelArg(transform, 8, sizeof(cl_int), &bscan_n);
	size_t * global_work_size = (size_t *) malloc(sizeof(size_t)*1);
	// AMD supports only 256
	global_work_size[0] = (int)(bscan_n*mask_size/256+1)*256;
	size_t * local_work_size = (size_t *) malloc(sizeof(size_t)*1);
	local_work_size[0] = 256;
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, transform, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Kernel: transform");
	printf("Kernel: transform\n");

	// Round off pixel_pos to nearest voxel coordinates and translate to origo
	clSetKernelArg(round_off_translate, 0, sizeof(cl_mem), &dev_pixel_pos0);
	clSetKernelArg(round_off_translate, 1, sizeof(cl_mem), &dev_pixel_pos1);
	clSetKernelArg(round_off_translate, 2, sizeof(cl_mem), &dev_pixel_pos2);
	clSetKernelArg(round_off_translate, 3, sizeof(cl_mem), &dev_pixel_pos3);
	clSetKernelArg(round_off_translate, 4, sizeof(cl_mem), &dev_pixel_pos4);
	clSetKernelArg(round_off_translate, 5, sizeof(cl_mem), &dev_pixel_pos5);
	clSetKernelArg(round_off_translate, 6, sizeof(cl_float), &volume_spacing);
	clSetKernelArg(round_off_translate, 7, sizeof(cl_int), &mask_size);
	clSetKernelArg(round_off_translate, 8, sizeof(cl_float), &volume_origo[0]);
	clSetKernelArg(round_off_translate, 9, sizeof(cl_float), &volume_origo[1]);
	clSetKernelArg(round_off_translate, 10, sizeof(cl_float), &volume_origo[2]);
	clSetKernelArg(round_off_translate, 11, sizeof(cl_int), &bscan_n);
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, round_off_translate, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));

	/*
	float * foo = (float *) malloc(pixel_pos_size/6);
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_pixel_pos5, CL_TRUE, 0, pixel_pos_size/6, foo, 0, 0, 0));

	float minx = 10000000; // infinity
	float miny = 10000000; // infinity
	float minz = 10000000; // infinity
	for (int i = 0; i < bscan_n*mask_size/6; i++) {
		if (foo[i*3 + 0] < minx && foo[i*3 + 0] > -10000) minx = foo[i*3 + 0];
		if (foo[i*3 + 1] < miny && foo[i*3 + 1] > -10000) miny = foo[i*3 + 1];
		if (foo[i*3 + 2] < minz && foo[i*3 + 2] > -10000) minz = foo[i*3 + 2];
	}
	printf("minxyz: %f %f %f\n\n", minx, miny, minz);
	free(foo);
	*/

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Kernel: round_off_translate");
	printf("Kernel: round_off_translate\n");

	// Fill volume from pixel_ill

	int volume_size = volume_n * volume_h * volume_w * sizeof(cl_uchar);
	
	// with byte adressable memory:
	dev_volume = ocl_create_buffer(context, CL_MEM_READ_WRITE, volume_size, volume);

	/* // AMD does not have byte adressable memory:
	unsigned int * int_volume = (unsigned int *) malloc(volume_size*4);
	memset(int_volume, 0, volume_size*4);
	dev_volume = ocl_create_buffer(context, CL_MEM_WRITE_ONLY, volume_size*4, int_volume);
	*/

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Allocate volume");

	clSetKernelArg(fill_volume, 0, sizeof(cl_mem), &dev_pixel_pos0);
	clSetKernelArg(fill_volume, 1, sizeof(cl_mem), &dev_pixel_pos1);
	clSetKernelArg(fill_volume, 2, sizeof(cl_mem), &dev_pixel_pos2);
	clSetKernelArg(fill_volume, 3, sizeof(cl_mem), &dev_pixel_pos3);
	clSetKernelArg(fill_volume, 4, sizeof(cl_mem), &dev_pixel_pos4);
	clSetKernelArg(fill_volume, 5, sizeof(cl_mem), &dev_pixel_pos5);
	clSetKernelArg(fill_volume, 6, sizeof(cl_mem), &dev_pixel_ill);
	clSetKernelArg(fill_volume, 7, sizeof(cl_int), &mask_size);
	clSetKernelArg(fill_volume, 8, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(fill_volume, 9, sizeof(cl_int), &volume_n);
	clSetKernelArg(fill_volume, 10, sizeof(cl_int), &volume_h);
	clSetKernelArg(fill_volume, 11, sizeof(cl_int), &volume_w);
	clSetKernelArg(fill_volume, 12, sizeof(cl_int), &bscan_n);
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, fill_volume, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Kernel: fill_volume");
	printf("Kernel: fill_volume\n");

	// Fill volume holes
	size_t * volume_global_work_size = (size_t *) malloc(sizeof(size_t)*2);
	// AMD supports only 256
	volume_global_work_size[0] = (volume_n*volume_h*volume_w/256+1)*256;
	size_t * volume_local_work_size = (size_t *) malloc(sizeof(size_t)*2);
	volume_local_work_size[0] = 256;
	clSetKernelArg(fill_holes, 0, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(fill_holes, 1, sizeof(cl_int), &volume_n);
	clSetKernelArg(fill_holes, 2, sizeof(cl_int), &volume_h);
	clSetKernelArg(fill_holes, 3, sizeof(cl_int), &volume_w);
	ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, fill_holes, 1, NULL, volume_global_work_size, volume_local_work_size, NULL, NULL, NULL));

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Kernel: fill_holes");
	printf("Kernel: fill_holes\n");

	// with byte adressable memory:
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_volume, CL_TRUE, 0, volume_size, volume, 0, 0, 0));

	/* // AMD does not have byte adressable memory:
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_volume, CL_TRUE, 0, volume_size*4, int_volume, 0, 0, 0));
	for (int i = 0; i < volume_h*volume_w*volume_n; i++)
		volume[i] = int_volume[i];
	free(int_volume);
	*/

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Transfer volume");

	clReleaseMemObject(dev_pixel_pos0);
	clReleaseMemObject(dev_pixel_pos1);
	clReleaseMemObject(dev_pixel_pos2);
	clReleaseMemObject(dev_pixel_pos3);
	clReleaseMemObject(dev_pixel_pos4);
	clReleaseMemObject(dev_pixel_pos5);
	clReleaseMemObject(dev_pixel_ill);
	clReleaseMemObject(dev_pos_matrices);
	clReleaseMemObject(dev_bscans0);
	clReleaseMemObject(dev_bscans1);
	clReleaseMemObject(dev_mask);

	ocl_check_error(clFinish(cmd_queue));
	holger_time(1, "Release device memory");

	holger_time_print(1);
}

void dummy_reconstruct() {
	int volume_w = VOL_W;
	int volume_h = VOL_H;
	int volume_n = VOL_N;

	volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
	for (int z = 0; z < volume_n; z++)
		for (int y = 0; y < volume_h; y++)
			for (int x = 0; x < volume_w; x++)
					volume[x + y*volume_w + z*volume_w*volume_h] = ((x/25+y/25+z/25)%2)*191 + (int)(64*(rand()/(float)RAND_MAX));
}

void reconstruct_vnn() {
	holger_time_start(1, "Reconstruct procedure (VNN)");

	float3 * plane_points = (float3 *) malloc(sizeof(float3)*bscan_n*3);
	plane_eq * bscan_plane_equations = (plane_eq *) malloc(sizeof(plane_eq)*bscan_n);

	int volume_w = VOL_W;
	int volume_h = VOL_H;
	int volume_n = VOL_N;
	float volume_spacing = VOXEL_SPACING;
	float3 volume_origo = {ORIGO_X+5, ORIGO_Y-20, ORIGO_Z}; // WHY?
	volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
	memset(volume, 0, sizeof(unsigned char)*volume_w*volume_h*volume_n);

	#ifdef PRINT
		printf("volume_whn: %d %d %d (%5.2f %5.2f %5.2f)\n", volume_w, volume_h, volume_n, volume_w*volume_spacing,volume_h*volume_spacing, volume_n*volume_spacing);
	#endif

	#define plane_points_c(n,i) (plane_points[(n)*3 + (i)])
	#define pos_matrices_int_a(n,x,y) (pos_matrices_interpolated[(n)*12 + (y)*4 + (x)])

	// Multiply cal_matrix into pos_matrices
	calibrate();

	// Interpolate pos_timetags and pos_matrices between bscan_timetags so that pos_n == bscan_n
	// Assumes constant frequency of pos_timetags and bscan_timetags and bscan_n < pos_n

	interpolate_input();

	holger_time(1, "Initialization code");

	// Fill plane_points
	for (int n = 0; n < bscan_n; n++) {
		float3 corner0 = {0.0f,0.0f,0.0f};
		plane_points_c(n, 0) = corner0;

		float3 cornerx = {0.0f, bscan_w*bscan_spacing_x, 0.0f};
		plane_points_c(n, 1) = cornerx;

		float3 cornery = {0.0f, 0.0f, bscan_h*bscan_spacing_y};
		plane_points_c(n, 2) = cornery;
	}

	#ifdef PRINT
	printf("plane_points:\n");
	for (int n = 0; n < bscan_n; n+=bscan_n) {	
		for (int i = 0; i < 3; i++) {	
			printf("%5.2f ", plane_points_c(n, i).x);
			printf("%5.2f ", plane_points_c(n, i).y);
			printf("%5.2f ", plane_points_c(n, i).z);
			printf(" \t ");
		}
		printf("\n");
	}
	#endif

	holger_time(1, "Fill plane_points");

	// Transform plane_points
	float * sums = (float *) malloc(sizeof(float)*3);
	for (int n = 0; n < bscan_n; n++) {	
		for (int i = 0; i < 3; i++) {	
			for (int y = 0; y < 3; y++) {
				float sum = 0;
				sum += pos_matrices_a(n,0,y)*plane_points_c(n,i).x;
				sum += pos_matrices_a(n,1,y)*plane_points_c(n,i).y;
				sum += pos_matrices_a(n,2,y)*plane_points_c(n,i).z;
				sum += pos_matrices_a(n,3,y);
				sums[y] = sum;
			}
			memcpy(&plane_points_c(n,i), sums, 3*sizeof(float));
		}
	}

	holger_time(1, "Transform plane_points");

	// Translate plane_points to origo
	for (int n = 0; n < bscan_n; n++)
		for (int i = 0; i < 3; i++)
			plane_points_c(n,i) = sub(plane_points_c(n,i), volume_origo);

	holger_time(1, "Translate plane_points to origo");

	#ifdef PRINT
	printf("Transformed translated plane_points:\n");
	for (int n = 0; n < bscan_n; n++) {	
		for (int i = 0; i < 3; i++) {	
			printf("%5.2f ", plane_points_c(n, i).x);
			printf("%5.2f ", plane_points_c(n, i).y);
			printf("%5.2f ", plane_points_c(n, i).z);
			printf(" \t ");
			printf(" \t ");
		}
		printf("\n");
	}
	#endif

	// Fill bscan_plane_equations
	for (int n = 0; n < bscan_n; n++) {
		float3 a = plane_points_c(n,0);
		float3 b = plane_points_c(n,1);
		float3 c = plane_points_c(n,2);
		float3 normal = normalize(cross(sub(a,b), sub(c,a)));

		bscan_plane_equations[n].a = normal.x;
		bscan_plane_equations[n].b = normal.y;
		bscan_plane_equations[n].c = normal.z;
		bscan_plane_equations[n].d = -normal.x*a.x - normal.y*a.y - normal.y*a.z;
	}

	#ifdef PRINT
	printf("bscan_plane_equations:\n");
	for (int n = 0; n < bscan_n; n++)
		printf("bscan_plane_equations[n]: %f %f %f %f\n", bscan_plane_equations[n*4+0], bscan_plane_equations[n*4+1], bscan_plane_equations[n*4+2], bscan_plane_equations[n*4+3]);
	#endif

	holger_time(1, "Fill bscan_plane_equations");

	// Fill volume from pixel_ill
	//int last_current_scan = -1;
	//int last_x = -1;
	//int last_y = 0;
	//int last_z = 0;
	//int counter = 0;
	
	int volume_size = volume_n * volume_h * volume_w * sizeof(cl_uchar);
	int bscans_size = bscan_n * bscan_h * bscan_w * sizeof(cl_uchar);
	int mask_byte_size = bscan_h * bscan_w * sizeof(cl_uchar);
	int plane_eq_size = sizeof(float) * 4 * bscan_n;
	int plane_points_size = sizeof(float) * 4 * bscan_n * 3;
	int printings_size = sizeof(float)*volume_h*10;

	float * h_dev_plane_eq = (float *) malloc(plane_eq_size);
	float * h_dev_plane_points = (float *) malloc(plane_points_size);
	memcpy(h_dev_plane_eq, bscan_plane_equations, plane_eq_size);
	for (int n = 0; n < bscan_n; n++)
		for (int m = 0; m < 3; m++)
			memcpy(&h_dev_plane_points[n*3*4+m*4], &plane_points[n*3+m], sizeof(float)*3);
	float * printings = (float *) malloc(printings_size);
	memset(printings, 0, printings_size);

	dev_bscans0 = ocl_create_buffer(context, CL_MEM_READ_ONLY, bscans_size/2, bscans);
	dev_bscans1 = ocl_create_buffer(context, CL_MEM_READ_ONLY, bscans_size/2, bscans + bscans_size/2);
	
	///* // with byte adressable memory:
	dev_volume = ocl_create_buffer(context, CL_MEM_WRITE_ONLY, volume_size, volume);
	dev_mask = ocl_create_buffer(context, CL_MEM_READ_ONLY, mask_byte_size, mask);
	//*/

	/* // AMD does not have byte adressable memory:
	unsigned int * int_volume = (unsigned int *) malloc(volume_size*4);
	memset(int_volume, 0, volume_size*4);
	unsigned int * int_mask = (unsigned int *) malloc(bscan_w*bscan_h*4);
	for (int i = 0; i < bscan_w*bscan_h; i++)
		int_mask[i] = mask[i];
	dev_volume = ocl_create_buffer(context, CL_MEM_WRITE_ONLY, volume_size*4, int_volume);
	dev_mask = ocl_create_buffer(context, CL_MEM_READ_ONLY, mask_byte_size*4, int_mask);
	*/
	
	cl_mem dev_plane_eq = ocl_create_buffer(context, CL_MEM_READ_ONLY, plane_eq_size, h_dev_plane_eq);
	cl_mem dev_plane_points = ocl_create_buffer(context, CL_MEM_READ_ONLY, plane_points_size, h_dev_plane_points);
	//cl_mem dev_printings = ocl_create_buffer(context, CL_MEM_WRITE_ONLY, printings_size, printings);
	ocl_check_error(clFinish(cmd_queue), "create buffer finish");

	//for (int section = 0; section < 1; section++) {
		clSetKernelArg(vnn, 0, sizeof(cl_mem), &dev_bscans0);
		clSetKernelArg(vnn, 1, sizeof(cl_mem), &dev_bscans1);
		clSetKernelArg(vnn, 2, sizeof(cl_mem), &dev_mask);
		clSetKernelArg(vnn, 3, sizeof(cl_int), &bscan_w);
		clSetKernelArg(vnn, 4, sizeof(cl_int), &bscan_h);
		clSetKernelArg(vnn, 5, sizeof(cl_int), &bscan_n);
		clSetKernelArg(vnn, 6, sizeof(cl_float), &bscan_spacing_x);
		clSetKernelArg(vnn, 7, sizeof(cl_float), &bscan_spacing_y);
		clSetKernelArg(vnn, 8, sizeof(cl_mem), &dev_volume);
		clSetKernelArg(vnn, 9, sizeof(cl_int), &volume_n);
		clSetKernelArg(vnn, 10, sizeof(cl_int), &volume_h);
		clSetKernelArg(vnn, 11, sizeof(cl_int), &volume_w);
		clSetKernelArg(vnn, 12, sizeof(cl_float), &volume_spacing);
		clSetKernelArg(vnn, 13, sizeof(cl_mem), &dev_plane_eq);
		clSetKernelArg(vnn, 14, sizeof(cl_mem), &dev_plane_points);
		//clSetKernelArg(vnn, 15, sizeof(cl_mem), &dev_printings);
		//clSetKernelArg(vnn, 16, sizeof(cl_int), &section);
		size_t * global_work_size = (size_t *) malloc(sizeof(size_t)*1);
		// For C2050 and FX5800: work group size best at 256
		global_work_size[0] = ((volume_w*volume_n)/256+1)*256;
		size_t * local_work_size = (size_t *) malloc(sizeof(size_t)*1);
		local_work_size[0] = 256;
		ocl_check_error(clEnqueueNDRangeKernel(cmd_queue, vnn, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL), "clEnqueueNDRangeKernel");
	//}

	ocl_check_error(clFinish(cmd_queue), "fill volume finish");
	holger_time(1, "Fill volume");

	// with byte adressable memory:
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_volume, CL_TRUE, 0, volume_size, volume, 0, 0, 0), "clEnqueueReadBuffer");

	/* // AMD does not have byte adressable memory:
	ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_volume, CL_TRUE, 0, volume_size*4, int_volume, 0, 0, 0));
	for (int i = 0; i < volume_h*volume_w*volume_n; i++)
		volume[i] = int_volume[i];
	free(int_volume);
	free(int_mask);
	*/

	/*ocl_check_error(clEnqueueReadBuffer(cmd_queue, dev_printings, CL_TRUE, 0, printings_size, printings, 0, 0, 0));
	for (int i = 0; i < printings_size/sizeof(float); i++) {
		printf("%f \n", printings[i]);
	}*/

	clReleaseMemObject(dev_bscans0);
	clReleaseMemObject(dev_bscans1);
	//clReleaseMemObject(dev_printings);
	clReleaseMemObject(dev_mask);
	clReleaseMemObject(dev_plane_eq);
	clReleaseMemObject(dev_plane_points);

	ocl_check_error(clFinish(cmd_queue), "final finish");
	holger_time(1, "Transfer volume");

	holger_time_print(1);
}