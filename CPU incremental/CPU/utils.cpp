#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "utils.h"

float3 cross(float3 v, float3 w) {
  float3 c = {
    v.y*w.z - v.z*w.y,
    v.z*w.x - v.x*w.z,
    v.x*w.y - v.y*w.x
	};
  return c;
}

float3 sub(float3 v, float3 w) {
  float3 c = {
    v.x - w.x,
    v.y - w.y,
    v.z - w.z
	};
  return c;
}

float3 normalize(float3 v) {
	float length = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
  float3 c = {
    v.x/length,
    v.y/length,
    v.z/length
	};
  return c;
}

float3 add(float3 v, float3 w) {
  float3 c = {
    v.x + w.x,
    v.y + w.y,
    v.z + w.z
	};
  return c;
}

float3 scale(float a, float3 v) {
  float3 c = {
    a * v.x,
    a * v.y,
    a * v.z
	};
  return c;
}

float dot(float3 v, float3 w) {
  return v.x*w.x + v.y*w.y + v.z*w.z;
}

void create_dummy_vol_file() {
	size_t vol_size = sizeof(unsigned char)*768*576*437;
	unsigned char * vol = (unsigned char *) malloc(vol_size);
	for (int i = 0; i < 437; i++) {
		for (int j = 0; j < 576; j++) {
			for (int k = 0; k < 768; k++) {
				unsigned char foo = inrange(k%(768/10), 0, 4) || inrange(j%(576/10), 0, 4);
				foo *= 255;
				vol[k + j*768 + i*576*768] = foo;
			}
		}
	}

	FILE * file_stream = NULL;
  file_stream = fopen("C:\\Master Holger\\Simple test input\\Lines\\lines.vol", "w");
	if(file_stream == NULL) return;
	if (fwrite(vol, vol_size, 1, file_stream) != 1) {
		fclose(file_stream);
		return;
	}

  /*
	size_t msk_size = sizeof(unsigned char)*768*576;
	unsigned char * msk = (unsigned char *) malloc(msk_size);
	memset(msk, 255, msk_size);
	
	file_stream = fopen("C:\\Master Holger\\Simple test input\\Lines\\lines.msk", "w");
	if(file_stream == NULL) return;
	if (fwrite(msk, msk_size, 1, file_stream) != 1) {
		fclose(file_stream);
		return;
	}
	*/

	fclose(file_stream);
}