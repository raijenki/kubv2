CC = mpicc
CFLAGS = -O0 -mcmodel=large


all: parint

parint: parint.c
	$(CC) $(CFLAGS) parint.c -o parint

clean:
	rm -f parint *.o
