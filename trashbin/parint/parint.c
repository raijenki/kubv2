# define _XOPEN_SOURCE 600

# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <math.h>
# include <float.h>
# include <string.h>
# include <limits.h>
# include <signal.h>
# include <sys/time.h>
# include "mpi.h"


// Make the scalar coefficient modifiable at compile time.
// The old value of 3.0 cause floating-point overflows after a relatively small
// number of iterations.  The new default of 0.42 allows over 2000 iterations for
// 32-bit IEEE arithmetic and over 18000 iterations for 64-bit IEEE arithmetic.
// The growth in the solution can be eliminated (almost) completely by setting 
// the scalar value to 0.41421445, but this also means that the error checking
// code no longer triggers an error if the code does not actually execute the
// correct number of iterations!
#ifndef SCALAR
#define SCALAR 0.42
#endif

#define NLOOP 32

/*
 *	3) Compile the code with optimization.  Many compilers generate
 *       unreasonably bad code before the optimizer tightens things up.  
 *     If the results are unreasonably good, on the other hand, the
 *       optimizer might be too smart for me!
 *
 *     For a simple single-core version, try compiling with:
 *            cc -O stream.c -o stream
 *     This is known to work on many, many systems....
 *
 *     To use multiple cores, you need to tell the compiler to obey the OpenMP
 *       directives in the code.  This varies by compiler, but a common example is
 *            gcc -O -fopenmp stream.c -o stream_omp
 *       The environment variable OMP_NUM_THREADS allows runtime control of the 
 *         number of threads/cores used when the resulting "stream_omp" program
 *         is executed.
 *
 *     To run with single-precision variables and arithmetic, simply add
 *         -DSTREAM_TYPE=float
 *     to the compile line.
 *     Note that this changes the minimum array sizes required --- see (1) above.
 *
 *     The preprocessor directive "TUNED" does not do much -- it simply causes the 
 *       code to call separate functions to execute each kernel.  Trivial versions
 *       of these functions are provided, but they are *not* tuned -- they just 
 *       provide predefined interfaces to be replaced with tuned code.
 *
 *
 *	4) Optional: Mail the results to mccalpin@cs.virginia.edu
 *	   Be sure to include info that will help me understand:
 *		a) the computer hardware configuration (e.g., processor model, memory type)
 *		b) the compiler name/version and compilation flags
 *      c) any run-time information (such as OMP_NUM_THREADS)
 *		d) all of the output from the test case.
 *
 * Thanks!
 *
 *-----------------------------------------------------------------------*/

# define HLINE "-------------------------------------------------------------\n"

# ifndef MIN
# define MIN(x,y) ((x)<(y)?(x):(y))
# endif
# ifndef MAX
# define MAX(x,y) ((x)>(y)?(x):(y))
# endif

#ifndef STREAM_TYPE
#define STREAM_TYPE double
#endif

//static STREAM_TYPE	a[STREAM_ARRAY_SIZE+OFFSET],
//			b[STREAM_ARRAY_SIZE+OFFSET],
//			c[STREAM_ARRAY_SIZE+OFFSET];

// Some compilers require an extra keyword to recognize the "restrict" qualifier.
STREAM_TYPE * restrict a, * restrict b, * restrict c, * restrict recv_array;

size_t		ARRAY_SIZE, array_elements, array_bytes, array_alignment;

