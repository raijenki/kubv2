# This is the testing environment for the HPC scaler 
# We first create four replicasets of the busy box just to consume most of the resources
kubectl create -f busybox-replicaset.yaml
# Wait the creation of all the busyboxes
sleep 120
CORES=14
# Every timestep we define a number of replicas that is lower than the previous one
# We get a 0 or 1 randomly and reduce a number of cores based on this
for i in {1..15}
do
	# This is to ensure that we don't get any negative number for scaling
	if [ $CORES -le 2 ]
	then
		kubectl delete -f busybox-replicaset.yaml
		break 1
	fi
	RANDOM_NUMBER=$((RANDOM%2))
	if [ $RANDOM_NUMBER -eq 1 ]
	then
		CORES="$((CORES - 1))"
	fi
        if [ $RANDOM_NUMBER -eq 0 ]
        then
                CORES="$((CORES - 2))"
        fi
	kubectl scale --replicas=$CORES -f busybox-replicaset.yaml
	SLEEP_TIME=$(shuf -i 40-120 -n 1)
        echo "Scaling to $CORES cores and sleeping for $SLEEP_TIME seconds."
	sleep $SLEEP_TIME
done
echo "Leaving loop..."
