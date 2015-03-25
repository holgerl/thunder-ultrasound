#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "utils.h"
#include "holger_time.h"
#include "ultrasound_simulator.h"
#include "global_const.h"
#include <omp.h>
#include <NVIDIA/vector_math.h>

extern omp_lock_t lock;

extern unsigned char * mask;
extern int bscan_w, bscan_h;
extern float * cal_matrix;
extern float bscan_spacing_x, bscan_spacing_y;

#define pos_matrix_a(x,y) (pos_matrix[(y)*4 + (x)])
#define volume_a(x,y,z) (volume[(x) + (y)*volume_w + (z)*volume_w*volume_h])
#define bscans_queue_a(n, x, y) bscans_queue[n][(x) + (y)*bscan_w]

extern unsigned char * volume;
extern int volume_w;
extern int volume_h;
extern int volume_n;
extern float volume_spacing;
extern float4 volume_origo; // ultrasoundSample5

extern cl_context context;
extern cl_kernel adv_fill_voxels;
extern cl_kernel trace_intersections;
extern cl_command_queue reconstruction_cmd_queue;
extern cl_device_id device;

float4 * x_vector_queue = (float4 *) malloc(BSCAN_WINDOW*sizeof(float4));
float4 * y_vector_queue = (float4 *) malloc(BSCAN_WINDOW*sizeof(float4));
plane_pts * plane_points_queue = (plane_pts *) malloc(BSCAN_WINDOW*sizeof(plane_pts));
float4 * bscan_plane_equation_queue = (float4 *) malloc(BSCAN_WINDOW*sizeof(float4));
float4 * intersections = (float4 *) malloc(sizeof(float4)*2*max3(volume_w, volume_h, volume_n)*max3(volume_w, volume_h, volume_n));
unsigned char * * bscans_queue = (unsigned char * *) malloc(BSCAN_WINDOW * bscan_w * bscan_h * sizeof(unsigned char));
unsigned char * bscans_queue_ = (unsigned char *) malloc(BSCAN_WINDOW * bscan_w * bscan_h * sizeof(unsigned char));
float * * pos_matrices_queue = (float * *) malloc(BSCAN_WINDOW * sizeof(float)*12);
float * pos_timetags_queue = (float *) malloc(BSCAN_WINDOW * sizeof(float));
float * bscan_timetags_queue = (float *) malloc(BSCAN_WINDOW * sizeof(float));

cl_mem dev_intersections;
extern cl_mem dev_volume;
cl_mem dev_x_vector_queue;
cl_mem dev_y_vector_queue;
cl_mem dev_plane_points_queue;
extern cl_mem dev_mask;
cl_mem dev_bscans_queue;
cl_mem dev_bscan_timetags_queue;
cl_mem dev_bscan_plane_equation_queue;

int intersections_size;
int volume_size;
int x_vector_queue_size;
int y_vector_queue_size;
int plane_points_queue_size;
int mask_size;
int bscans_queue_size;
int bscan_timetags_queue_size;
int bscan_plane_equation_queue_size;

int dev_x_vector_queue_size;
int dev_y_vector_queue_size;
int dev_plane_points_queue_size;

int max_vol_dim = max3(volume_w, volume_h, volume_n);
size_t global_work_size[1] = {((max_vol_dim*max_vol_dim)/256+1)*256};
size_t local_work_size[1] = {256};

void shift_queues() {
	for (int i = 0; i < BSCAN_WINDOW-1; i++) {
		x_vector_queue[i] = x_vector_queue[i+1];
		y_vector_queue[i] = y_vector_queue[i+1];
		bscans_queue[i] = bscans_queue[i+1];
		pos_matrices_queue[i] = pos_matrices_queue[i+1];
		bscan_timetags_queue[i] = bscan_timetags_queue[i+1];
		pos_timetags_queue[i] = pos_timetags_queue[i+1];
		bscan_plane_equation_queue[i] = bscan_plane_equation_queue[i+1];
		plane_points_queue[i] = plane_points_queue[i+1];
	}
}

void wait_for_input() {
	bscans_queue[BSCAN_WINDOW-1] = NULL;
	pos_matrices_queue[BSCAN_WINDOW-1] = NULL;
	while (bscans_queue[BSCAN_WINDOW-1] == NULL || pos_matrices_queue[BSCAN_WINDOW-1] == NULL) {
		if (poll_bscan()) get_last_bscan(&bscan_timetags_queue[BSCAN_WINDOW-1], &bscans_queue[BSCAN_WINDOW-1]);
		if (poll_pos_matrix()) get_last_pos_matrix(&pos_timetags_queue[BSCAN_WINDOW-1], &pos_matrices_queue[BSCAN_WINDOW-1]);
	}

	// TODO: Interpolate the pos matrix to the timetag of the bscan
}

