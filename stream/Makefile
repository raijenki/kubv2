CC = gcc
CFLAGS = -O2 -mcmodel=large -fopenmp


all: stream_c

stream_c: stream.c
	$(CC) $(CFLAGS) stream.c -o stream_c

clean:
	rm -f stream_f.exe stream_c.exe *.o
