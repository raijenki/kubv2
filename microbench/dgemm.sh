#ssh node-2 "sudo apt-get install libopenblas-dev -y && cd kubv2/microbench/mt-dgemm && make"

reference=412 # 588 * 0.7
min_tmout=40
max_tmout=70
sum_tmout=0

min_slp=20
max_slp=30

while [ $sum_tmout -le $reference ]; do
    sleep $((RANDOM % ($max_slp - $min_slp + 1) + $min_slp))
    timeout=$((RANDOM % ($max_tmout - $min_tmout + 1) + $min_tmout))
    ssh node-4 "cd kubv2/microbench/mt-dgemm && OMP_NUM_THREADS=4 timeout $timeout ./mt-dgemm 10000 1000"
    sum_tmout=$((sum_tmout + timeout))
done

echo "Total Timeout: $sum_tmout"
