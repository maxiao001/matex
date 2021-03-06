/* ---------------------------------------------------------------- */
/* (C)Copyright IBM Corp.  2007, 2008                               */
/* ---------------------------------------------------------------- */
/**
 * \file ad_bg_tuning.c
 * \brief Defines ad_bg performance tuning
 */

/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 2008 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

/*---------------------------------------------------------------------
 * ad_bg_tuning.c
 *
 * defines global variables and functions for performance tuning and 
 * functional debugging.
 *---------------------------------------------------------------------*/

#include "ad_bg_tuning.h"
#include "mpi.h"

#if !defined(PVFS2_SUPER_MAGIC)
  #define PVFS2_SUPER_MAGIC (0x20030528)
#endif


int 	bgmpio_timing;
int 	bgmpio_timing2;
int 	bgmpio_comm;
int 	bgmpio_tunegather;
int 	bgmpio_tuneblocking;
long    bglocklessmpio_f_type;
int     bgmpio_bg_nagg_pset;

double	bgmpio_prof_cw    [BGMPIO_CIO_LAST];
double	bgmpio_prof_cr    [BGMPIO_CIO_LAST];

/* set internal variables for tuning environment variables */
/** \page mpiio_vars MPIIO Configuration
  \section env_sec Environment Variables
 * - BGMPIO_COMM - Define how data is exchanged on collective
 *   reads and writes.  Possible values:
 *   - 0 - Use MPI_Alltoallv.
 *   - 1 - Use MPI_Isend/MPI_Irecv.
 *   - Default is 0.
 *
 * - BGMPIO_TIMING - collect timing breakdown for MPI I/O collective calls.
 *   Must also compile the library with BG_PROFILE defined. Possible values:
 *   - 0 - Do not collect/report timing.
 *   - 1 - Collect/report timing.
 *   - Default is 0.
 *
 * - BGMPIO_TIMING2 - collect additional averages for MPI I/O collective calls.
 *   Must also compile the library with BG_PROFILE defined. Possible values:
 *   - 0 - Do not collect/report averages.
 *   - 1 - Collect/report averages.
 *   - Default is 0.
 *
 * - BGMPIO_TUNEGATHER - Tune how starting and ending offsets are communicated
 *   for aggregator collective i/o.  Possible values:
 *   - 0 - Use two MPI_Allgather's to collect starting and ending offsets.
 *   - 1 - Use MPI_Allreduce(MPI_MAX) to collect starting and ending offsets.
 *   - Default is 1.
 *
 * - BGMPIO_TUNEBLOCKING - Tune how aggregate file domains are 
 *   calculated (block size).  Possible values:
 *   - 0 - Evenly calculate file domains across aggregators.  Also use 
 *   MPI_Isend/MPI_Irecv to exchange domain information.
 *   - 1 - Align file domains with the underlying file system's block size.  Also use 
 *   MPI_Alltoallv to exchange domain information.
 *   - Default is 1.
 *
 * - BGLOCKLESSMPIO_F_TYPE - Specify a filesystem type that should run
 *   the ad_bglockless driver.   NOTE: Using romio prefixes (such as
 *   "bg:" or "bglockless:") on a file name will override this environment
 *   variable.  Possible values:
 *   - 0xnnnnnnnn - Any valid file system type (or "magic number") from
 *                  statfs() field f_type.
 *   - The default is 0x20030528 (PVFS2_SUPER_MAGIC)
 *
 * - BGMPIO_NAGG_PSET - Specify a ratio of "I/O aggregators" to use for each
 *   compute group (compute nodes + i/o nodes).    Possible values:
 *   - any integer
 *   - Default is 8
 *
*/
void ad_bg_get_env_vars() {
    char *x, *dummy;

    bgmpio_comm   = 0;
	x = getenv( "BGMPIO_COMM"         ); 
	if (x) bgmpio_comm         = atoi(x);
    bgmpio_timing = 0;
	x = getenv( "BGMPIO_TIMING"       ); 
	if (x) bgmpio_timing       = atoi(x);
    bgmpio_timing2 = 0;
	x = getenv( "BGMPIO_TIMING2"      ); 
	if (x) bgmpio_timing2      = atoi(x);
    bgmpio_tunegather = 1;
	x = getenv( "BGMPIO_TUNEGATHER"   ); 
	if (x) bgmpio_tunegather   = atoi(x);
    bgmpio_tuneblocking = 1;
    x = getenv( "BGMPIO_TUNEBLOCKING" ); 
    if (x) bgmpio_tuneblocking = atoi(x);
    bglocklessmpio_f_type = PVFS2_SUPER_MAGIC;
    x = getenv( "BGLOCKLESSMPIO_F_TYPE" ); 
    if (x) bglocklessmpio_f_type = strtol(x,&dummy,0);
    DBG_FPRINTF(stderr,"BGLOCKLESSMPIO_F_TYPE=%ld/%#lX\n",
            bglocklessmpio_f_type,bglocklessmpio_f_type);
    /* note: this value will be 'sanity checked' in ADIOI_BG_persInfo_init(),
     * when we know a bit more about what "largest possible value" and
     * "smallest possible value" should be */
    bgmpio_bg_nagg_pset = ADIOI_BG_NAGG_PSET_DFLT;
    x = getenv("BGMPIO_NAGG_PSET");
    if (x) bgmpio_bg_nagg_pset = atoi(x);
}

