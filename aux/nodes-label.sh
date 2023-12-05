for i in {2..5}
do
	kubectl label nodes node-$i node=node$i
	if [ $i -eq 2 ] 
	then 
		kubectl label nodes node-$i zone=A
	elif [ $i -eq 3 ]
	then
		kubectl label nodes node-$i zone=A
	elif [ $i -eq 4 ]
	then
		kubectl label nodes node-$i zone=B
	else
		kubectl label nodes node-$i zone=B
	fi
done
