kubectl delete -f gromacs.yaml
git add .
git commit -m "mpi_host"
git push
docker rmi raijenki/mpik8s:gromacs
docker build . -t raijenki/mpik8s:gromacs
docker push raijenki/mpik8s:gromacs
kubectl create -f gromacs.yaml
