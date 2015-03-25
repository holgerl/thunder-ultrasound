#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "utils.h"
#include "global_const.h"
#include "holger_time.h"
#include "ultrasound_simulator.h"
#include <omp.h>

extern omp_lock_t lock;

extern unsigned char * mask;
extern int bscan_w, bscan_h;
extern float bscan_spacing_x, bscan_spacing_y;
extern float * cal_matrix;

//unsigned char * compressed_mask;
unsigned char * hole_ill;

extern unsigned char * volume;
extern int volume_w;
extern int volume_h;
extern int volume_n;
extern float volume_spacing;
extern float volume_origo_pnn[4]; // ultrasoundSample5

extern cl_context context;
extern cl_kernel round_off_translate;
extern cl_kernel fill_volume;
extern cl_kernel transform;
extern cl_kernel fill_pixel_ill_pos;
extern cl_kernel fill_holes;
extern cl_command_queue reconstruction_cmd_queue;
extern cl_device_id device;

cl_mem dev_pixel_pos;
cl_mem dev_pixel_ill;
cl_mem dev_hole_ill;
cl_mem dev_pos_matrix;
cl_mem dev_volume;
cl_mem dev_bscan;
cl_mem dev_mask;

void host_free() {
	free(volume);
	//free(compressed_mask);
	free(hole_ill);
}

void create_volume() {
	int volume_size = volume_n * volume_h * volume_w * sizeof(unsigned char);

	volume = (unsigned char *) malloc(volume_size);
	memset(volume, 0, sizeof(unsigned char)*volume_w*volume_h*volume_n);

	dev_volume = ocl_create_buffer(context, CL_MEM_READ_WRITE, volume_size, volume);
}

