#kubectl delete -f cm1.yaml
git add .
git commit -m "mpi_host"
git push
docker rmi raijenki/mpik8s:cm1
docker build . -t raijenki/mpik8s:cm1
docker push raijenki/mpik8s:cm1
kubectl create -f cm1.yaml
