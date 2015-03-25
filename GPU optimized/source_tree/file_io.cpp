#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "global_const.h"

unsigned char * bscans;
unsigned char * mask;
float * pos_timetags, * pos_matrices, * bscan_timetags, * cal_matrix;
int bscan_w, bscan_h, bscan_n, pos_n;
float bscan_spacing_x, bscan_spacing_y;
char * buffer;

#define BUFFER_SIZE (10*1024*1024)
#define MAX_TIMETAGS 2048
#define MAX_MATRICES 2048

size_t read_str(const char * filename) {
	FILE * file = fopen(filename, "r");
	size_t read_count = 0;
	if (file == NULL) printf("ERROR opening file: %s\n", filename);
	else read_count = fread(buffer, sizeof(char), BUFFER_SIZE, file);
	buffer[read_count] = '\0';
	return read_count;
}

void read_header(const char * filename) {
	int read_count = read_str(filename);
	int scan_count = sscanf(buffer, "NDims = 3 DimSize = %d %d %d\n ElementSpacing = %f %f %*f\n", &bscan_w, &bscan_h, &bscan_n, &bscan_spacing_x, &bscan_spacing_y);
	if (read_count > 500 || scan_count != 5) printf("ERROR in header file format\n");
	else printf("%s\n", buffer);
}

#define IS_WS(c) (c == '\n' ||  c == '\r' || c == ' ' || c == '\t')

// Adds a float to either pos_timetags or pos_matrices
void add_pos(char * ptr, int length) {
	static int counter = 0;
	static int pos_matrices_i = 0;
	char * number = (char *) malloc(length*sizeof(char)+1);
	memcpy(number, ptr, length);
	number[length] = '\0';
	if (counter%13==0)
		pos_timetags[pos_n++] = atof(number);
	else
		pos_matrices[pos_matrices_i++] = atof(number);
	counter++;
}

// Splits string of concatenated floats into floats
void extract(int t0, int t1, char * buffer) {
	char * extracted = (char *) malloc((t1-t0+1)*sizeof(char));
	memcpy(extracted, &buffer[t0], t1-t0);
	extracted[t1-t0] = '\0';
	//bool print = pos_n-1 == (216718-216632)/2;

	int dots = 0;
	char * pch = strchr(extracted, '.');
  while (pch != NULL) {
		dots++;
		pch = strchr(pch+1, '.');
  }

	char * ptr = strchr(extracted, '.');
	int base = 0;
	if (ptr != NULL) {
		ptr = strchr(ptr+1, '.');
		while (ptr != NULL) {
			dots++;
			int length = ptr-extracted-base-1;
			if (extracted[base+length-1] == '-')
				length--;
			add_pos(extracted+base, length);
			base += length;
			ptr = strchr(ptr+1, '.');
		}
		add_pos(extracted+base, t1-t0-base);
	} else {
		add_pos(extracted, t1-t0);
	}
}

void read_positions(const char * filename) {
	int read_count = read_str(filename);
	pos_timetags = (float *) malloc(sizeof(float)*MAX_TIMETAGS);
	pos_matrices = (float *) malloc(sizeof(float)*12*MAX_MATRICES);
	
	// Special care is taken because of awkward/faulty format of *.pos files
	// (In some files, two or more floats are concatenated with no whitespace between them)

	// Split into whitespace-divided strings:
	int t0 = 0;
	for (int t1 = 1; t1 < read_count; t1++) {
		char c = buffer[t1];
		if (c == '\0') {
			break;
		} else if (IS_WS(c)) {
			if (!IS_WS(buffer[t1-1]))
				extract(t0, t1, buffer);
			t0 = t1+1;
		}
	}
}

void read_calibration(const char * filename) {
	int read_count = read_str(filename);
	cal_matrix = (float *) malloc(sizeof(float)*16);
	int counter = 0;

	int t0 = 0;
	for (int t1 = 1; t1 < read_count; t1++) {
		char c = buffer[t1];
		if (c == '\0') {
			break;
		} else if (IS_WS(c)) {
			if (!IS_WS(buffer[t1-1])) {
				char * extracted = (char *) malloc((t1-t0+1)*sizeof(char));
				memcpy(extracted, &buffer[t0], t1-t0);
				extracted[t1-t0] = '\0';
				cal_matrix[counter++] = atof(extracted);
			}
			t0 = t1+1;
		}
	}
}

void read_timetags(const char * filename) {
	read_str(filename);
	bscan_timetags = (float *) malloc(sizeof(float)*bscan_n);
	int i = 0;

	char * pch = strtok(buffer, "\n\r");
	while (pch != NULL) {
		bscan_timetags[i++] = atof(pch);
		pch = strtok (NULL, "\n\r");
	}

	if (i != bscan_n)
		printf("ERROR: number of bscan_timetags != bscan_n\n");
}

