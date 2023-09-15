#!/bin/bash

#values=(8 16 32 48 64)
#values=(16 32 64)
#scenarios=(90 150 210 165 270 380 320 530 740) 
#scenarios=(34 58 80 45 75 105 113 190 262)
values=(32)
scenarios=(380)
rm -f /home/daniel/k3dvol/*
for val in "${values[@]}"
do
	sed -i "s/#define NLOOP [0-9]\+/#define NLOOP $val/" stream_mpi.c 
	git add .
	git commit -m "auto update nloop"
	git push
	docker build . --tag=raijenki/mpik8s:smpi
	docker push raijenki/mpik8s:smpi
	for scen in "${scenarios[@]}"
	do
	if [ $val -eq 16 ] && ([ $scen -eq 90 ] || [ $scen -eq 150 ] || [ $scen -eq 210 ])
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i - Base $scen" >> parint_2to6ranks_16.txt 
		kubectl create -f smpi.yaml
		sleep "$scen"
		kubectl create -f scheduler.yaml
		sleep 250
		kubectl describe job.batch.volcano.sh >> parint_2to6ranks_16.txt 
		kubectl delete -f smpi.yaml -f scheduler.yaml
		sleep 10
		rm -f /home/daniel/k3dvol/*
		echo "FINISHED" >> parint_2to6ranks_16.txt
		done
	fi
	if [ $val -eq 32 ] && ([ $scen -eq 165 ] || [ $scen -eq 270 ] || [ $scen -eq 380 ])
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i - Base $scen" >> parint_2to6ranks_32.txt 
		kubectl create -f smpi.yaml
		sleep "$scen"
		kubectl create -f scheduler.yaml
		sleep 350
		kubectl describe job.batch.volcano.sh >> parint_2to6ranks_32.txt 
		kubectl delete -f smpi.yaml -f scheduler.yaml
		rm -f /home/daniel/k3dvol/*
		sleep 10
		echo "FINISHED" >> parint_2to6ranks_32.txt
		done
	fi

	if [ $val -eq 64 ] && ([ $scen -eq 320 ] || [ $scen -eq 530 ] || [ $scen -eq 740 ])
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i - Base $scen" >> parint_2to6ranks_64.txt 
		kubectl create -f smpi.yaml
		sleep "$scen"
		kubectl create -f scheduler.yaml
		sleep 600
		kubectl describe job.batch.volcano.sh >> parint_2to6ranks_64.txt 
		kubectl delete -f smpi.yaml -f scheduler.yaml
		rm -f /home/daniel/k3dvol/*
		sleep 10
		echo "FINISHED" >> parint_2to6ranks_64.txt
		done
	fi
	done
done
