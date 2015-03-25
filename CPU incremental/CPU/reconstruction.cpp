#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "utils.h"
#include "holger_time.h"
#include "ultrasound_simulator.h"

extern unsigned char * mask;
extern int bscan_w, bscan_h;
extern float * cal_matrix;
extern float bscan_spacing_x, bscan_spacing_y;

#define pos_matrix_a(x,y) (pos_matrix[(y)*4 + (x)])
#define volume_a(x,y,z) (volume[(x) + (y)*volume_w + (z)*volume_w*volume_h])
#define bscans_queue_a(n, x, y) bscans_queue[n][(x) + (y)*bscan_w]

//#define PRINT
#define PRINT_RES0 3
#define PRINT_RES1 30
#define PRINT_RES2 6000

#define BSCAN_WINDOW 4 // must be >= 4 if PT
#define PT_OR_DW 1 // 0=PT, 1=DW

int volume_w = 512;
int volume_h = 256;
int volume_n = 512;
unsigned char * volume = (unsigned char *) malloc(sizeof(unsigned char)*volume_w*volume_h*volume_n);
float volume_spacing = 0.08f;
float3 volume_origo = {90.0f, -20.0f, 280.0f}; // ultrasoundSample5

float3 * x_vector_queue = (float3 *) malloc(BSCAN_WINDOW*sizeof(float3));
float3 * y_vector_queue = (float3 *) malloc(BSCAN_WINDOW*sizeof(float3));
plane_pts * plane_points_queue = (plane_pts *) malloc(BSCAN_WINDOW*sizeof(plane_pts));
plane_eq * bscan_plane_equation_queue = (plane_eq *) malloc(BSCAN_WINDOW*sizeof(plane_pts));
float3 * intersections = (float3 *) malloc(sizeof(float3)*2*max3(volume_w, volume_h, volume_n)*max3(volume_w, volume_h, volume_n));
unsigned char * * bscans_queue = (unsigned char * *) malloc(BSCAN_WINDOW * bscan_w * bscan_h * sizeof(unsigned char));
float * * pos_matrices_queue = (float * *) malloc(BSCAN_WINDOW * sizeof(float)*12);
float * pos_timetags_queue = (float *) malloc(BSCAN_WINDOW * sizeof(float));
float * bscan_timetags_queue = (float *) malloc(BSCAN_WINDOW * sizeof(float));

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
	float3 temp0 = {0.0f,0.0f,0.0f};
	plane_points_queue[BSCAN_WINDOW-1].corner0 = temp0;
	float3 temp1 = {0.0f, bscan_w*bscan_spacing_x, 0.0f};
	plane_points_queue[BSCAN_WINDOW-1].cornerx = temp1;
	float3 temp2 = {0.0f, 0.0f, bscan_h*bscan_spacing_y};
	plane_points_queue[BSCAN_WINDOW-1].cornery = temp2;

	// Transform plane_points
	float3 * foo = (float3 *) &plane_points_queue[BSCAN_WINDOW-1];
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
		foo[i] = sub(foo[i], volume_origo);
	}
}

void insert_plane_eq() {
	// Fill bscan_plane_equation
	float3 a = plane_points_queue[BSCAN_WINDOW-1].corner0;
	float3 b = plane_points_queue[BSCAN_WINDOW-1].cornerx;
	float3 c = plane_points_queue[BSCAN_WINDOW-1].cornery;
	float3 _normal = normalize(cross(sub(a,b), sub(c,a)));

	bscan_plane_equation_queue[BSCAN_WINDOW-1].a = _normal.x;
	bscan_plane_equation_queue[BSCAN_WINDOW-1].b = _normal.y;
	bscan_plane_equation_queue[BSCAN_WINDOW-1].c = _normal.z;
	bscan_plane_equation_queue[BSCAN_WINDOW-1].d = -_normal.x*a.x - _normal.y*a.y - _normal.y*a.z;

	x_vector_queue[BSCAN_WINDOW-1] = normalize(sub(plane_points_queue[BSCAN_WINDOW-1].cornerx, plane_points_queue[BSCAN_WINDOW-1].corner0));
	y_vector_queue[BSCAN_WINDOW-1] = normalize(sub(plane_points_queue[BSCAN_WINDOW-1].cornery, plane_points_queue[BSCAN_WINDOW-1].corner0));
}