void calibrate_pos_matrix(float * pos_matrix, float * cal_matrix) {
	// Multiply cal_matrix into pos_matrix
	float * new_matrix = (float *) malloc(sizeof(float)*12);
	for (int b = 0; b < 3; b++)
		for (int c = 0; c < 4; c++) {
			float sum = 0;
			for (int k = 0; k < 3; k++) 
				sum += cal_matrix[b*4 + k]*pos_matrix[k*4 + c];	
			new_matrix[b*4 + c] = sum;
		}
	memcpy(pos_matrix, new_matrix, 12*sizeof(float));
	free(new_matrix);
}

void insert_plane_points(float * pos_matrix) {
	// Fill plane_points
	plane_points_queue[BSCAN_WINDOW-1].corner0 = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
	plane_points_queue[BSCAN_WINDOW-1].cornerx = make_float4(0.0f, bscan_w*bscan_spacing_x, 0.0f, 0.0f);
	plane_points_queue[BSCAN_WINDOW-1].cornery = make_float4(0.0f, 0.0f, bscan_h*bscan_spacing_y, 0.0f);

	// Transform plane_points
	float4 * foo = (float4 *) &plane_points_queue[BSCAN_WINDOW-1];
	float * sums = (float *) malloc(sizeof(float)*3);	
	for (int i = 0; i < 3; i++) {	
		for (int y = 0; y < 3; y++) {
			float sum = 0;
			sum += pos_matrix_a(0,y)*foo[i].x;
			sum += pos_matrix_a(1,y)*foo[i].y;
			sum += pos_matrix_a(2,y)*foo[i].z;
			sum += pos_matrix_a(3,y);
			sums[y] = sum;
		}
		memcpy(&foo[i], sums, 3*sizeof(float));
		foo[i] = foo[i] - volume_origo;
	}
}

void insert_plane_eq() {
	// Fill bscan_plane_equation
	float4 a = plane_points_queue[BSCAN_WINDOW-1].corner0;
	float4 b = plane_points_queue[BSCAN_WINDOW-1].cornerx;
	float4 c = plane_points_queue[BSCAN_WINDOW-1].cornery;
	float4 normal = normalize(cross(a-b, c-a));

	bscan_plane_equation_queue[BSCAN_WINDOW-1].x = normal.x;
	bscan_plane_equation_queue[BSCAN_WINDOW-1].y = normal.y;
	bscan_plane_equation_queue[BSCAN_WINDOW-1].z = normal.z;
	bscan_plane_equation_queue[BSCAN_WINDOW-1].w = -normal.x*a.x - normal.y*a.y - normal.y*a.z;

	x_vector_queue[BSCAN_WINDOW-1] = normalize(plane_points_queue[BSCAN_WINDOW-1].cornerx - plane_points_queue[BSCAN_WINDOW-1].corner0);
	y_vector_queue[BSCAN_WINDOW-1] = normalize(plane_points_queue[BSCAN_WINDOW-1].cornery - plane_points_queue[BSCAN_WINDOW-1].corner0);
}

