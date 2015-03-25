#include "global_const.h"

#if MODE != CONSOLE_MODE

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#ifdef __APPLE__
  #include <OpenCL/OpenCL.h>
  #include <GLUT/glut.h>
#else
	#include <GL/glut.h>
  #include <CL/cl.h>
#endif //__APPLE__
#include "ray_casting.h"
#include "holger_time.h"

/*cl_float4 cl_float4::operator[](int i) {
   return s[i];
}*/

#define max(x1,x2) ((x1) > (x2) ? (x1):(x2))
#define min(x1,x2) ((x1) < (x2) ? (x1):(x2))

#define win_width (bscan_w + 10 + max(max(volume_w, volume_n), volume_n))
#define win_height max(bscan_h, volume_h+volume_h+volume_w+20)

extern unsigned char * bscans;
extern unsigned char * mask;
extern unsigned char * volume;
extern int bscan_w, bscan_h, bscan_n;

extern cl_mem dev_volume;

int bscan_index = 0;
int volume_index;
unsigned char * volume_slice_zy;
unsigned char * volume_slice_zx;
int bitmap_w = 800;
int bitmap_h = 600;
unsigned char * bitmap = (unsigned char *) malloc(sizeof(unsigned char)*bitmap_w*bitmap_h);
enum gui_mode_type {INPUT_OUTPUT, MPR_SLICES, RAY_CASTING};
gui_mode_type gui_mode = INPUT_OUTPUT;
int mpr_win_w = bitmap_w;
int mpr_win_h = bitmap_h;
float volume_index_float;
int mouse_rot_x, mouse_rot_y;
GLuint textures[3];
int volume_w = VOL_W;
int volume_h = VOL_H;
int volume_n = VOL_N;

void (*cleanup)(void);

void build_slices() {
	int foo = volume_index_float*volume_w;
	for (int y = 0; y < volume_h; y++)
		for (int x = 0; x < volume_n; x++)
			volume_slice_zy[x + y*volume_n] = volume[foo + y*volume_w + x*volume_w*volume_h];

	foo = volume_index_float*volume_h;
	for (int y = 0; y < volume_w; y++)
		for (int x = 0; x < volume_n; x++)
			volume_slice_zx[x + y*volume_n] = volume[y + foo*volume_w + x*volume_w*volume_h];
}

