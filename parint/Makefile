CC = mpicc
CFLAGS = -O0 -mcmodel=large


all: stream_mpi

stream_c: stream_mpi.c
	$(CC) $(CFLAGS) stream_mpi.c -o stream_mpi

clean:
	rm -f stream_mpi.exe stream_mpi.exe *.o