int find_intersections(int axis) {

	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_bscan_plane_equation_queue, CL_TRUE, 0, bscan_plane_equation_queue_size, bscan_plane_equation_queue, 0, 0, 0));
	omp_unset_lock(&lock);

	clSetKernelArg(trace_intersections, 0, sizeof(cl_mem), &dev_intersections);
	clSetKernelArg(trace_intersections, 1, sizeof(cl_int), &volume_w);
	clSetKernelArg(trace_intersections, 2, sizeof(cl_int), &volume_h);
	clSetKernelArg(trace_intersections, 3, sizeof(cl_int), &volume_n);
	clSetKernelArg(trace_intersections, 4, sizeof(cl_float), &volume_spacing);
	clSetKernelArg(trace_intersections, 5, sizeof(cl_mem), &dev_bscan_plane_equation_queue);
	clSetKernelArg(trace_intersections, 6, sizeof(cl_int), &axis);
	omp_set_lock(&lock);
	ocl_check_error(clEnqueueNDRangeKernel(reconstruction_cmd_queue, trace_intersections, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
	omp_unset_lock(&lock);

	return max_vol_dim*max_vol_dim;
}

void fill_voxels(int intersection_counter) {
	holger_time(2, "(other)");
	
	holger_time(3, "(other)");
	
	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_x_vector_queue, CL_TRUE, 0, dev_x_vector_queue_size, x_vector_queue, 0, 0, 0));
	omp_unset_lock(&lock);

	holger_time(3, "dev_x_vector_queue");

	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_y_vector_queue, CL_TRUE, 0, dev_y_vector_queue_size, y_vector_queue, 0, 0, 0));
	omp_unset_lock(&lock);

	holger_time(3, "dev_y_vector_queue");

	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_plane_points_queue, CL_TRUE, 0, dev_plane_points_queue_size, plane_points_queue, 0, 0, 0));
	omp_unset_lock(&lock);

	holger_time(3, "dev_plane_points_queue");

	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_mask, CL_TRUE, 0, mask_size, mask, 0, 0, 0));
	omp_unset_lock(&lock);

	holger_time(3, "dev_mask");

	unsigned char * temp = (unsigned char *) malloc(bscans_queue_size);
	for (int i = 0; i < BSCAN_WINDOW; i++)
		memcpy(&temp[bscan_w*bscan_h*i], bscans_queue[i], sizeof(unsigned char)*bscan_w*bscan_h);
	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_bscans_queue, CL_TRUE, 0, bscans_queue_size, temp, 0, 0, 0));
	omp_unset_lock(&lock);
	free(temp);

	holger_time(3, "dev_bscans_queue");

	omp_set_lock(&lock);
	ocl_check_error(clEnqueueWriteBuffer(reconstruction_cmd_queue, dev_bscan_timetags_queue, CL_TRUE, 0, bscan_timetags_queue_size, bscan_timetags_queue, 0, 0, 0));
	omp_unset_lock(&lock);

	holger_time(3, "dev_bscan_timetags_queue");

	holger_time(2, "Write to device");

	clSetKernelArg(adv_fill_voxels, 0, sizeof(cl_mem), &dev_intersections);
	clSetKernelArg(adv_fill_voxels, 1, sizeof(cl_mem), &dev_volume);
	clSetKernelArg(adv_fill_voxels, 2, sizeof(cl_float), &volume_spacing);
	clSetKernelArg(adv_fill_voxels, 3, sizeof(cl_int), &volume_w);
	clSetKernelArg(adv_fill_voxels, 4, sizeof(cl_int), &volume_h);
	clSetKernelArg(adv_fill_voxels, 5, sizeof(cl_int), &volume_n);
	clSetKernelArg(adv_fill_voxels, 6, sizeof(cl_mem), &dev_x_vector_queue);
	clSetKernelArg(adv_fill_voxels, 7, sizeof(cl_mem), &dev_y_vector_queue);
	clSetKernelArg(adv_fill_voxels, 8, sizeof(cl_mem), &dev_plane_points_queue);
	clSetKernelArg(adv_fill_voxels, 9, sizeof(cl_mem), &dev_bscan_plane_equation_queue);
	clSetKernelArg(adv_fill_voxels, 10, sizeof(cl_float), &bscan_spacing_x);
	clSetKernelArg(adv_fill_voxels, 11, sizeof(cl_float), &bscan_spacing_y);
	clSetKernelArg(adv_fill_voxels, 12, sizeof(cl_int), &bscan_w);
	clSetKernelArg(adv_fill_voxels, 13, sizeof(cl_int), &bscan_h);
	clSetKernelArg(adv_fill_voxels, 14, sizeof(cl_mem), &dev_mask);
	clSetKernelArg(adv_fill_voxels, 15, sizeof(cl_mem), &dev_bscans_queue);
	clSetKernelArg(adv_fill_voxels, 16, sizeof(cl_mem), &dev_bscan_timetags_queue);
	clSetKernelArg(adv_fill_voxels, 17, sizeof(cl_int), &intersection_counter);
	omp_set_lock(&lock);
	ocl_check_error(clEnqueueNDRangeKernel(reconstruction_cmd_queue, adv_fill_voxels, 1, NULL, global_work_size, local_work_size, NULL, NULL, NULL));
	omp_unset_lock(&lock);

	holger_time(2, "OpenCL kernel");

	static int foo = 0;
	omp_set_lock(&lock);
	//if (++foo % 5 == 0)
	//if (++foo > 400)
	if (true)
		ocl_check_error(clEnqueueReadBuffer(reconstruction_cmd_queue, dev_volume, CL_TRUE, 0, volume_size, volume, 0, 0, 0));
	omp_unset_lock(&lock);

	holger_time(2, "Read from device");
}

