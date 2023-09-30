
CC=gcc
CFLAGS=-ffast-math -ftree-vectorizer-verbose=3 -O3 -fopenmp -DUSE_CBLAS
LDFLAGS=-L/usr/lib -lopenblas

mt-dgemm: mt-dgemm.c
	$(CC) $(CFLAGS) -o mt-dgemm mt-dgemm.c $(LDFLAGS)

clean:
	rm mt-dgemm *.o

