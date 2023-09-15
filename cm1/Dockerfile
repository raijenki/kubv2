FROM debian:11 As base
RUN /bin/sh -c set -xe \ 
    && echo '#!/bin/sh' > /usr/sbin/policy-rc.d \
    && echo 'exit 101' >> /usr/sbin/policy-rc.d \
    && chmod +x /usr/sbin/policy-rc.d \
    && dpkg-divert --local --rename --add /sbin/initctl \
    && cp -a /usr/sbin/policy-rc.d /sbin/initctl \
    && sed -i 's/^exit.*/exit 0/' /sbin/initctl \		
    && echo 'force-unsafe-io' > /etc/dpkg/dpkg.cfg.d/docker-apt-speedup \
    && echo 'DPkg::Post-Invoke { "rm -f /var/cache/apt/archives/*.deb /var/cache/apt/archives/partial/*.deb /var/cache/apt/*.bin || true"; };' > /etc/apt/apt.conf.d/docker-clean \
    && echo 'APT::Update::Post-Invoke { "rm -f /var/cache/apt/archives/*.deb /var/cache/apt/archives/partial/*.deb /var/cache/apt/*.bin || true"; };' >> /etc/apt/apt.conf.d/docker-clean \	
    && echo 'Dir::Cache::pkgcache ""; Dir::Cache::srcpkgcache "";' >> /etc/apt/apt.conf.d/docker-clean 	\
    && echo 'Acquire::Languages "none";' > /etc/apt/apt.conf.d/docker-no-languages 	\
    && echo 'Acquire::GzipIndexes "true"; Acquire::CompressionTypes::Order:: "gz";' > /etc/apt/apt.conf.d/docker-gzip-indexes \
    && echo 'Apt::AutoRemove::SuggestsImportant "false";' > /etc/apt/apt.conf.d/docker-autoremove-suggests
RUN rm -rf /var/lib/apt/lists/*
RUN mkdir -p /run/systemd && echo 'docker' > /run/systemd/container
RUN  apt-get update --fix-missing \
     && apt-get install -y wget git \
     && apt-get install -y build-essential gfortran libopenmpi-dev \
     && apt-get install -y ssh time 
RUN cd /home/ && wget https://www2.mmm.ucar.edu/people/bryan/cm1/cm1r20.3.tar.gz \
    && tar xvf cm1r20.3.tar.gz
RUN cd /home/ && git clone https://github.com/raijenki/hpc-tests.git \
    && cp /home/hpc-tests/cm1/Makefile /home/cm1r20.3/src/. \
    && cd /home/cm1r20.3/src && make -j \
    && cp /home/cm1r20.3/run/cm1.exe /home/

FROM debian:11-slim As prod
RUN apt-get update && apt-get install -y gfortran openmpi-bin python3 git ssh time python3-pip
RUN pip install grpcio grpcio-tools psutil kubernetes retrying
ADD "https://www.random.org/cgi-bin/randbyte?nbytes=10&format=h" skipcache
WORKDIR /home
RUN cd /home && git clone http://github.com/raijenki/hpc-tests.git && cd hpc-tests/cm1 && python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=. mpi_monitor.proto
COPY --from=base /home/cm1.exe /home/hpc-tests/cm1/.
RUN mkdir -p /var/run/sshd;
CMD /usr/sbin/sshd;
