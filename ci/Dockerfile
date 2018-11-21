FROM debian:testing-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV GOPATH=/usr/local/go
ENV PATH=$PATH:/usr/local/go/bin
ENV LD_LIBRARY_PATH=/usr/local/lib64:/usr/local/lib:/usr/local/lib/x86_64-linux-gnu
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig:/usr/local/share/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig
ENV LDFLAGS="-L/usr/local/lib64 -L/usr/local/lib/ -L/usr/local/lib/x86_64-linux-gnu"
ENV CFLAGS="-g3"
ENV CXXFLAGS="-g3"
ENV GIT_DATE="`date +%Y-%m-%d -d \"1 year ago\"`"

RUN echo 'path-exclude=/usr/share/doc/*' > /etc/dpkg/dpkg.cfg.d/99-exclude-cruft
RUN echo 'path-exclude=/usr/share/man/*' >> /etc/dpkg/dpkg.cfg.d/99-exclude-cruft
RUN echo '#!/bin/sh' > /usr/sbin/policy-rc.d
RUN echo 'exit 101' >> /usr/sbin/policy-rc.d
RUN chmod +x /usr/sbin/policy-rc.d

RUN echo deb-src http://deb.debian.org/debian testing main >> /etc/apt/sources.list
RUN apt-get update && \
    apt-get -y install ca-certificates && \
    apt-get -y install --no-install-recommends \
      libgbm-dev \
      libxvmc-dev \
      autoconf \
      golang-go \
      cmake \
      spirv-headers \
      xinit \
      check \
      linux-image-amd64 \
      git \
      procps \
      systemd \
      libnss-systemd \
      dbus \
      strace \
      systemd-coredump \
      time \
      busybox \
      kbd \
      ccache \
      xserver-xorg-core \
      xterm \
      wget \
      gdc-6 \
      clang-6.0 \
      lld-6.0 \
      nasm \
      waffle-utils \
      gdb \
      libcurl4-openssl-dev \
      xvfb \
      mesa-utils \
      strace \
      psmisc \
      libpng-dev \
      libxrandr-dev \
      libsm-dev \
      meson \
      ninja-build \
      zlib1g-dev \
      libwayland-dev \
      libwayland-egl-backend-dev && \
    apt-get -y build-dep --no-install-recommends \
      qemu \
      libdrm \
      mesa \
      virglrenderer \
      libepoxy \
      piglit && \
    apt-get -y remove valgrind && \
    apt-get clean

ARG KNOWN_GOOD_FAKEMACHINE=c4752ddf3343
RUN go get -v github.com/tomeuv/fakemachine/cmd/fakemachine
RUN go install -x github.com/tomeuv/fakemachine/cmd/fakemachine

# To avoid this error:
# error: RPC failed; curl 56 GnuTLS recv error (-54): Error in the pull function.
RUN git config --global http.postBuffer 1048576000

ARG KNOWN_GOOD_EPOXY=737b6918703c
WORKDIR /libepoxy
RUN git clone --shallow-since="$GIT_DATE" https://github.com/anholt/libepoxy.git . && \
    git checkout ${KNOWN_GOOD_EPOXY} && \
    git log --oneline -n 1 && \
    ./autogen.sh --prefix=/usr/local && \
    make -j$(nproc) install && \
    rm -rf /libepoxy
WORKDIR /

ARG KNOWN_GOOD_CTS=fd68124a565e
WORKDIR /VK-GL-CTS
RUN git clone --shallow-since="$GIT_DATE" https://github.com/KhronosGroup/VK-GL-CTS.git . && \
    git checkout ${KNOWN_GOOD_CTS} && \
    git log --oneline -n 1 && \
    mkdir build && \
    cd build && \
    cmake -DDEQP_TARGET=x11_egl -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    find . -name CMakeFiles | xargs rm -rf && \
    find . -name lib\*.a | xargs rm -rf && \
    mv /VK-GL-CTS/build /usr/local/VK-GL-CTS && \
    rm -rf /VK-GL-CTS
WORKDIR /

ARG KNOWN_GOOD_PIGLIT=1a2f49f17fb45
WORKDIR /piglit
RUN git clone --shallow-since="$GIT_DATE" https://gitlab.freedesktop.org/mesa/piglit.git . && \
    git checkout ${KNOWN_GOOD_PIGLIT} && \
    git log --oneline -n 1 && \
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release . && \
    make -j$(nproc) install && \
    rm -rf /usr/local/lib/piglit/generated_tests/spec/arb_vertex_attrib_64bit && \
    rm -rf /usr/local/lib/piglit/generated_tests/spec/glsl-4.20 && \
    rm -rf /piglit
WORKDIR /

WORKDIR /battery
RUN wget https://github.com/VoltLang/Battery/releases/download/v0.1.18/battery-0.1.18-x86_64-linux.tar.gz && \
    tar xzvf battery-0.1.18-x86_64-linux.tar.gz && \
    rm battery-0.1.18-x86_64-linux.tar.gz && \
    mv battery /usr/local/bin
WORKDIR /

WORKDIR /volt
RUN git clone --depth=1 https://github.com/VoltLang/Watt.git && \
    git clone --depth=1 https://github.com/VoltLang/Volta.git && \
    git clone --depth=1 https://github.com/Wallbraker/dEQP.git && \
    battery config --release --lto Volta Watt && \
    battery build && \
    battery config --cmd-volta Volta/volta Volta/rt Watt dEQP && \
    battery build && \
    cp dEQP/deqp /usr/local/bin && \
    rm -rf /volt
WORKDIR /

ARG KNOWN_GOOD_DRM=4ec31fc31a4be909c8204164c844b4a18f098af7
WORKDIR /drm
RUN git clone --shallow-since="$GIT_DATE" https://gitlab.freedesktop.org/mesa/drm.git . && \
    git checkout ${KNOWN_GOOD_DRM} && \
    git log --oneline -n 1 && \
    mkdir -p build && \
    meson build/ && \
    meson configure build/ -Dprefix=/usr/local -Dlibdir=lib && \
    ninja -C build/ install && \
    rm -rf /drm
WORKDIR /

ARG KNOWN_GOOD_MESA=d877451b48a59ab0f9a4210fc736f51da5851c9a
WORKDIR /mesa
RUN git clone --shallow-since="$GIT_DATE" https://gitlab.freedesktop.org/mesa/mesa.git . && \
    git checkout ${KNOWN_GOOD_MESA} && \
    git log --oneline -n 1 && \
    mkdir -p build && \
    meson build/ && \
    meson configure build/ -Dprefix=/usr/local -Dplatforms=drm,x11,wayland,surfaceless -Ddri-drivers=i965 -Dgallium-drivers=swrast,virgl,radeonsi,r600 -Dbuildtype=debugoptimized -Dllvm=true -Dglx=dri -Dgallium-vdpau=false -Dgallium-va=false -Dvulkan-drivers=[] -Dlibdir=lib && \
    ninja -C build/ install && \
    rm -rf /mesa
WORKDIR /

