CC = mpicc
CFLAGS = -O1 -fno-PIC -mcmodel=large


all: parint

parint: parint.c
	$(CC) $(CFLAGS) parint.c -o parint

clean:
	rm -f parint *.o
