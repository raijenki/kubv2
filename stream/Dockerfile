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
     && apt-get install -y git \
     && apt-get install -y build-essential \
     && apt-get install -y cmake \
     && apt-get install -y libopenblas-dev \
     && apt-get install -y ssh time \
     && apt-get clean \
     && rm -rf /var/lib/apt/lists/*
RUN cd /home && git clone https://github.com/raijenki/hpc-tests.git \
    && cd hpc-tests/stream \
    && make

FROM debian:11-slim As prod
RUN apt-get update && apt-get install -y glibc-source libgomp1 python3 python3-pip git time
RUN pip install grpcio grpcio-tools psutil kubernetes
RUN cd /home && git clone http://github.com/raijenki/hpc-tests.git && cd hpc-tests/stream && python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=. mpi_monitor.proto
COPY --from=base /home/hpc-tests/stream/stream_c /home/hpc-tests/stream/stream_c
RUN chmod +x /home/hpc-tests/stream/stream_c
WORKDIR /home

