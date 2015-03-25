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

typedef struct {
  float3 corner0;
  float3 cornerx;
  float3 cornery;
} plane_pts;
 
float3 cross(float3 v, float3 w);
float3 sub(float3 v, float3 w);
float3 normalize(float3 v);
float3 add(float3 v, float3 w);
float3 scale(float a, float3 v);
float dot(float3 v, float3 w);
#define distance(v, plane) (plane.a*v.x + plane.b*v.y + plane.c*v.z + plane.d)/sqrt(plane.a*plane.a + plane.b*plane.b + plane.c*plane.c)

#define inrange(x,a,b) ((x) >= (a) && (x) < (b))
#define max3(a,b,c) max(a, max(b, c))

void create_dummy_vol_file();

#endif