
// TODO: Does not take frequencies into account

#define bscan_frequency 43
#define pos_frequency 52

extern int bscan_w, bscan_h, pos_n, bscan_n;
extern float * pos_timetags, * pos_matrices, * bscan_timetags;
extern unsigned char * bscans;

int simulator_bscan_counter = -1;
int simulator_pos_counter = -1;

void get_last_bscan(float * timetag, unsigned char * * bscan) {
	*timetag = bscan_timetags[simulator_bscan_counter];
	*bscan = &bscans[simulator_bscan_counter*bscan_w*bscan_h];
}

void get_last_pos_matrix(float * timetag, float * * pos_matrix) {
	*timetag = pos_timetags[simulator_pos_counter];
	*pos_matrix = &pos_matrices[simulator_pos_counter*12];
}

#define inc 1

bool poll_bscan() {
		simulator_bscan_counter += inc;
		return true;
}

bool poll_pos_matrix() {
		simulator_pos_counter += inc;
		return true;
}

bool end_of_data() {
	return simulator_bscan_counter >= bscan_n || simulator_pos_counter >= pos_n;
}