#!/bin/bash

#values=(8 16 32 48 64)
values=(16 32 64)
#scenarios=(90 150 210 165 270 380 320 530 740) 
scenarios=(44 62 80 45 75 105 113 190 262)
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
	if [ $val -eq 16 ] && ([ $scen -eq 44 ] || [ $scen -eq 62 ] || [ $scen -eq 80 ])
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i - Base $scen" >> parint_4to6ranks_16.txt 
		kubectl create -f smpi.yaml
		sleep "$scen"
		kubectl create -f scheduler.yaml
		sleep 250
		kubectl describe job.batch.volcano.sh >> parint_4to6ranks_16.txt 
		kubectl delete -f smpi.yaml -f scheduler.yaml
		sleep 10
		rm -f /home/daniel/k3dvol/*
		echo "FINISHED" >> parint_4to6ranks_16.txt
		done
	fi
	if [ $val -eq 32 ] && ([ $scen -eq 45 ] || [ $scen -eq 75 ] || [ $scen -eq 105 ])
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i - Base $scen" >> parint_4to6ranks_32.txt 
		kubectl create -f smpi.yaml
		sleep "$scen"
		kubectl create -f scheduler.yaml
		sleep 350
		kubectl describe job.batch.volcano.sh >> parint_4to6ranks_32.txt 
		kubectl delete -f smpi.yaml -f scheduler.yaml
		rm -f /home/daniel/k3dvol/*
		sleep 10
		echo "FINISHED" >> parint_4to6ranks_32.txt
		done
	fi

	if [ $val -eq 64 ] && ([ $scen -eq 113 ] || [ $scen -eq 190 ] || [ $scen -eq 262 ])
	then
		for i in 1 2 3
		do
		echo "STARTING $val - Trial $i - Base $scen" >> parint_4to6ranks_64.txt 
		kubectl create -f smpi.yaml
		sleep "$scen"
		kubectl create -f scheduler.yaml
		sleep 400
		kubectl describe job.batch.volcano.sh >> parint_4to6ranks_64.txt 
		kubectl delete -f smpi.yaml -f scheduler.yaml
		rm -f /home/daniel/k3dvol/*
		sleep 10
		echo "FINISHED" >> parint_4to6ranks_64.txt
		done
	fi
	done
done
