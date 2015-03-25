#include <stdlib.h>
#include <stdio.h>
#include <CL/cl.h>
#include <string.h>
#include <math.h>
#include "utils.h"

void random_init(float * data, int length) {
	for (int i = 0; i < length; i++)
		data[i] = rand()/(float)RAND_MAX;
}

void inc_init(float * data, int length) {
	for (int i = 0; i < length; i++)
		data[i] = i;
}

char* file2string(const char* filename, const char* preamble, size_t* final_length) {
	FILE * file_stream = NULL;
	size_t source_length;

	// open the OpenCL source code file
	if(fopen_s(&file_stream, filename, "rb") != 0) return NULL;

	size_t preamble_length = strlen(preamble);

	// get the length of the source code
	fseek(file_stream, 0, SEEK_END); 
	source_length = ftell(file_stream);
	fseek(file_stream, 0, SEEK_SET); 

	// allocate a buffer for the source code string and read it in
	char* source_str = (char *)malloc(source_length + preamble_length + 1); 
	memcpy(source_str, preamble, preamble_length);
	if (fread((source_str) + preamble_length, source_length, 1, file_stream) != 1) {
		fclose(file_stream);
		free(source_str);
		return 0;
	}

	// close the file and return the total length of the combined (preamble + source) string
	fclose(file_stream);
	if(final_length != 0) 
		*final_length = source_length + preamble_length;
	source_str[source_length + preamble_length] = '\0';

	return source_str;
}

cl_kernel ocl_kernel_build(cl_program program, cl_device_id device, char * kernel_name) {
	cl_int err;
	cl_kernel kernel = clCreateKernel(program, kernel_name, &err);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[2048];
		printf("ERROR: Failed to build program executable: %s!\n", kernel_name);

		clGetProgramBuildInfo(
			program,              // the program object being queried
			device,            // the device for which the OpenCL code was built
			CL_PROGRAM_BUILD_LOG, // specifies that we want the build log
			sizeof(buffer),       // the size of the buffer
			buffer,               // on return, holds the build log
			&len);                // on return, the actual size in bytes of the data returned

		printf("%s\n", buffer);
		exit(1);
	}
	return kernel;
}

cl_mem ocl_create_buffer(cl_context context, cl_mem_flags flags, size_t size, void * host_data) {
	if (host_data != NULL) flags |= CL_MEM_COPY_HOST_PTR;
	cl_int err;
	cl_mem dev_mem = clCreateBuffer(context, flags, size, host_data, &err);
	if (err != CL_SUCCESS) {
		printf("ERROR clCreateBuffer of size %lu: %d\n", size, err);
		exit(err);
	}
	printf("clCreateBuffer of %lu bytes (%f MB)\n", size, size/1024.0f/1024.0f);
	return dev_mem;
}

void ocl_check_error(int err, char * info) {
	if (err != CL_SUCCESS) {
		printf("ERROR %s: %d\n", info, err);
		exit(err);
	}
}

/*void ocl_set_args(cl_kernel kernel, int n, ...) {
	va_list args;
	va_start(args, n);
	for (int i = 0; i < n; i++) {
		void * arg = va_arg(args, void *);
		clSetKernelArg(kernel, i, sizeof(*arg), &arg);
	}
	va_end(args);
}*/