/* Begin of SigTerm handling*/
int backup = 0;
int qq = 0;
volatile sig_atomic_t done = 0;
void term(int signum) {
	printf("Caught SIGTERM!!\n");
	backup = 1;
}
int stream_load(int myrank, int array_length) {
	/*
	FILE *f2 = fopen("./data/time.dat", "r");
	for (int i = 0; i < 4; i++) {
		for(int j = 0; j < qq; j++) {
    		fscanf(f2, "%lf\n", &times[i][j]);
    		// check for error here too
		}
		fscanf(f2, "%c%c%c\n", &placebo[0], &placebo[1], &placebo[2]);
	}
	fclose(f2);*/

	if(myrank==0){
		FILE *f2 = fopen("/data/c.dat", "r");
		if(f2){
			fread( recv_array, sizeof(double) , ARRAY_SIZE, f2);
			fclose(f2);
		}else{
			fprintf(stderr, "Unable to read file c.dat\n");
		}
	}
	MPI_Scatter(recv_array, array_length, MPI_DOUBLE, c, array_length, MPI_DOUBLE, 0, MPI_COMM_WORLD);

}
void stream_write(int k, int myrank, int array_length) {
	
	//Only Rank 0 needs to save k.data because all MPI ranks have the same k
	if(myrank==0){
		FILE *f1 = fopen("/data/k.dat", "w");
		fprintf(f1,"%d",k);
		fclose(f1);
	}

	//each MPI rank has a different file
	char buffer[64];

	/*
	snprintf(buffer, 64, "./data/a_rank%d.dat\0", myrank);
	fprintf(stderr, buffer);
	FILE *f1 = fopen(buffer, "w");
	if(f1) {
		fprintf(f1,"%d",k);
		fclose(f1);
	}else{
		fprintf(stderr, "Unable to create file %s\n", buffer);
	}
	*/
	
	/* Option 1
	snprintf(buffer, 64, "./data/c_rank%d.dat\0", myrank);
	fprintf(stderr, buffer);
	FILE *f2 = fopen(buffer, "wb");
	if(f2){
		fwrite(c , sizeof(double) , array_length, f2 );
		fclose(f2);
	}else{
		fprintf(stderr, "Unable to create file %s\n", buffer);
	}
	*/

	MPI_Gather(c, array_length, MPI_DOUBLE, recv_array, array_length, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	if(myrank==0){
		FILE *f2 = fopen("/data/c.dat", "wb");
		if(f2){
			fwrite(c , sizeof(double) , ARRAY_SIZE, f2 );
			fclose(f2);
		}else{
			fprintf(stderr, "Unable to create file %s\n", buffer);
		}
	}

}
/* Begin of SigTerm handling*/


int
main(int argc, char **argv){
	STREAM_TYPE scalar = SCALAR;
	int NTIMES = 0;
	if(argc != 3) {
		printf("The wrong number of arguments is passed!");
		return 1;
	}else {
		ARRAY_SIZE = atoi(argv[1]);
		NTIMES = atoi(argv[2]);
	}

	//sigterm 
	struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = term;
    sigaction(SIGUSR1, &action, NULL);

	int         rc, numranks, myrank;
	STREAM_TYPE	AvgError[3] = {0.0,0.0,0.0};
	STREAM_TYPE *AvgErrByRank;

    /* --- SETUP --- call MPI_Init() before anything else! --- */

    rc = MPI_Init(NULL, NULL);
    if (rc != MPI_SUCCESS) {
       printf("ERROR: MPI Initialization failed with return code %d\n",rc);
       exit(1);
    }
	// if either of these fail there is something really screwed up!
	MPI_Comm_size(MPI_COMM_WORLD, &numranks);
	MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
	if(myrank==0){
		printf("%d MPI ranks run ARRAY_SIZE=%d of %zu bytes for %d Iterations\n", 
				numranks, ARRAY_SIZE, sizeof(STREAM_TYPE), NTIMES);
	}

    /* --- NEW FEATURE --- distribute requested storage across MPI ranks --- */
	array_elements = ARRAY_SIZE / numranks;		// don't worry about rounding vs truncation
    array_alignment = 64;						// Can be modified -- provides partial support for adjusting relative alignment

	// Dynamically allocate the three arrays using "posix_memalign()"
	// NOTE that the OFFSET parameter is not used in this version of the code!
    array_bytes = array_elements * sizeof(STREAM_TYPE);
    int k = posix_memalign((void **)&a, array_alignment, array_bytes);
    if (k != 0) {
        printf("Rank %d: Allocation of array a failed, return code is %d\n",myrank,k);
		MPI_Abort(MPI_COMM_WORLD, 2);
        exit(1);
    }
    k = posix_memalign((void **)&b, array_alignment, array_bytes);
    if (k != 0) {
        printf("Rank %d: Allocation of array b failed, return code is %d\n",myrank,k);
		MPI_Abort(MPI_COMM_WORLD, 2);
        exit(1);
    }
    k = posix_memalign((void **)&c, array_alignment, array_bytes);
    if (k != 0) {
        printf("Rank %d: Allocation of array c failed, return code is %d\n",myrank,k);
		MPI_Abort(MPI_COMM_WORLD, 2);
        exit(1);
    }
	if(myrank==0) recv_array = (double*) malloc(sizeof(double)*ARRAY_SIZE);

	// Initial informational printouts -- rank 0 handles all the output
	if (myrank == 0) {
		printf(HLINE);
		printf("This system uses %d bytes per array element.\n", sizeof(STREAM_TYPE));

		printf(HLINE);
		printf("Each kernel will be executed %d times.\n", NTIMES);
		printf("The SCALAR value used for this run is %f\n",SCALAR);

#ifdef _OPENMP
		printf(HLINE);
#pragma omp parallel 
		{
#pragma omp master
		{
			k = omp_get_num_threads();
			printf ("Number of Threads requested for each MPI rank = %i\n",k);
			}
		}
#endif

#ifdef _OPENMP
		k = 0;
#pragma omp parallel
#pragma omp atomic 
			k++;
		printf ("Number of Threads counted for rank 0 = %i\n",k);
#endif

	}

    /* --- SETUP --- initialize arrays and estimate precision of timer --- */
	// Check if k.dat exists, if not do nothing
	k = 0;
	FILE *fp = fopen("/data/k.dat", "r");
	if (fp) { // file exists, load times
		fscanf(fp,"%d", &qq);
		fclose(fp);
		stream_load(myrank, array_elements);
		k = k + qq;
		backup = 2; // Ensure this is executed only once
		printf("Rank %d RESUMING from k=%d !!\n", myrank,k);
	}else{
		#pragma omp parallel for
		for (int j=0; j<array_elements; j++) {
			a[j] = 1.0;
			b[j] = 2.0;
			c[j] = 0.0;
		}
	}
		
    
    /*	--- MAIN LOOP --- repeat test cases NTIMES times --- */
	double timing_prev = MPI_Wtime();
    for (; k<NTIMES; k++)
	{
		if(backup == 1) {
			stream_write(k, myrank, array_elements);
			printf("Rank %d finished iteration %d , checkpointing is done, stop now\n", myrank, (k-1));
			MPI_Finalize();
			return 0;
		}

		// kernel 1: Copy
		//MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel for
		for (int j=0; j<array_elements; j++){
			double temp = 0.0;
			#pragma GCC unroll 16
            for (int ll = 0; ll < NLOOP; ll++)
				temp += SCALAR * a[j] + b[j];
			c[j] = temp;
		}
		//MPI_Barrier(MPI_COMM_WORLD);
		if(k%10==0 && myrank==0) {
			fprintf(stderr, "%d out of %d : Time (%.3f sec)\n", k, NTIMES, MPI_Wtime()-timing_prev);
			timing_prev = MPI_Wtime();
		}

	}
    /*	--- SUMMARY --- */

	// Because of the MPI_Barrier() calls, the timings from any thread are equally valid. 
    // The best estimate of the maximum performance is the minimum of the "outside the barrier"
    // timings across all the MPI ranks.

	// Gather all timing data to MPI rank 0
	MPI_Gather(c, array_elements, MPI_DOUBLE, recv_array, array_elements, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	free(a);
	free(b);
	free(c);	
	if(myrank==0) free(recv_array); 

    MPI_Finalize();
	return(0);
}




