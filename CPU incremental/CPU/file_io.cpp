#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

unsigned char * bscans;
unsigned char * mask;
float * pos_timetags, * pos_matrices, * bscan_timetags, * cal_matrix;
int bscan_w, bscan_h, bscan_n, pos_n;
float bscan_spacing_x, bscan_spacing_y;
char * buffer;

#define BUFFER_SIZE (10*1024*1024)
#define MAX_TIMETAGS 2048
#define MAX_MATRICES 2048

size_t read_str(char * filename) {
	FILE * file = fopen(filename, "r");
	size_t read_count = 0;
	if (file == NULL) printf("ERROR opening file: %s\n", filename);
	else read_count = fread(buffer, sizeof(char), BUFFER_SIZE, file);
	buffer[read_count] = '\0';
	return read_count;
}

void read_header(char * filename) {
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

void read_positions(char * filename) {
	int read_count = read_str(filename);
	pos_timetags = (float *) malloc(sizeof(float)*MAX_TIMETAGS);
	pos_matrices = (float *) malloc(sizeof(float)*12*MAX_MATRICES);
	
	// Special care is taken because of awkward/faulty format of *.pos files

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

void read_calibration(char * filename) {
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

void read_timetags(char * filename) {
	read_str(filename);
	bscan_timetags = (float *) malloc(sizeof(float)*bscan_n);
	int i = 0;

	char * pch = strtok(buffer, "\n\r");
	while (pch != NULL) {
		bscan_timetags[i++] = atof(pch);
		pch = strtok (NULL, "\n\r");
	}

	if (i != bscan_n)
		printf("ERROR: i != bscan_n\n");
}

void read_rawdata(char * filename) {
	int size = bscan_w*bscan_h*bscan_n;
	bscans = (unsigned char *) malloc(sizeof(unsigned char)*size);
	FILE * file = fopen(filename, "r");
	if (file == NULL) printf("ERROR opening raw data file: %s\n", filename);
	fread(bscans, sizeof(unsigned char), size, file);
}

void read_mask(char * filename) {
	int size = bscan_w*bscan_h;
	mask = (unsigned char *) malloc(sizeof(unsigned char)*size);
	FILE * file = fopen(filename, "r");
	if (file == NULL) printf("ERROR opening mask file: %s\n", filename);
	fread(mask, sizeof(unsigned char), size, file);
}

void read_input(char * mhd_filename, char * pos_filename, char * tim_filename, char * msk_filename, char * vol_filename, char * cal_filename) {
	buffer = (char *) malloc(sizeof(char)*BUFFER_SIZE);
	read_header(mhd_filename);
	read_positions(pos_filename);
	read_timetags(tim_filename);
	read_mask(msk_filename);
	read_rawdata(vol_filename);
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