void reconstruct_simple() {
	holger_time_start(1, "Reconstruction");
	
	//#define PRINT
	#define PRINT_RES0 3
	#define PRINT_RES1 30

	// Count mask pixels
	int mask_size = 0;
	for (int y = 0; y < bscan_h; y++)
		for (int x = 0; x < bscan_w; x++)
			if (mask[x + y*bscan_w] > 0)
				mask_size++;

	printf("mask_size: %d\n", mask_size);
	printf("bscan_w/h: %d %d\n", bscan_w, bscan_h);
	printf("volume_w/h/n: %d %d %d\n", volume_w, volume_h, volume_n);
	printf("volume_spacing: %f\n", volume_spacing);
	printf("volume_origo_pnn: %f %f %f\n", volume_origo_pnn[0], volume_origo_pnn[1], volume_origo_pnn[2]);
	printf("\n");

	int bscan_size = bscan_w * bscan_h * sizeof(cl_uchar);
	dev_bscan = ocl_create_buffer(context, CL_MEM_READ_ONLY, bscan_size, NULL);

	int pos_matrix_size = sizeof(cl_float)*12;
	dev_pos_matrix = ocl_create_buffer(context, CL_MEM_READ_ONLY, pos_matrix_size, NULL);

	int pixel_pos_size = mask_size * 3 * sizeof(cl_float);
	int pixel_ill_size = mask_size * sizeof(cl_uchar);
	dev_pixel_pos = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_pos_size, NULL);
	dev_pixel_ill = ocl_create_buffer(context, CL_MEM_READ_WRITE, pixel_ill_size, NULL);

	dev_hole_ill = ocl_create_buffer(context, CL_MEM_READ_WRITE, 5*pixel_ill_size, NULL);

	hole_ill = (unsigned char *) malloc(5*pixel_ill_size);

	clFinish(reconstruction_cmd_queue);
	holger_time(1, "Initialization");

	while(true) {
		printf("Reconstructing\n");
		
		// Retrieve ultrasound data
		unsigned char * bscan = NULL;
		float * pos_matrix = NULL;
		float pos_timetag, bscan_timetag;
		while (pos_matrix == NULL || bscan == NULL) {
			if (poll_bscan()) get_last_bscan(&bscan_timetag, &bscan);
			if (poll_pos_matrix()) get_last_pos_matrix(&pos_timetag, &pos_matrix);
		}
		if (end_of_data()) break;
		
		// TODO: Interpolate the pos matrix to the timetag of the bscan

		// Multiply cal_matrix into pos_matrix
		float * new_matrix = (float *) malloc(sizeof(float)*12);
		for (int b = 0; b < 3; b++) {
			for (int c = 0; c < 4; c++) {
				float sum = 0;
				for (int k = 0; k < 3; k++) 
					sum += cal_matrix[b*4 + k]*pos_matrix[k*4 + c];	
				new_matrix[b*4 + c] = sum;
			}
		}
		memcpy(pos_matrix, new_matrix, 12*sizeof(float));
		free(new_matrix);

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Initialization");

		// Fill pixel_ill and pixel_pos
		unsigned char * pixel_ill = (unsigned char *) malloc(sizeof(unsigned char)*mask_size);
		float * pixel_pos = (float *) malloc(sizeof(float)*mask_size*3);
		int mask_counter = 0;
		for (int y = 0; y < bscan_h; y++)
			for (int x = 0; x < bscan_w; x++)
				if (mask[x + y*bscan_w] > 0) {
					pixel_ill[mask_counter] = bscan[x + y*bscan_w];
					pixel_pos[mask_counter*3+0]	= 0;
					pixel_pos[mask_counter*3+1] = x*bscan_spacing_x;
					pixel_pos[mask_counter*3+2] = y*bscan_spacing_y;
					mask_counter++;
				}

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Fill pixel_ill and pixel_pos");

		omp_set_lock(&lock);
		ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_pos_matrix, CL_TRUE, 0, pos_matrix_size, pos_matrix, 0, 0, 0));
		omp_unset_lock(&lock);

		// Seems to be not needeed anymore:
		omp_set_lock(&lock);
		ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_bscan, CL_TRUE, 0, bscan_size, bscan, 0, 0, 0));
		omp_unset_lock(&lock);

		omp_set_lock(&lock);
		ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_pixel_pos, CL_TRUE, 0, pixel_pos_size, pixel_pos, 0, 0, 0));
		omp_unset_lock(&lock);

		omp_set_lock(&lock);
		ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_pixel_ill, CL_TRUE, 0, pixel_ill_size, pixel_ill, 0, 0, 0));
		omp_unset_lock(&lock);

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Transfers");

		// Transform pixel_pos
		clSetKernelArg(transform, 0, sizeof(cl_mem), &dev_pixel_pos);
		clSetKernelArg(transform, 1, sizeof(cl_mem), &dev_pos_matrix);
		clSetKernelArg(transform, 2, sizeof(cl_int), &mask_size);
		size_t * global_work_size = (size_t *) malloc(sizeof(size_t)*1);
		// AMD can handle max 256
		global_work_size[0] = (int)(mask_size/256+1)*256;
		size_t * local_work_size = (size_t *) malloc(sizeof(size_t)*1);
		local_work_size[0] = 256;
		omp_set_lock(&lock);
		ocl_check_error(clEnqueueNDRangeKernel(reconstruction_cmd_queue, transform, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
		omp_unset_lock(&lock);		

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Transform pixel_pos");

		// Round off pixel_pos to nearest voxel coordinates and translate to origo
		clSetKernelArg(round_off_translate, 0, sizeof(cl_mem), &dev_pixel_pos);
		clSetKernelArg(round_off_translate, 1, sizeof(cl_float), &volume_spacing);
		clSetKernelArg(round_off_translate, 2, sizeof(cl_int), &mask_size);
		clSetKernelArg(round_off_translate, 3, sizeof(cl_float), &volume_origo_pnn[0]);
		clSetKernelArg(round_off_translate, 4, sizeof(cl_float), &volume_origo_pnn[1]);
		clSetKernelArg(round_off_translate, 5, sizeof(cl_float), &volume_origo_pnn[2]);
		omp_set_lock(&lock);
		ocl_check_error(clEnqueueNDRangeKernel(reconstruction_cmd_queue, round_off_translate, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
		omp_unset_lock(&lock);		

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Round off and translate");

		// Fill GPU volume from pixel_ill
		clSetKernelArg(fill_volume, 0, sizeof(cl_mem), &dev_pixel_pos);
		clSetKernelArg(fill_volume, 1, sizeof(cl_mem), &dev_pixel_ill);
		clSetKernelArg(fill_volume, 2, sizeof(cl_int), &mask_size);
		clSetKernelArg(fill_volume, 3, sizeof(cl_mem), &dev_volume);
		clSetKernelArg(fill_volume, 4, sizeof(cl_int), &volume_n);
		clSetKernelArg(fill_volume, 5, sizeof(cl_int), &volume_h);
		clSetKernelArg(fill_volume, 6, sizeof(cl_int), &volume_w);
		omp_set_lock(&lock);
		ocl_check_error(clEnqueueNDRangeKernel(reconstruction_cmd_queue, fill_volume, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
		omp_unset_lock(&lock);		

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Fill GPU volume");

		/*// Fill volume holes
		clSetKernelArg(fill_holes, 0, sizeof(cl_mem), &dev_pixel_pos);
		clSetKernelArg(fill_holes, 1, sizeof(cl_mem), &dev_hole_ill);
		clSetKernelArg(fill_holes, 2, sizeof(cl_int), &mask_size);
		clSetKernelArg(fill_holes, 3, sizeof(cl_mem), &dev_volume);
		clSetKernelArg(fill_holes, 4, sizeof(cl_int), &volume_n);
		clSetKernelArg(fill_holes, 5, sizeof(cl_int), &volume_h);
		clSetKernelArg(fill_holes, 6, sizeof(cl_int), &volume_w);
		omp_set_lock(&lock);
		ocl_check_error(clEnqueueNDRangeKernel(reconstruction_cmd_queue, fill_holes, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
		omp_unset_lock(&lock);
		*/

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Fill GPU volume holes");

		omp_set_lock(&lock);
		ocl_check_error(clEnqueueReadBuffer(reconstruction_cmd_queue, dev_pixel_pos, CL_TRUE, 0, pixel_pos_size, pixel_pos, 0, 0, 0));
		omp_unset_lock(&lock);

		omp_set_lock(&lock);
		ocl_check_error(clEnqueueReadBuffer(reconstruction_cmd_queue, dev_hole_ill, CL_TRUE, 0, 5*pixel_ill_size, hole_ill, 0, 0, 0));
		omp_unset_lock(&lock);

		clFinish(reconstruction_cmd_queue);
		holger_time(1, "Transfers");

		// Fill CPU volume from pixel_ill and hole_ill
		#define inrange(x,a,b) ((x) >= (a) && (x) < (b))
		#define volume_a(x,y,z) (volume[(x) + (y)*volume_w + (z)*volume_w*volume_h])
		#define pixel_pos_c(i,c) (pixel_pos[(i)*3 + (c)])
		for (int i = 0; i < mask_size; i++) {
			int x = pixel_pos_c(i,0);
			int y = pixel_pos_c(i,1);
			int z = pixel_pos_c(i,2);
			if (inrange(x,0,volume_w) && inrange(y,0,volume_h) && inrange(z,0,volume_n))
				volume_a(x,y,z) = pixel_ill[i];
			/*// hole filling:
			for (int h = 0; h < 5; h++) {
				int a = -(1+h); int b = -(1+h); int c = -(1+h); // TODO: Set to direction of probe trajectory
				if (inrange(x+a,0,volume_w) && inrange(y+b,0,volume_h) && inrange(z+c,0,volume_n))
					if (volume_a(x+a,y+b,z+c) == 0 && hole_ill[h*pixel_ill_size + i] != 0)
						volume_a(x+a,y+b,z+c) = hole_ill[h*pixel_ill_size + i];
			}
			*/
		}

		holger_time(1, "Fill CPU volume");
	}

	clReleaseMemObject(dev_pixel_pos);
	clReleaseMemObject(dev_pixel_ill);
	clReleaseMemObject(dev_pos_matrix);
	clReleaseMemObject(dev_bscan);
	clFinish(reconstruction_cmd_queue);

	holger_time(1, "Memory releases");

	holger_time_print(1);
}

void dummy_reconstruct() {
	volume_w = 200;
	volume_h = 250;
	volume_n = 300;

	volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
	for (int z = 0; z < volume_n; z++)
		for (int y = 0; y < volume_h; y++)
			for (int x = 0; x < volume_w; x++)
				volume[x + y*volume_w + z*volume_w*volume_h] = ((x/25+y/25+z/25)%2)*191 + (int)(64*(rand()/(float)RAND_MAX));
}