void reconstruct_adv() {
	holger_time_start(1, "Reconstruction");

	memset(volume, 0, sizeof(unsigned char)*volume_w*volume_h*volume_n);

	printf("bscan_w/h: %d %d\n", bscan_w, bscan_h);
	printf("volume_w/h/n: %d %d %d\n", volume_w, volume_h, volume_n);
	printf("volume_spacing: %f\n", volume_spacing);
	printf("volume_origo: %f %f %f\n", volume_origo.x, volume_origo.y, volume_origo.z);
	printf("\n");

	holger_time(1, "Initialization");

	// Fill up queue
	for (int i = 0; i < BSCAN_WINDOW; i++) {
		shift_queues();
		wait_for_input();
		if (end_of_data()) break;
	}

	intersections_size = sizeof(cl_float4)*2*max_vol_dim*max_vol_dim;
	volume_size = volume_w * volume_h * volume_n * sizeof(cl_uchar);
	x_vector_queue_size = BSCAN_WINDOW*sizeof(cl_float4);
	y_vector_queue_size = BSCAN_WINDOW*sizeof(cl_float4);
	plane_points_queue_size = BSCAN_WINDOW*sizeof(plane_pts);
	mask_size = bscan_w * bscan_h * sizeof(cl_uchar);
	bscans_queue_size = BSCAN_WINDOW * bscan_w * bscan_h * sizeof(cl_uchar);
	bscan_timetags_queue_size = BSCAN_WINDOW * sizeof(cl_float);
	bscan_plane_equation_queue_size = BSCAN_WINDOW * sizeof(float4);

	dev_x_vector_queue_size = BSCAN_WINDOW*sizeof(float)*4;
	dev_y_vector_queue_size = BSCAN_WINDOW*sizeof(float)*4;
	dev_plane_points_queue_size = BSCAN_WINDOW*sizeof(float)*4*3;

	dev_intersections = ocl_create_buffer(context, CL_MEM_READ_WRITE, intersections_size, NULL);
	dev_volume = ocl_create_buffer(context, CL_MEM_READ_WRITE, volume_size, volume);
	dev_x_vector_queue = ocl_create_buffer(context, CL_MEM_READ_WRITE, dev_x_vector_queue_size, NULL);
	dev_y_vector_queue = ocl_create_buffer(context, CL_MEM_READ_WRITE, dev_y_vector_queue_size, NULL);
	dev_plane_points_queue = ocl_create_buffer(context, CL_MEM_READ_WRITE, dev_plane_points_queue_size, NULL);
	dev_mask = ocl_create_buffer(context, CL_MEM_READ_WRITE, mask_size, NULL);
	dev_bscans_queue = ocl_create_buffer(context, CL_MEM_READ_WRITE, bscans_queue_size, NULL);
	dev_bscan_timetags_queue = ocl_create_buffer(context, CL_MEM_READ_WRITE, bscan_timetags_queue_size, NULL);
	dev_bscan_plane_equation_queue = ocl_create_buffer(context, CL_MEM_READ_WRITE, bscan_plane_equation_queue_size, NULL);

	holger_time_start(2, "Fill voxels");
	holger_time_start(3, "Write to device");

	while(true) {
		// Retrieve ultrasound data and perform ye olde switcheroo
		shift_queues();
		wait_for_input();
		if (end_of_data()) break;

		holger_time(1, "Retrieve ultrasound data");

		calibrate_pos_matrix(pos_matrices_queue[BSCAN_WINDOW-1], cal_matrix);
		holger_time(1, "Calibrate");

		insert_plane_points(pos_matrices_queue[BSCAN_WINDOW-1]);
		holger_time(1, "Fill, transform and translate plane_points");

		insert_plane_eq();
		holger_time(1, "Fill bscan_plane_equation");

		//int axis = find_orthogonal_axis(bscan_plane_equation);	// TODO: Function that finds axis most orthogonal to bscan plane
		int axis = 1;																							// Actually, turns out that the output is pretty much equal for any axis, 
																															// but the computation time varies (ca 1X - 2X)

		int intersection_counter = find_intersections(axis);
		holger_time(1, "Find ray-plane intersections");

		fill_voxels(intersection_counter);
		holger_time(1, "Fill voxels");
	}

	holger_time_print(1);
	holger_time_print(2);
	holger_time_print(3);
}