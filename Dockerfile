# Builder image
FROM ubuntu:18.04 as builder

# Install build tools and libraries
RUN apt-get -q update &&\
	DEBIAN_FRONTEND="noninteractive" apt-get -q install -y -o Dpkg::Options::="--force-confnew" --no-install-recommends build-essential cmake libjpeg-dev libpng-dev zlib1g-dev &&\
	rm -rf /var/lib/apt/lists/*

# Copy sources
COPY . /usr/src/openjk

# Build x86_64 arch
RUN mkdir /usr/src/openjk/build.x86_64 &&\
	cd /usr/src/openjk/build.x86_64 &&\
	cmake -DCMAKE_INSTALL_PREFIX=/opt \
		-DBuildMPCGame=OFF -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPUI=OFF \
		-DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF -DBuildMPRdVulkanVision=OFF \
		.. &&\
	make &&\
	make install


# Server image
FROM ubuntu:18.04

# Install utilities and libraries
RUN apt-get -q update &&\
	DEBIAN_FRONTEND="noninteractive" apt-get -q install -y -o Dpkg::Options::="--force-confnew" --no-install-recommends socat libstdc++6 zlib1g &&\
	rm -rf /var/lib/apt/lists/*

# Copy binaries and scripts
RUN mkdir -p /opt/openjk/cdpath/base /opt/openjk/basepath /opt/openjk/homepath
COPY --from=builder /opt/JediAcademy/openjkded.* /opt/openjk/
COPY --from=builder /opt/JediAcademy/base/ /opt/openjk/cdpath/base/
COPY --from=builder /opt/JediAcademy/OpenJK/ /opt/openjk/cdpath/OpenJK/
COPY scripts/docker/*.sh /opt/openjk/
COPY scripts/docker/server.cfg /opt/openjk/cdpath/base/
COPY scripts/docker/server.cfg /opt/openjk/cdpath/OpenJK/
RUN chmod +x /opt/openjk/openjkded.* /opt/openjk/*.sh

# Execution
ENV OJK_OPTS="+exec server.cfg"
EXPOSE 29070/udp
HEALTHCHECK --interval=10s --timeout=9s --retries=6 CMD ["/opt/openjk/healthcheck.sh"]
CMD ["/opt/openjk/run.sh"]
