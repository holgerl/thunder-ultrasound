#ifndef _THUNDER_UTILS
#define _THUNDER_UTILS

#include <stdlib.h>
#include <stdio.h>

typedef struct {
  float x;
  float y;
  float z;
} float3;

typedef struct {
  float a;
  float b;
  float c;
	float d;
} plane_eq;
 
float3 cross(float3 v, float3 w);
float3 sub(float3 v, float3 w);
float3 normalize(float3 v);
float3 add(float3 v, float3 w);
float3 scale(float a, float3 v);
float dot(float3 v, float3 w);
#define distance(v, plane) (plane.a*v.x + plane.b*v.y + plane.c*v.z + plane.d)/sqrt(plane.a*plane.a + plane.b*plane.b + plane.c*plane.c)

#endif