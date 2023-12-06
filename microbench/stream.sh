ssh node-4 "sudo cd kubv2 && git pull && cd kubv2/microbench/stream && make"

reference=219 # 731 * 0.3
min_tmout=15
max_tmout=45
sum_tmout=0

min_slp=20
max_slp=40

while [ $sum_tmout -le $reference ]; do
    sleep $((RANDOM % ($max_slp - $min_slp + 1) + $min_slp))
    timeout=$((RANDOM % ($max_tmout - $min_tmout + 1) + $min_tmout))
    ssh node-4 "cd kubv2/microbench/stream && OMP_NUM_THREADS=4 timeout $timeout ./stream"
    sum_tmout=$((sum_tmout + timeout))
done

echo "Total Timeout: $sum_tmout"
