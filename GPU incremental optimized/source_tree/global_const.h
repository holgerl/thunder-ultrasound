#ifndef _GLOBAL_CONST
#define _GLOBAL_CONST

//#define PRINT
#define PRINT_RES0 3
#define PRINT_RES1 30
#define PRINT_RES2 6000

#define BSCAN_WINDOW 4 // must be >= 4 if PT
#define PT_OR_DW 0 // 0=PT, 1=DW

#define COMPOUND_AVG 0
#define COMPOUND_MAX 1
#define COMPOUND_IFEMPTY 2
#define COMPOUND_OVERWRITE 3

#define COMPOUND_METHOD COMPOUND_OVERWRITE

#endif