# pic-workflow
---

This repository is a small workflow for pic. It consists of three parts:
* preparation.py: It reads the inputfiles.txt and transforms them into directories at your persistent volume.
* pic.sh: This is the bash script to run sputniPIC with the provided inputfile as argument
* tracker.py: This keep tracks of the centre of each field (position [63][31]) for every simulation, and stores the maximum absolute value found for each species of pic. It executes in parallel with pic.sh. 
* end exec: For now it is just a dummy that sleeps for a few seconds before exiting execution.  

## How to use:
---
Put the workflow.py into your DAG directory of airflow. You need to configure the preparation.py and tracker.py to the correct persistent volume storage. You might need to rebuild the docker image if you are mounting to anything different than /data/gem in the pods. At the end, tracker.py is supposed to print the array with maximum values.

Limitations:
---
* Inputs are hardcoded into the docker image as we currently do not have discussed a way on how to download / use inputs "from the cloud".
* This workflow assumes that the number of pods running is equal the number of inputs, so it will fail if there's a scheduled pod who has not started yet (i.e., the tracker will stop checking other simulations while waiting for this scheduled pod to start).