void reset_gl_matrices() {
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void bind_textures() {
	glBindTexture(GL_TEXTURE_2D, textures[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, volume_n, volume_w, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, volume_slice_zx);
	glBindTexture(GL_TEXTURE_2D, textures[1]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, volume_n, volume_h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, volume_slice_zy);
	glBindTexture(GL_TEXTURE_2D, textures[2]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, volume_w, volume_h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &volume[(int)(volume_n*volume_index_float*volume_w*volume_h)]);
}

void display(void) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	if (gui_mode == INPUT_OUTPUT) {
		glDisable(GL_TEXTURE_2D);
		reset_gl_matrices();
		glOrtho(0, win_width, 0, win_height, -1, 1);
		glPixelZoom(1.0, -1.0);

		build_slices();
		
		glRasterPos2i(0, win_height);
			glDrawPixels(bscan_w, bscan_h, GL_LUMINANCE, GL_UNSIGNED_BYTE, &bscans[bscan_index*bscan_w*bscan_h]);
		glRasterPos2i(win_width-volume_w, win_height);
			glDrawPixels(volume_w, volume_h, GL_LUMINANCE, GL_UNSIGNED_BYTE, &volume[(int)(volume_n*volume_index_float*volume_w*volume_h)]);
		glRasterPos2i(win_width-volume_n, win_height-volume_h-10);
			glDrawPixels(volume_n, volume_h, GL_LUMINANCE, GL_UNSIGNED_BYTE, volume_slice_zy);
		glRasterPos2i(win_width-volume_n, win_height-volume_h-volume_h-20);
			glDrawPixels(volume_n, volume_w, GL_LUMINANCE, GL_UNSIGNED_BYTE, volume_slice_zx);
	}

	if (gui_mode == RAY_CASTING) {
		glDisable(GL_TEXTURE_2D);
		reset_gl_matrices();
		glOrtho(0, win_width, 0, win_height, -1, 1);
		glPixelZoom(1.0, -1.0);

		cl_float4 camera_pos = {volume_w*2, volume_h*2, volume_n*2};
		cl_float4 camera_lookat = {volume_w/2, volume_h/2, volume_n/2};

		float rot_angle	= -mouse_rot_x/180.0f*3.14/5.0f;
		float x_pos = camera_pos.s[0]-camera_lookat.s[0];
		float y_pos = camera_pos.s[2]-camera_lookat.s[2];
		camera_pos.s[0] = cos(rot_angle)*x_pos - sin(rot_angle)*y_pos + camera_lookat.s[0];
		camera_pos.s[2] = sin(rot_angle)*x_pos + cos(rot_angle)*y_pos + camera_lookat.s[2];

		// Freehand funky mode:
		rot_angle = -mouse_rot_y/180.0f*3.14/5.0f;
		x_pos = camera_pos.s[1]-camera_lookat.s[1];
		y_pos = camera_pos.s[2]-camera_lookat.s[2];
		camera_pos.s[1] = cos(rot_angle)*x_pos - sin(rot_angle)*y_pos + camera_lookat.s[1];
		camera_pos.s[2] = sin(rot_angle)*x_pos + cos(rot_angle)*y_pos + camera_lookat.s[2];
		
		ray_cast(camera_pos, camera_lookat);

		glRasterPos2i(win_width/2-bitmap_w/2, win_height/2+bitmap_h/2);
			glDrawPixels(bitmap_w, bitmap_h, GL_LUMINANCE, GL_UNSIGNED_BYTE, bitmap);
	}

	if (gui_mode == MPR_SLICES) {
		glEnable(GL_TEXTURE_2D);
		reset_gl_matrices();
		gluPerspective(30.0f, mpr_win_w/(float)mpr_win_h, 0.1f, 10000.0f);
		
		build_slices();
		bind_textures();

		gluLookAt(volume_w*2, volume_h*2, volume_n*2, 0, 0, 0, 0, 1, 0);
		glTranslatef(volume_w/2, volume_h/2, volume_n/2);
		glRotatef(mouse_rot_x/5.0f, 0, 1, 0);
		glRotatef(mouse_rot_y/5.0f, 1, 0, -1);
		glTranslatef(-volume_w/2, -volume_h/2, -volume_n/2);

		// top face
		glBindTexture(GL_TEXTURE_2D, textures[0]);
		glTranslatef(0,volume_h*volume_index_float,0);
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex3f( 0, 0,0);
			glTexCoord2f(1.0f, 0.0f); glVertex3f(volume_w, 0,0);
			glTexCoord2f(1.0f, 1.0f); glVertex3f(volume_w, 0, volume_n);
			glTexCoord2f(0.0f, 1.0f); glVertex3f( 0, 0, volume_n);
		glEnd();
		glTranslatef(0,-volume_h*volume_index_float,0);

		// front face
		glBindTexture(GL_TEXTURE_2D, textures[1]);
		glTranslatef(0,0,volume_n*volume_index_float);
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex3f( 0, 0, 0);
			glTexCoord2f(1.0f, 0.0f); glVertex3f(volume_w, 0, 0);
			glTexCoord2f(1.0f, 1.0f); glVertex3f(volume_w,volume_h, 0);
			glTexCoord2f(0.0f, 1.0f); glVertex3f(0,volume_h, 0);
		glEnd();
		glTranslatef(0,0,-volume_n*volume_index_float);

		// left face
		glBindTexture(GL_TEXTURE_2D, textures[2]);
		glTranslatef(volume_w*volume_index_float,0,0);
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex3f(0, 0, 0);
			glTexCoord2f(0.0f, 1.0f); glVertex3f(0, volume_h,0);
			glTexCoord2f(1.0f, 1.0f); glVertex3f(0,volume_h,volume_n);
			glTexCoord2f(1.0f, 0.0f); glVertex3f(0,0, volume_n);
		glEnd();
	}

	glutSwapBuffers();
}

void idle(void) {
	volume_index_float = volume_index/(float)max(volume_w, max(volume_h, volume_n));
	mouse_rot_x -= 3;
	display();
}

void reshape(int w, int h) {
	glViewport(0, 0, win_width, win_height);
}

void key(unsigned char key, int x, int y) {
	if (key == '\033' || key == 'q') {
			release_ray_casting();
			cleanup();
			exit(0);
	}

	if (key == 'w') 
			if (volume_index < max(max(volume_n-1, volume_h-1), volume_w-1)) volume_index++;
	if (key == 's')
		if (volume_index > 0) volume_index--;
	if (key == 'd')
		if (volume_index < max(max(volume_n-10, volume_h-10), volume_w-10)) volume_index += 10;
	if (key == 'a')
		if (volume_index >= 10) volume_index -= 10;

	if (key == 'r')
		if (bscan_index < bscan_n-1) bscan_index += 1;
	if (key == 'f')
		if (bscan_index >= 1) bscan_index -= 1;

	if (key == '1')
		gui_mode = INPUT_OUTPUT;
	if (key == '2')
		gui_mode = MPR_SLICES;
	if (key == '3')
		gui_mode = RAY_CASTING;
	if (key == '1' || key == '2' || key == '3') {
		volume_index = max(volume_w, max(volume_h, volume_n))/2;
		mouse_rot_x = 0;
		mouse_rot_y = 0;
	}

  glutPostRedisplay();
}

void mouse_press(int button, int state, int x, int y) {
  if (state == GLUT_DOWN) {
    
  }
  glutPostRedisplay();
}

void mouse_movement(bool pressed, int x, int y) {
	static int mouse_x = 0;
	static int mouse_y = 0;
	if (pressed) mouse_rot_x += x - mouse_x;
	if (pressed) mouse_rot_y += y - mouse_y;
  mouse_x = x;
	mouse_y = y;
}

void mouse_active(int x, int y) {
  mouse_movement(true, x, y);
}

void mouse_passive(int x, int y) {
  mouse_movement(false, x, y);
}

void gui(int argc, char **argv, void (*cleanup_function)(void)) {
	volume_slice_zy = (unsigned char *) malloc(sizeof(unsigned char)*volume_n*volume_h);
	volume_slice_zx = (unsigned char *) malloc(sizeof(unsigned char)*volume_n*volume_w);

	cleanup = cleanup_function;

  glutInit(&argc, argv);
  glutInitWindowSize(win_width, win_height);
  glutInitDisplayMode(GLUT_DEPTH | GLUT_RGBA | GLUT_DOUBLE);
	glutCreateWindow("Thunder - keys: w, s, a, d, r, f, 1, 2, 3, q, esc");
	glEnable(GL_DEPTH_TEST);
	
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glClearColor(46/255.0f, 76/255.0f, 39/255.0f, 1.0);

	volume_index = max(volume_w, max(volume_h, volume_n))/2; // Start in middle of volume

	glGenTextures(3, textures);
	glBindTexture(GL_TEXTURE_2D, textures[0]);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, textures[1]);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, textures[2]);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); 
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

	init_ray_casting(dev_volume, volume_w, volume_h, volume_n, bitmap, bitmap_w, bitmap_h);

	glutDisplayFunc(display);
	glutIdleFunc(idle);
  glutKeyboardFunc(key);
  glutReshapeFunc(reshape);
  glutMouseFunc(mouse_press);
	glutMotionFunc(mouse_active);
	glutPassiveMotionFunc(mouse_passive);
  glutMainLoop();
}

void profiler_gui() {
	holger_time_start(2, "Profiler GUI");

	init_ray_casting(dev_volume, volume_w, volume_h, volume_n, bitmap, bitmap_w, bitmap_h);

	holger_time(2, "Ray casting initializations");

	for (int i = 0; i < PROFILER_RAYCASTS; i++) {
		cl_float4 camera_pos = {volume_w*2, volume_h*2, volume_n*2};
		cl_float4 camera_lookat = {volume_w/2, volume_h/2, volume_n/2};
		ray_cast(camera_pos, camera_lookat);
	}

	holger_time(2, "Ray casts");
	
	release_ray_casting();

	holger_time(2, "Ray casting releases");

	holger_time_print(2);
}

#endif