/* report timing breakdown for MPI I/O collective call */
void ad_bg_wr_timing_report( int rw, ADIO_File fd, int myrank, int nprocs )
{
    int i;

    if (bgmpio_timing) {

	double *bgmpio_prof_org = bgmpio_prof_cr;
	if (rw) bgmpio_prof_org = bgmpio_prof_cw;

	double bgmpio_prof_avg[ BGMPIO_CIO_LAST ];
	double bgmpio_prof_max[ BGMPIO_CIO_LAST ];
	
	MPI_Reduce( bgmpio_prof_org, bgmpio_prof_avg, BGMPIO_CIO_LAST, MPI_DOUBLE, MPI_SUM, 0, fd->comm );
	MPI_Reduce( bgmpio_prof_org, bgmpio_prof_max, BGMPIO_CIO_LAST, MPI_DOUBLE, MPI_MAX, 0, fd->comm );

	if (myrank == 0) {

	    for (i=0; i<BGMPIO_CIO_LAST; i++) bgmpio_prof_avg[i] /= nprocs;

	    if (bgmpio_timing2) {
		bgmpio_prof_avg[ BGMPIO_CIO_B_POSI_RW  ] = bgmpio_prof_avg[ BGMPIO_CIO_DATA_SIZE ] * nprocs / 
							     bgmpio_prof_max[ BGMPIO_CIO_T_POSI_RW  ];
		bgmpio_prof_avg[ BGMPIO_CIO_B_MPIO_RW  ] = bgmpio_prof_avg[ BGMPIO_CIO_DATA_SIZE ] * nprocs / 
							     bgmpio_prof_max[ BGMPIO_CIO_T_MPIO_RW  ];
	    } else {

		bgmpio_prof_avg[ BGMPIO_CIO_B_POSI_RW  ] = 0;
		bgmpio_prof_avg[ BGMPIO_CIO_B_MPIO_RW  ] = 0;
	    }

		bgmpio_prof_avg[ BGMPIO_CIO_B_MPIO_CRW ] = bgmpio_prof_avg[ BGMPIO_CIO_DATA_SIZE ] * nprocs / 
							     bgmpio_prof_max[ BGMPIO_CIO_T_MPIO_CRW ];

	    printf("\tTIMING-1 %1s , ", (rw ? "W" : "R") );
	    printf(    "SZ: %12.4f , ", bgmpio_prof_avg[ BGMPIO_CIO_DATA_SIZE ] * nprocs );
	    printf(  "SK-a: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_SEEK ]     );
	    printf(  "SK-m: %10.3f , ", bgmpio_prof_max[ BGMPIO_CIO_T_SEEK ]     );
	    printf(  "LC-a: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_LCOMP ]    );
	    printf(  "GA-m: %10.3f , ", bgmpio_prof_max[ BGMPIO_CIO_T_GATHER ]   );
	    printf(  "AN-a: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_PATANA ]   );
	    printf(  "FD-a: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_FD_PART ]  );
	    printf(  "MY-a: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_MYREQ ]    );
	    printf(  "OT-m: %10.3f , ", bgmpio_prof_max[ BGMPIO_CIO_T_OTHREQ ]   );
	    printf(  "EX-m: %10.3f , ", bgmpio_prof_max[ BGMPIO_CIO_T_DEXCH ]    );
	    printf("\tTIMING-2 %1s , ", (rw ? "W" : "R") );
	    printf( "PXT-m: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_POSI_RW ]  );
	    printf( "MPT-m: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_MPIO_RW ]  );
	    printf("MPTC-m: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_T_MPIO_CRW ] );
	    printf(   "PXB: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_B_POSI_RW ]  );
	    printf(   "MPB: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_B_MPIO_RW ]  );
	    printf(  "MPBC: %10.3f , ", bgmpio_prof_avg[ BGMPIO_CIO_B_MPIO_CRW ] );
	}
    }

}
