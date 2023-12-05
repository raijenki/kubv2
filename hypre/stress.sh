time mpirun --cpu-set 0-2 --bind-to core -np 3 ./ex13 -n 3500 &
sleep 385
numactl -C0-2 ./stream/stream_c.exe &
