#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mpi.h"

int main(void) {
    // Initialize MPI
    int rank, size, k;
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Create a string that mix the name "benchmark" with the rank number
    char filename[20];
    sprintf(filename, "benchmark%d.txt", rank);    
    FILE *fp;
    
    if (access(filename, F_OK) == -1) {
        fp = fopen(filename, "w");
        fprintf(fp, "%d\n", 0);
    } else {
        fp = fopen(filename, "r");
        fscanf(fp, "%d", &k);
    }

    // Get the time before the loop
    double start = MPI_Wtime();

    // Loop 600 times and wait 1 second between each loop
    for (int i = k; i <= 20; i++) {
        sleep(1);
        // Every 10 iterations, write "i" into the file (always overwriting the previous value)
        if (i % 2 == 0) {
            fp = fopen(filename, "w");
            fprintf(fp, "%d\n", i);
        }
    }
    
    fclose(fp);
    MPI_Barrier(MPI_COMM_WORLD);

    double end = MPI_Wtime();
    // If rank 0, read all the benchmark files and sum the values  
    int sum;
    if (rank == 0) {
        sum = 0;
        for (int i = 0; i < size; i++) {
            sprintf(filename, "benchmark%d.txt", i);
            fp = fopen(filename, "r");
            fscanf(fp, "%d", &k);
            sum += k;
        }
    printf("Sum: %d\n", sum);
    printf("Time: %f\n", end - start);
    fclose(fp);
    }

    MPI_Finalize();
    return 0;
}