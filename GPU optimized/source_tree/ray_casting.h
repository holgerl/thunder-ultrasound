void init_ray_casting(cl_mem dev_volume, int volume_w, int volume_h, int volume_n, unsigned char * bitmap, int bitmap_w, int bitmap_h);
void ray_cast(cl_float4 camera_pos, cl_float4 camera_lookat);

void release_ray_casting();