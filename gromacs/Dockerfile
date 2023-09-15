FROM ubuntu:22.04
RUN /bin/bash -c set -xe \ 
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
     && apt-get install -y git wget cmake zip unzip \
     && apt-get install -y build-essential gfortran libopenmpi-dev \
     && apt-get install -y ssh time python3
RUN cd /home/ && wget ftp://ftp.gromacs.org/gromacs/gromacs-2023.tar.gz \
    && tar xvf gromacs-2023.tar.gz && cd gromacs-2023 \
    && mkdir build && cd build \
    && cmake .. -DGMX_BUILD_OWN_FFTW=ON -DREGRESSIONTEST_DOWNLOAD=ON -DGMX_MPI=ON && make && make install
RUN apt-get install -y python3-pip
RUN pip install grpcio grpcio-tools psutil kubernetes retrying
ADD "https://www.random.org/cgi-bin/randbyte?nbytes=10&format=h" skipcache
RUN cd /home/ && git clone http://github.com/raijenki/hpc-tests.git && cd hpc-tests/gromacs && python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=. mpi_monitor.proto
RUN cd /home/hpc-tests/gromacs/ && wget https://www.mpinat.mpg.de/benchMEM && unzip benchMEM
WORKDIR /home
SHELL ["/bin/bash", "-l", "-c"]
RUN cat /usr/local/gromacs/bin/GMXRC.bash > /root/.bashrc
RUN mkdir -p /var/run/sshd;
CMD /usr/sbin/sshd;
