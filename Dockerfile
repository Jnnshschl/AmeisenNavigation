# AmeisenNavigation server + exporter (Linux x64/arm64).
#
#   docker build -t ameisennav .
#
#   # Server: meshes from a volume, every config key can be set as ANAV_<key>
#   docker run -d --name ameisennav -p 47110:47110 -v /srv/meshes:/meshes:ro ameisennav
#   docker run -d -p 47110:47110 -v /srv/mmaps:/meshes:ro -e ANAV_bUseAnpFileFormat=0 ameisennav
#
#   # Exporter: WoW client in, .anp files out
#   docker run --rm -v /srv/wow:/wow:ro -v /srv/meshes:/meshes ameisennav exporter -w /wow -o /meshes -m 0,1 -s

FROM ubuntu:26.04 AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake ninja-build git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Extra CMake options, e.g. --build-arg ANAV_CMAKE_ARGS="-DANAV_ENABLE_LTO=OFF"
ARG ANAV_CMAKE_ARGS=""

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DANAV_BUILD_TESTS=OFF ${ANAV_CMAKE_ARGS} \
 && cmake --build /build \
 && cmake --install /build --prefix /opt/ameisennav --strip

FROM ubuntu:26.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends libgomp1 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --no-create-home --shell /usr/sbin/nologin ameisennav

COPY --from=build /opt/ameisennav/bin/ /usr/local/bin/
COPY deploy/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

# Defaults for the container, a config file mounted at /etc/ameisennav/config.cfg is loaded first and
# these (and any other ANAV_<key> variables) override it.
ENV ANAV_sIp=0.0.0.0 \
    ANAV_iPort=47110 \
    ANAV_sMmapsPath=/meshes \
    ANAV_bUseAnpFileFormat=1

USER ameisennav
EXPOSE 47110
VOLUME ["/meshes"]
STOPSIGNAL SIGTERM

ENTRYPOINT ["docker-entrypoint.sh"]
CMD ["server"]
