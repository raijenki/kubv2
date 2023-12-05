#!/bin/bash

#values=(8 16 32 48 64)
values=(16 64)

for val in "${values[@]}"
do
	sed -i "s/#define NLOOP [0-9]\+/#define NLOOP $val/" stream_mpi.c 
	git add .
	git commit -m "auto update nloop"
	git push
	docker build . --tag=raijenki/mpik8s:smpi
	docker push raijenki/mpik8s:smpi
	if [ $val -eq 16 ]
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i" >> parint_2ranks.txt 
		kubectl create -f smpi.yaml
		sleep 400
		kubectl describe job.batch.volcano.sh >> parint_2ranks.txt 
		kubectl delete -f smpi.yaml
		sleep 10
		echo "FINISHED" >> parint_2ranks.txt
		done
	fi
	if [ $val -eq 64 ]
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i" >> parint_2ranks.txt 
		kubectl create -f smpi.yaml
		sleep 1200
		kubectl describe job.batch.volcano.sh >> parint_2ranks.txt 
		kubectl delete -f smpi.yaml
		sleep 10
		echo "FINISHED" >> parint_2ranks.txt
		done
	fi
done
