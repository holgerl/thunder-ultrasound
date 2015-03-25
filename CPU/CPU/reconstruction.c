#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "utils.h"
#include "holger_time.h"

extern unsigned char * bscans;
extern unsigned char * mask;
extern int bscan_w, bscan_h, bscan_n, pos_n;
extern float * pos_timetags, * pos_matrices, * bscan_timetags, * cal_matrix;
extern float bscan_spacing_x, bscan_spacing_y;

unsigned char * volume;
int volume_w;
int volume_h;
int volume_n;
float volume_spacing;

void reconstruct_pnn() {
	holger_time_start(1, "Reconstruct procedure (PNN)");

	float * pixel_pos;
	unsigned char * pixel_ill;
	float * volume_origo;

	volume_w = 512;
	volume_h = 256;
	volume_n = 512;
	volume_spacing = 0.08; // ultrasoundSample5 512*256*512
	//volume_spacing = 0.1;  // ultrasoundSample5 450*200*450
	volume_origo = (float *) malloc(sizeof(float)*3);
	volume_origo[0] = 1100; volume_origo[1] = 0; volume_origo[2] = 3510; // ultrasoundSample5 512*256*512
	//volume_origo[0] = 900; volume_origo[1] = 0; volume_origo[2] = 2800; // ultrasoundSample5 450*200*450
	volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
	memset(volume, 0, sizeof(unsigned char)*volume_w*volume_h*volume_n);

	holger_time(1, "Volume malloc and memset");

	//#define PRINT
	#define PRINT_RES0 3
	#define PRINT_RES1 30

	//#define pixel_pos_a(n,i) (pixel_pos[(n)*3*mask_size + (i)*3])
	#define pixel_pos_c(n,i,c) (pixel_pos[(n)*3*mask_size + (i)*3 + (c)])
	#define pos_matrices_int_a(n,x,y) (pos_matrices_interpolated[(n)*12 + (y)*4 + (x)])

	// Count mask pixels
	int mask_size = 0;
	for (int y = 0; y < bscan_h; y++)
		for (int x = 0; x < bscan_w; x++)
			if (mask[x + y*bscan_w] > 0)
				mask_size++;

	// Allocate pixel_ill and pixel_pos
	pixel_ill = (unsigned char *) malloc(sizeof(unsigned char)*mask_size*bscan_n);
	pixel_pos = (float *) malloc(sizeof(float)*mask_size*3*bscan_n);

	#ifdef PRINT
	printf("mask_size: %d\n", mask_size);
	printf("bscan_w/h/n: %d,%d,%d\n", bscan_w, bscan_h, bscan_n);
	printf("pos_n: %d\n\n", pos_n);
	#endif

	#ifdef PRINT
	printf("\n");
	printf("pos_matrices:\n");
	for (int n = 0; n < pos_n; n+=pos_n/PRINT_RES0) {
	//n = pos_n/2; {
		for (int y = 0; y < 3; y++) {
			for (int x = 0; x < 4; x++)
				printf("%f ", pos_matrices[n*3*4 + y*4 + x]);
			printf("\n");
		}
		printf("\n");
	}
	#endif

	// Multiply cal_matrix into pos_matrices
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

	// Interpolate pos_timetags and pos_matrices between bscan_timetags so that pos_n == bscan_n
	// Assumes constant frequency of pos_timetags and bscan_timetags and bscan_n < pos_n

	#ifdef PRINT
	printf("pos_timetags before normalization: ");
	for (int i = 0; i < pos_n; i++)
		printf("%3.1f ", pos_timetags[i]);
	printf("\n");
	#endif

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
		int pos_i = i*pos_n/(float)bscan_n;
		for (int matrix_i = 0; matrix_i < 12; matrix_i++)
			pos_matrices_interpolated[i*12 + matrix_i] = 
			pos_matrices[i*12 + matrix_i] + 
			(bscan_timetags[i]-pos_timetags[pos_i]) *
			(pos_matrices[(i+1)*12 + matrix_i] - pos_matrices[i*12 + matrix_i]) /
			(pos_timetags[pos_i+1] - pos_timetags[pos_i]);
		pos_timetags_interpolated[i] = bscan_timetags[i];
	}
	pos_n = bscan_n;

	holger_time(1, "Initialization code");

	#ifdef PRINT
	printf("bscan_timetags:\n");
	for (int i = 0; i < bscan_n; i+=bscan_n/PRINT_RES1)
		printf("%3.1f ", bscan_timetags[i]);
	printf("\n\n");
	printf("pos_timetags:\n");
	for (int i = 0; i < pos_n; i+=pos_n/PRINT_RES1)
		printf("%3.1f ", pos_timetags[i]);
	printf("\n\n");
	printf("cal_matrix multiplied into pos_matrices:\n");
	for (int n = 0; n < pos_n; n+=pos_n/PRINT_RES0) {
	//n = pos_n/2; {
		for (int y = 0; y < 3; y++) {
			for (int x = 0; x < 4; x++)
				printf("%f ", pos_matrices[n*3*4 + y*4 + x]);
			printf("\n");
		}
		printf("\n");
	}
	printf("pos_matrices_interpolated:\n");
	for (int n = 0; n < bscan_n; n+=bscan_n/PRINT_RES0) {
	//n = bscan_n/2; {
		for (int y = 0; y < 3; y++) {
			for (int x = 0; x < 4; x++)
				printf("%f ", pos_matrices_interpolated[n*3*4 + y*4 + x]);
			printf("\n");
		}
		printf("\n");
	}
	#endif

	// Fill pixel_ill and pixel_pos
	for (int n = 0; n < bscan_n; n++) {
		int mask_counter = 0;
		for (int y = 0; y < bscan_h; y++)
			for (int x = 0; x < bscan_w; x++)
				if (mask[x + y*bscan_w] > 0) {
					pixel_ill[n*mask_size + mask_counter] = bscans[x + y*bscan_w + n*bscan_w*bscan_h];
					pixel_pos[n*mask_size*3 + mask_counter*3+0]	= 0;
					pixel_pos[n*mask_size*3 + mask_counter*3+1] = x*bscan_spacing_x;
					pixel_pos[n*mask_size*3 + mask_counter*3+2] = y*bscan_spacing_y;
					mask_counter++;
				}
	}

	holger_time(1, "Fill pixel_ill and pixel_pos");

	#ifdef PRINT
	printf("pixel_ill:\n");
	for (int n = 0; n < bscan_n; n+=bscan_n/PRINT_RES0) {
	//int n = bscan_n/2; {
		for (int y = 0; y < mask_size; y+=mask_size/PRINT_RES1)
			printf("%d ", pixel_ill[n*mask_size + y]);
		printf("\n");
	}
	printf("\n");
	printf("pixel_pos:\n");
	for (int n = 0; n < bscan_n; n+=bscan_n/PRINT_RES0) {
	//n = bscan_n/2; {
		for (int y = 0; y < mask_size; y+=mask_size/PRINT_RES1) {
			printf("%3.1f %3.1f %3.1f\t", 
				pixel_pos[n*mask_size*3 + y*3 + 0],
				pixel_pos[n*mask_size*3 + y*3 + 1],
				pixel_pos[n*mask_size*3 + y*3 + 2]);
		}
		printf("\n");
	}
	#endif

	// Transform pixel_pos
	float * sums = (float *) malloc(sizeof(float)*3);
	for (int n = 0; n < bscan_n; n++) {	
		for (int i = 0; i < mask_size; i++) {	
			for (int y = 0; y < 3; y++) {
				float sum = 0;
				for (int x = 0; x < 3; x++) 
					sum += pos_matrices_int_a(n,x,y)*pixel_pos_c(n,i,x);
				sum += pos_matrices_int_a(n,3,y);
				sums[y] = sum;
			}
			memcpy(&pixel_pos_c(n,i,0), sums, 3*sizeof(float));
		}
	}

	holger_time(1, "Transform pixel_pos");

	#ifdef PRINT
	printf("transformed pixel_pos:\n");
	for (int n = 0; n < bscan_n; n+=bscan_n/PRINT_RES0) {
	//n = bscan_n/2; {
		for (int y = 0; y < mask_size; y+=mask_size/PRINT_RES1) {
			printf("%3.1f %3.1f %3.1f\t", 
				pixel_pos[n*mask_size*3 + y*3 + 0],
				pixel_pos[n*mask_size*3 + y*3 + 1],
				pixel_pos[n*mask_size*3 + y*3 + 2]);
		}
		printf("\n");
	}
	printf("\n");
	#endif

	// Round off pixel_pos to nearest voxel coordinates and translate to origo
	for (int n = 0; n < bscan_n; n++)
		for (int i = 0; i < mask_size; i++) {
			pixel_pos_c(n,i,0) = (int)(pixel_pos_c(n,i,0)/volume_spacing) - volume_origo[0];
			pixel_pos_c(n,i,1) = (int)(pixel_pos_c(n,i,1)/volume_spacing) - volume_origo[1];
			pixel_pos_c(n,i,2) = (int)(pixel_pos_c(n,i,2)/volume_spacing) - volume_origo[2];
		}

	holger_time(1, "Round off and translate pixel_pos");

	#ifdef PRINT
	printf("origo pixel_pos:\n");
	for (int n = 0; n < bscan_n; n+=bscan_n/PRINT_RES0) {
	//int n = bscan_n/2; {
		for (int y = 0; y < mask_size; y+=mask_size/PRINT_RES1) {
		//for (int y = 0; y < mask_size; y++) {
			printf("%4.0f %4.0f %4.0f\t", 
				pixel_pos[n*mask_size*3 + y*3 + 0],
				pixel_pos[n*mask_size*3 + y*3 + 1],
				pixel_pos[n*mask_size*3 + y*3 + 2]);
		}
		printf("\n");
	}
	printf("\n");
	#endif

	// Fill volume from pixel_ill
	#define inrange(x,a,b) ((x) >= (a) && (x) < (b))
	#define volume_a(x,y,z) (volume[(x) + (y)*volume_w + (z)*volume_w*volume_h])
	int volume_misses = 0;
	int volume_hits = 0;
	int volume_overwrites = 0;
	for (int a = 0; a < bscan_n; a++) {
		for (int i = 0; i < mask_size; i++) {
			/*float xf = pixel_pos_c(a,i,0);
			float yf = pixel_pos_c(a,i,1);
			float zf = pixel_pos_c(a,i,2);
			int x = xf-floor(xf) > 0.5 ? (int)xf + 1 : (int)xf;
			int y = yf-floor(yf) > 0.5 ? (int)yf + 1 : (int)yf;
			int z = zf-floor(zf) > 0.5 ? (int)zf + 1 : (int)zf;*/
			int x = pixel_pos_c(a,i,0);
			int y = pixel_pos_c(a,i,1);
			int z = pixel_pos_c(a,i,2);
			if (inrange(x,0,volume_w) && inrange(y,0,volume_h) && inrange(z,0,volume_n)) {
				volume_hits++;
				if (volume_a(x,y,z) != 0)
					volume_overwrites++;
				volume_a(x,y,z) = pixel_ill[a*mask_size + i];
			} else
				volume_misses++;
		}
	}

	holger_time(1, "Fill volume from pixel_ill");
	
	printf("volume_misses: %d\n", volume_misses);
	printf("volume_hits: %d\n", volume_hits);
	printf("volume_overwrites: %d\n", volume_overwrites);
	printf("hit ratio: %f\n", volume_hits/(float)(bscan_n*mask_size));
	printf("hit ratio excl. overwrites: %f\n", (volume_hits-volume_overwrites)/(float)(bscan_n*mask_size));

	#ifdef PRINT
	/*printf("volume:\n");
	//for (int z = 0; z < volume_n; z++) {
	int z = volume_n/2; {
		for (int y = 0; y < volume_h; y+=volume_h/PRINT_RES/PRINT_RES) {
			for (int x = 0; x < volume_w; x+=volume_w/PRINT_RES/PRINT_RES)
				printf("%d ", volume[x + y*volume_w + z*volume_w*volume_h]);
			printf("\n");
		}
		printf("\n");
	}*/
	#endif

	// Fill volume holes
	// (Assumes no black ultrasound input data)
	#define kernel_size 5
	#define half_kernel (kernel_size/2)
	#define cutoff (kernel_size*kernel_size*kernel_size/2 - half_kernel)
	for(int z = half_kernel; z < volume_n-half_kernel; z++) {
		for(int y = half_kernel; y < volume_h-half_kernel; y++) {
			for(int x = half_kernel; x < volume_w-half_kernel; x++) {
				if (volume_a(x,y,z) == 0) {
					int sum = 0;
					int sum_counter = 0;
					for(int i = -half_kernel; i <= half_kernel; i++)
						for(int j = -half_kernel; j <= half_kernel; j++)
							for(int k = -half_kernel; k <= half_kernel; k++)
								if (volume_a(x+i,y+j,z+k) != 0) {
									sum += volume_a(x+i,y+j,z+k);
									sum_counter++;
								}
					if (sum_counter > cutoff && sum/(float)sum_counter <= 255) volume_a(x,y,z) = sum/(float)sum_counter;
				}
			}
		}
	}

	holger_time(1, "Fill volume holes");

	#ifdef PRINT
	/*printf("volume with holes filled:\n");
	//for (int z = 0; z < volume_n; z++) {
	int z = volume_n/2; {
		for (int y = 0; y < volume_h; y+=volume_h/PRINT_RES/PRINT_RES) {
			for (int x = 0; x < volume_w; x+=volume_w/PRINT_RES/PRINT_RES)
				printf("%d ", volume[x + y*volume_w + z*volume_w*volume_h]);
			printf("\n");
		}
		printf("\n");
	}*/
	#endif

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

