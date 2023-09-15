#kubectl delete -f cm1.yaml
git add .
git commit -m "mpi_host"
git push
docker rmi raijenki/mpik8s:smpi
docker build . -t raijenki/mpik8s:smpi
docker push raijenki/mpik8s:smpi
kubectl create -f smpi.yaml
