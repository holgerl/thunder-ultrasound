#include <stdlib.h>
#include <stdio.h>
#include "gui.h"
#include "file_io.h"
#include "reconstruction.h"
#include "utils.h"
#include "holger_time.h"
#include <omp.h>

//C:\\Master Holger\\Franks thumb drive\\UL\\Fantom_Baat\\clip11-3dFPA-analog
//C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5
//C:\\Master Holger\\Franks thumb drive\\UL\\Calibration_191109\\ultrasoundSample19
//C:\\Master Holger\\Simple test input\\Simple\\simple_test
//C:\\Master Holger\\Simple test input\\Modified Calibration_191109\\ultrasoundSample19
//C:\\Master Holger\\Franks thumb drive\\UL\\Fantom_Baat\\clip11-3dFPA-analog
//C:\\Master Holger\\Simple test input\\Lines\\lines

int main(int argc, char ** argv) {
	//create_dummy_vol_file(); exit(0);
	holger_time_start(0, "Main");	
	holger_time(0, "OpenCL initialization");
	read_input(
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.mhd",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.pos",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.tim",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.msk",
		//"C:\\Master Holger\\Simple test input\\Lines\\lines.vol",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.vol",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\calibration_files\\M12L.cal"
	);
	holger_time(0, "Read input files");
	#pragma omp parallel num_threads(2)
	{
		int thread_idx = omp_get_thread_num();
		if (thread_idx == 0) {
			printf("Reconstruct thread\n");
			reconstruct();
		} else if (thread_idx == 1) {
			printf("GUI thread\n");
			gui(argc, argv);
		}
	}

	gui(argc, argv);
	holger_time(0, "Reconstruction");
	holger_time_print(0);
	exit(0);
}