FROM debian:11-slim As base
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
     && apt-get install -y build-essential python3 python3-pip ssh time
RUN cd /home/ && git clone https://github.com/raijenki/hpc-tests.git \
    && cd /home/hpc-tests/pic/src && make \
    && cp /home/hpc-tests/pic/src/sputniPIC /home && mv /home/hpc-tests/pic/* /home/ \
    && rm -rf /home/hpc-tests && rm -rf /home/src && rm /home/Dockerfile
RUN chmod +x /home/exec_pic.sh && chmod +x /home/end_exec.sh && chmod a+x /home/sputniPIC && chmod +x /home/inputs.txt
RUN mkdir -p /var/run/sshd;
CMD /usr/sbin/sshd;
