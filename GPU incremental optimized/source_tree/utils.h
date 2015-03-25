#ifndef _THUNDER_UTILS
#define _THUNDER_UTILS

#include <stdlib.h>
#include <stdio.h>
#include <CL/cl.h>
#include <NVIDIA/vector_math.h>

void random_init(float * data, int length);
void inc_init(float * data, int length);
char * file2string(const char* filename, const char* preamble, size_t* final_length);
//void ocl_set_args(cl_kernel kernel, int n, ...);
cl_kernel ocl_kernel_build(cl_program program, cl_device_id device, char * kernel_name);
cl_mem ocl_create_buffer(cl_context context, cl_mem_flags flags, size_t size, void * host_data);
void ocl_check_error(int err, char * info = "");

typedef struct {
  float4 corner0;
  float4 cornerx;
  float4 cornery;
} plane_pts;
 
#define distance(v, plane) (plane.x*v.x + plane.y*v.y + plane.z*v.z + plane.w)/sqrt(plane.x*plane.x + plane.y*plane.y + plane.z*plane.z)
#define inrange(x,a,b) ((x) >= (a) && (x) < (b))
#define max3(a,b,c) max(a, max(b, c))

// cross product (for float4 without taking 4th dimension into account)
inline __host__ __device__ float4 cross(float4 a, float4 b){ 
    return make_float4(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x, 0); 
}

#endif