void reconstruct_vnn() {
	holger_time_start(1, "Reconstruct procedure (VNN)");

	float3 * plane_points = (float3 *) malloc(sizeof(float3)*bscan_n*3);
	plane_eq * bscan_plane_equations = (plane_eq *) malloc(sizeof(plane_eq)*bscan_n);

	volume_w = 512;
	volume_h = 256;
	volume_n = 512;
	volume_spacing = 0.08; // ultrasoundSample5 512*256*512
	//volume_spacing = 0.1;  // ultrasoundSample5 450*200*450
	float3 volume_origo = {92.5f, -20.0f, 281.5f}; // ultrasoundSample5 512*256*512
	//float3 volume_origo = {90.0f, -20.0f, 280.0f}; // ultrasoundSample5 450*200*450
	//float3 volume_origo = {102.0f, -10.0f, 272.0f}; // ultrasoundSample5
	volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
	memset(volume, 0, sizeof(unsigned char)*volume_w*volume_h*volume_n);

	//#define PRINT
	#define PRINT_RES0 3
	#define PRINT_RES1 30

	#ifdef PRINT
	printf("volume_whn: %d %d %d (%5.2f %5.2f %5.2f)\n", volume_w, volume_h, volume_n, volume_w*volume_spacing,volume_h*volume_spacing, volume_n*volume_spacing);
	#endif

	#define plane_points_c(n,i) (plane_points[(n)*3 + (i)])
	#define pos_matrices_int_a(n,x,y) (pos_matrices_interpolated[(n)*12 + (y)*4 + (x)])

	// Multiply cal_matrix into pos_matrices
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

	// Interpolate pos_timetags and pos_matrices between bscan_timetags so that pos_n == bscan_n
	// Assumes constant frequency of pos_timetags and bscan_timetags and bscan_n < pos_n

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
		int pos_i = i*pos_n/(float)bscan_n;
		for (int matrix_i = 0; matrix_i < 12; matrix_i++)
			pos_matrices_interpolated[i*12 + matrix_i] = 
			pos_matrices[i*12 + matrix_i] + 
			(bscan_timetags[i]-pos_timetags[pos_i]) *
			(pos_matrices[(i+1)*12 + matrix_i] - pos_matrices[i*12 + matrix_i]) /
			(pos_timetags[pos_i+1] - pos_timetags[pos_i]);
		pos_timetags_interpolated[i] = bscan_timetags[i];
	}
	pos_n = bscan_n;

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
				sum += pos_matrices_int_a(n,0,y)*plane_points_c(n,i).x;
				sum += pos_matrices_int_a(n,1,y)*plane_points_c(n,i).y;
				sum += pos_matrices_int_a(n,2,y)*plane_points_c(n,i).z;
				sum += pos_matrices_int_a(n,3,y);
				sums[y] = sum;
			}
			memcpy(&plane_points_c(n,i,0), sums, 3*sizeof(float));
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
	for (int n = 0; n < bscan_n; n++) {
		printf("bscan_plane_equations[n]: %f %f %f %f\n", bscan_plane_equations[n*4+0], bscan_plane_equations[n*4+1], bscan_plane_equations[n*4+2], bscan_plane_equations[n*4+3]);
	}
	#endif

	holger_time(1, "Fill bscan_plane_equations");

	// Fill volume from pixel_ill
	#define inrange(x,a,b) ((x) >= (a) && (x) < (b))
	#define volume_a(x,y,z) (volume[(x) + (y)*volume_w + (z)*volume_w*volume_h])
	float kernel_radius = volume_spacing*5;
	printf("kernel_radius: %f\n", kernel_radius);

	// Find bscan closest to voxel (0,0,0)
	int current_bscan = -1;
	float dist = -1;
	for (int n = 0; n < bscan_n; n++) {
		float3 voxel_000 = {0.0f, 0.0f, 0.0f};
		float temp = distance(voxel_000, bscan_plane_equations[n]);
		//printf("n: %d temp: %9f \t norm: %5.2f %5.2f %5.2f d: %5.2f \n", n, temp, bscan_plane_equations[n].a, bscan_plane_equations[n].b, bscan_plane_equations[n].c, bscan_plane_equations[n].d);
		if (dist == -1 || abs(temp) < abs(dist)) {
			current_bscan = n;
			dist = temp;
		}
	}

	printf("curr_bscan: %d  dist: %f\n", current_bscan, dist);

	for (int y = 0; y < volume_h; y++) {
		for (int _z = 0; _z < volume_n; _z++) {
			for (int _x = 0; _x < volume_w; _x++) {

				// fast slice selection voxel traversal:
				int z = _z; if (y%2==1) z = volume_n-_z-1;
				int x = _x; if (_z%2==1) x = volume_w-_x-1;

				//bool print_voxel = x == volume_w/2 && z == volume_n/2;

				float3 voxel_coord = {x*volume_spacing, y*volume_spacing, z*volume_spacing};
				//if (print_voxel) printf("xyz: %d %d %d (%5.2f, %5.2f, %5.2f) \n", x, y, z, voxel_coord.x, voxel_coord.y, voxel_coord.z);

				// Find bscan closest to voxel:
				// Uses modified fast slice selection range (ideally +-1, but bscans are not completely ordered)
				float dist = 10000; // inf
				float temp;
				bool done_up = false;
				bool done_down = false;
				int min_bscan = 0;
				int n;
				for (int i = 1; !done_down || !done_up; i++) {
					if (!done_up) {
						n = current_bscan + i;
						temp = abs(distance(voxel_coord, bscan_plane_equations[n]));
						min_bscan = temp < dist ? n : min_bscan;
						dist = min(dist, temp);
						done_up = temp-dist > kernel_radius || n == bscan_n-1;
						//if (print_voxel) printf("n: %d  temp: %f \t norm: %5.2f %5.2f %5.2f d: %5.2f \n", n, temp, bscan_plane_equations[n].a, bscan_plane_equations[n].b, bscan_plane_equations[n].c, bscan_plane_equations[n].d);
					}

					if (!done_down) {
						n = current_bscan - i;
						temp = abs(distance(voxel_coord, bscan_plane_equations[n]));
						min_bscan = temp < dist ? n : min_bscan;
						dist = min(dist, temp);
						done_down = temp-dist > kernel_radius || n == 0;
						//if (print_voxel) printf("n: %d  temp: %f \t norm: %5.2f %5.2f %5.2f d: %5.2f \n", n, temp, bscan_plane_equations[n].a, bscan_plane_equations[n].b, bscan_plane_equations[n].c, bscan_plane_equations[n].d);
					}
				}
				current_bscan = min_bscan;
				dist = distance(voxel_coord, bscan_plane_equations[current_bscan]);

				//if (print_voxel) printf("curr_bscan: %d  dist: %f\n", current_bscan, dist);
				
				float3 normal = {bscan_plane_equations[current_bscan].a, bscan_plane_equations[current_bscan].b, bscan_plane_equations[current_bscan].c};
				float3 corner0 = plane_points_c(current_bscan,0);
				float3 cornerx = plane_points_c(current_bscan,1);
				float3 cornery = plane_points_c(current_bscan,2);

				float3 p = sub(add(voxel_coord, scale(-dist, normal)), corner0);
				float3 x_vector = normalize(sub(cornerx, corner0));
				float3 y_vector = normalize(sub(cornery, corner0));

				int px = dot(p, x_vector)/bscan_spacing_x;
				int py = dot(p, y_vector)/bscan_spacing_y;

				if (px >= 0 && px < bscan_w && py >= 0 && py < bscan_h)
					if (abs(dist) < kernel_radius)
						if (mask[px + py*bscan_w] != 0)
							volume_a(x,y,z) = bscans[px + py*bscan_w + current_bscan*bscan_w*bscan_h];

				//if (print_voxel) printf("\n");
			}
		}
	}

	holger_time(1, "Fill volume from pixel_ill");

	holger_time_print(1);
}