int find_intersections(int axis) {
	int intersection_counter = 0;

	float3 Rd = {axis == 0, axis == 1, axis == 2};
	int iter_end[3] = {(axis != 0)*volume_w+(axis==0), (axis != 1)*volume_h+(axis==1), (axis != 2)*volume_n+(axis==2)};

	for (int x = 0; x < iter_end[0]; x++) {
		for (int y = 0; y < iter_end[1]; y++) {
			for (int z = 0; z < iter_end[2]; z++) {
				bool invalid = false;
				for (int f = 0; f < 2; f++) {
					int i = f==0 ? BSCAN_WINDOW/2-1 : BSCAN_WINDOW/2;
					//int i = f==0 ? BSCAN_WINDOW/2-BSCAN_WINDOW/4-1 : BSCAN_WINDOW/2+BSCAN_WINDOW/4; // Alternatively ...
					//int i = f==0 ? 0 : BSCAN_WINDOW-1; // Alternatively ...
					float3 Pn = {bscan_plane_equation_queue[i].a, bscan_plane_equation_queue[i].b, bscan_plane_equation_queue[i].c};
					float3 R0 = {x*volume_spacing, y*volume_spacing, z*volume_spacing};
					float Vd = dot(Pn, Rd);
					float V0 = -(dot(Pn, R0) + bscan_plane_equation_queue[i].d);
					float t = V0/Vd;
					if (Vd == 0) invalid = true;

					float3 intersection = add(R0, scale(t, Rd));
					intersections[intersection_counter*2 + f] = intersection;
				}
				if (!invalid) intersection_counter++;
			}	
		}
	}

	return intersection_counter;
}

bool project(unsigned char * bilinear, float * dist, float3 voxel_coord, int q_idx) {
	float3 normal = {bscan_plane_equation_queue[q_idx].a, bscan_plane_equation_queue[q_idx].b, bscan_plane_equation_queue[q_idx].c};

	float dist0 = abs(distance(voxel_coord, bscan_plane_equation_queue[q_idx]));
	float3 p0 = sub(add(voxel_coord, scale(-dist0, normal)), plane_points_queue[q_idx].corner0);
	float px0 = dot(p0, x_vector_queue[q_idx])/bscan_spacing_x;
	float py0 = dot(p0, y_vector_queue[q_idx])/bscan_spacing_y;
	float xa = px0-floor(px0);
	float ya = py0-floor(py0);
	int xa0 = (int)px0;
	int ya0 = (int)py0;

	bool valid = false;
	float bilinear0 = 0;

	if (inrange(xa0, 0, bscan_w) && inrange(ya0, 0, bscan_h) && inrange(xa0+1, 0, bscan_w) && inrange(ya0+1, 0, bscan_h))
		if (mask[xa0 + ya0*bscan_w] != 0 && mask[xa0+1 + (ya0+1)*bscan_w] != 0 && mask[xa0+1 + ya0*bscan_w] != 0 && mask[xa0 + (ya0+1)*bscan_w] != 0) {
			if (bilinear != NULL) bilinear0 = bscans_queue_a(q_idx,xa0,ya0)*(1-xa)*(1-ya) + bscans_queue_a(q_idx,xa0+1,ya0)*xa*(1-ya) + bscans_queue_a(q_idx,xa0,ya0+1)*(1-xa)*ya + bscans_queue_a(q_idx,xa0+1,ya0+1)*xa*ya;
			valid = true;
		}

	*dist = dist0;
	if (bilinear != NULL) *bilinear = (unsigned char) bilinear0;
	return valid;
}

