#include <stdlib.h>
#include <stdio.h>
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