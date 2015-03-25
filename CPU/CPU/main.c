#include <stdlib.h>
#include <stdio.h>
#include "gui.h"
#include "file_io.h"
#include "reconstruction.h"
#include "utils.h"
#include "holger_time.h"

//C:\\Master Holger\\Franks thumb drive\\UL\\Fantom_Baat\\clip11-3dFPA-analog
//C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5
//C:\\Master Holger\\Franks thumb drive\\UL\\Calibration_191109\\ultrasoundSample19
//C:\\Master Holger\\Simple test input\\Simple\\simple_test
//C:\\Master Holger\\Simple test input\\Small size\\small
//C:\\Master Holger\\Simple test input\\Modified Calibration_191109\\ultrasoundSample19

int main(int argc, char ** argv) {
	holger_time_start(0, "Main procedure");
	read_input(
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.mhd",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.pos",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.tim",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.msk",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\SpineData\\ultrasoundSample5.vol",
		"C:\\Master Holger\\Franks thumb drive\\UL\\Nevro_Spine\\calibration_files\\M12L.cal"
		);
	holger_time(0, "Read input files");
	reconstruct_pnn();
	//reconstruct_vnn();
	holger_time(0, "Reconstruction");
	holger_time_print(0);
	gui(argc, argv);
}