void fill_voxels(int intersection_counter) {
	int prt_c = 0;

	printf("fill_voxels %d\n", intersection_counter);

	for (int i = 0; i < intersection_counter; i++) {
		float3 intrs0 = intersections[i*2 + 0];
		float3 intrs1 = intersections[i*2 + 1];
		
		intrs0 = scale(1/volume_spacing, intrs0);
		intrs1 = scale(1/volume_spacing, intrs1);
		int x0 = min(intrs0.x,intrs1.x);
		int x1 = max(x0+1, max(intrs0.x,intrs1.x));
		int y0 = min(intrs0.y,intrs1.y);
		int y1 = max(y0+1, max(intrs0.y,intrs1.y));
		int z0 = min(intrs0.z,intrs1.z);
		int z1 = max(z0+1, max(intrs0.z,intrs1.z));

		//printf("%d %d %d %d %d %d \n", x0, y0, z0, x1, y1, z1);
		int safeness = 100;
		if (x1-x0 > safeness || y1-y0 > safeness || z1-z0 > safeness) break;

		int safety = 0;
		for (int z = z0; z <= z1; z++)
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++) {
					float3 voxel_coord = {x*volume_spacing,y*volume_spacing,z*volume_spacing};
					if (inrange(x, 0, volume_w) && inrange(y, 0, volume_h) && inrange(z, 0, volume_n)) {
						float contribution = 0;
						if (PT_OR_DW) { // DW
							float dists[BSCAN_WINDOW];
							unsigned char bilinears[BSCAN_WINDOW];
							bool valid = true;
							float G = 0;
							for (int n = 0; n < BSCAN_WINDOW; n++) {
								valid &= project(&bilinears[n], &dists[n], voxel_coord, n);
								G += 1/dists[n];
								contribution += bilinears[n]/dists[n];
							}
							if (!valid) continue;

							contribution /= G;
						} else { // PT
							
							// Find virtual plane time stamp:
							float dists[4];
							bool valid = true;
							for (int n = 0; n < 4; n++)
								valid &= project(NULL, &dists[n], voxel_coord, BSCAN_WINDOW/2-2+n);
							if (!valid) continue;
							float G = dists[1] + dists[2];
							float t = dists[2]/G*bscan_timetags_queue[BSCAN_WINDOW/2-1] + dists[1]/G*bscan_timetags_queue[BSCAN_WINDOW/2];

							// Cubic interpolate 4 bscan plane equations, corner0s and x- and y-vectors:
							plane_eq v_plane_eq = {0,0,0,0};
							float3 v_corner0 = {0,0,0};
							float3 v_x_vector = {0,0,0};
							float3 v_y_vector = {0,0,0};
							for (int k = 0; k < 4; k++) {
								float phi = 0;
								float a = -1/2.0f;
								float abs_t = abs((t-bscan_timetags_queue[BSCAN_WINDOW/2-2+k]))/(bscan_timetags_queue[1]-bscan_timetags_queue[0]);
								if (inrange(abs_t, 0, 1))
									phi = (a+2)*abs_t*abs_t*abs_t - (a+3)*abs_t*abs_t + 1;
								else if (inrange(abs_t, 1, 2))
									phi = a*abs_t*abs_t*abs_t - 5*a*abs_t*abs_t + 8*a*abs_t - 4*a;
								v_plane_eq.a += bscan_plane_equation_queue[BSCAN_WINDOW/2-2+k].a*phi;
								v_plane_eq.b += bscan_plane_equation_queue[BSCAN_WINDOW/2-2+k].b*phi;
								v_plane_eq.c += bscan_plane_equation_queue[BSCAN_WINDOW/2-2+k].c*phi;
								v_plane_eq.d += bscan_plane_equation_queue[BSCAN_WINDOW/2-2+k].d*phi;
								v_corner0 = add(v_corner0, scale(phi, plane_points_queue[BSCAN_WINDOW/2-2+k].corner0));
								v_x_vector = add(v_x_vector, scale(phi, x_vector_queue[BSCAN_WINDOW/2-2+k]));
								v_y_vector = add(v_y_vector, scale(phi, y_vector_queue[BSCAN_WINDOW/2-2+k]));
							}

							// Find 2D coordinates on virtual plane:
							float3 p0 = sub(voxel_coord, v_corner0);
							float px0 = dot(p0, v_x_vector)/bscan_spacing_x;
							float py0 = dot(p0, v_y_vector)/bscan_spacing_y;
							float xa = px0-floor(px0);
							float ya = py0-floor(py0);
							int xa0 = (int)px0;
							int ya0 = (int)py0;

							// Distance weight 4 bilinears:
							float F = 0;
							for (int n = 0; n < 4; n++) {
								float bilinear0 = bscans_queue_a(BSCAN_WINDOW/2-2+n,xa0,ya0)*(1-xa)*(1-ya) + bscans_queue_a(BSCAN_WINDOW/2-2+n,xa0+1,ya0)*xa*(1-ya) + bscans_queue_a(BSCAN_WINDOW/2-2+n,xa0,ya0+1)*(1-xa)*ya + bscans_queue_a(BSCAN_WINDOW/2-2+n,xa0+1,ya0+1)*xa*ya;
								F += 1/dists[n];
								contribution += bilinear0/dists[n];
							}
							contribution /= F;
						}
						
						// Compounding methods:
						//if (volume_a(x,y,z) != 0) volume_a(x,y,z) = (volume_a(x,y,z) + contribution)/2;	else volume_a(x,y,z) = contribution;
						//volume_a(x,y,z) = max(volume_a(x,y,z), contribution);
						//if (volume_a(x,y,z) == 0) volume_a(x,y,z) = contribution;
						volume_a(x,y,z) = (unsigned char) contribution;
					}
				}
	}
}

void reconstruct() {
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

	int counter = BSCAN_WINDOW;

	while(true) {
		// Retrieve ultrasound data and perform ye olde switcheroo
		shift_queues();
		wait_for_input();
		if (end_of_data()) break;

		holger_time(1, "Retrieve ultrasound data");
		printf("Reconstructing %d ...\n", counter);

		calibrate_pos_matrix(pos_matrices_queue[BSCAN_WINDOW-1], cal_matrix);
		holger_time(1, "Calibrate");
		printf("Calibrate\n");

		insert_plane_points(pos_matrices_queue[BSCAN_WINDOW-1]);
		holger_time(1, "Fill, transform and translate plane_points");
		printf("plane_points\n");

		insert_plane_eq();
		holger_time(1, "Fill bscan_plane_equation");
		printf("bscan_plane_equation\n");

		//int axis = find_orthogonal_axis(bscan_plane_equation);	// TODO: Function that finds axis most orthogonal to bscan plane
		int axis = 0;																							// Actually, turns out that the output is pretty much equal for any axis, 
																															// but the computation time varies (1X - 2X)

		int intersection_counter = find_intersections(axis);
		holger_time(1, "Find ray-plane intersections");
		printf("intersections\n");

		fill_voxels(intersection_counter);
		holger_time(1, "Fill voxels");
		printf("voxels\n");

		counter++;
	}

	holger_time_print(1);
}