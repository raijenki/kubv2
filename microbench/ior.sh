ssh node-4 "cd kubv2/microbench/ior && ./configure && make"

reference=412 # 588 * 0.7
min_tmout=40
max_tmout=70
sum_tmout=0

min_slp=20
max_slp=30

while [ $sum_tmout -le $reference ]; do
    sleep $((RANDOM % ($max_slp - $min_slp + 1) + $min_slp))
    timeout=$((RANDOM % ($max_tmout - $min_tmout + 1) + $min_tmout))
    ssh node-4 "cd kubv2/microbench/ior && timeout $timeout ./mpirun -np 4 ./ior -t 2m -b 32m -s 768 -F -C -e -w -o testfile -i 50 "
    sum_tmout=$((sum_tmout + timeout))
done

echo "Total Timeout: $sum_tmout"