void read_bscans(const char * filename) {
	int size = bscan_w*bscan_h*bscan_n;
	bscans = (unsigned char *) malloc(sizeof(unsigned char)*size);
	FILE * file = fopen(filename, "r");
	if (file == NULL) printf("ERROR opening raw data file: %s\n", filename);
	fread(bscans, sizeof(unsigned char), size, file);
}

void read_mask(const char * filename) {
	int size = bscan_w*bscan_h;
	mask = (unsigned char *) malloc(sizeof(unsigned char)*size);
	FILE * file = fopen(filename, "r");
	if (file == NULL) printf("ERROR opening mask file: %s\n", filename);
	fread(mask, sizeof(unsigned char), size, file);
	/*for (int y = 0; y < bscan_h; y++) {
		for (int x=0; x<bscan_w; x++) {
			mask[x + y*bscan_w] = 0;
			if (x > 250 && x < 430 && y > 150 && y< 350)
				mask[x + y*bscan_w] = 255;
		}
	}*/
}

void read_input(const char * mhd_filename, const char * pos_filename, const char * tim_filename, const char * msk_filename, const char * vol_filename, const char * cal_filename) {
	buffer = (char *) malloc(sizeof(char)*BUFFER_SIZE);
	read_header(mhd_filename);
	read_positions(pos_filename);
	read_timetags(tim_filename);
	read_mask(msk_filename);
	read_bscans(vol_filename);
	read_calibration(cal_filename);
	free(buffer);

	/*printf("cal_matrix:\n");
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++)
			printf("%f ", cal_matrix[i*4 + j]);
		printf("\n");
	}
	printf("\n");
	printf("pos_timetags:\n");
	for (int i = 0; i < pos_n; i++)
		printf("%f\t", pos_timetags[i]);
	printf("\n");
	printf("bscan_timetags:\n");
	for (int i = 0; i < bscan_n; i++)
		printf("%f\t", bscan_timetags[i]);
	printf("\n");
	printf("pos_matrices:\n");
	for (int i = 0; i < pos_n; i++) {
		for (int j = 0; j < 3; j++) {
			for (int k = 0; k < 4; k++)
				printf("%12.7f ", pos_matrices[i*4*3 + j*4 + k]);
			printf("\n");
		}
		printf("\n");
	}*/
}

extern unsigned char * volume;

void write_output(const char * filename) {
	FILE * file_stream = NULL;
  file_stream = fopen(filename, "w");
	if(file_stream == NULL) return;
	if (fwrite(volume, sizeof(unsigned char)*VOL_W*VOL_H*VOL_N, 1, file_stream) != 1) {
		fclose(file_stream);
		return;
	}

	fclose(file_stream);

	float volume_index_float = 0.0f;
	int foo = volume_index_float*VOL_W;
	unsigned char * volume_slice_zy = (unsigned char *) malloc(VOL_N*VOL_H);
	for (int y = 0; y < VOL_H; y++)
		for (int x = 0; x < VOL_N; x++)
			volume_slice_zy[x + y*VOL_N] = volume[foo + y*VOL_W + x*VOL_W*VOL_H];

	file_stream = fopen("volume_slice_zy.raw", "w");
	if(file_stream == NULL) return;
	if (fwrite(volume_slice_zy, VOL_N*VOL_H, 1, file_stream) != 1) {
		fclose(file_stream);
		return;
	}
	fclose(file_stream);

	unsigned char * volume_slice_zx = (unsigned char *) malloc(VOL_N*VOL_W);
	foo = volume_index_float*VOL_H;
	for (int y = 0; y < VOL_W; y++)
		for (int x = 0; x < VOL_N; x++)
			volume_slice_zx[x + y*VOL_N] = volume[y + foo*VOL_W + x*VOL_W*VOL_H];

  file_stream = fopen("volume_slice_zx.raw", "w");
	if(file_stream == NULL) return;
	if (fwrite(volume_slice_zx, VOL_N*VOL_W, 1, file_stream) != 1) {
		fclose(file_stream);
		return;
	}
	fclose(file_stream);

	foo = volume_index_float*VOL_N;
	file_stream = fopen("volume_slice_xy.raw", "w");
	if(file_stream == NULL) return;
	if (fwrite(volume+foo*VOL_W*VOL_H, VOL_N*VOL_W, 1, file_stream) != 1) {
		fclose(file_stream);
		return;
	}
	fclose(file_stream);
}