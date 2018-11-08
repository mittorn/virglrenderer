#!/bin/bash

set -x

#DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends ninja-build meson

# To prevent hitting assertions such as the below:
# sb/sb_sched.cpp:1207:schedule_alu: Assertion `!"unscheduled pending instructions"' failed.
export R600_DEBUG=nosb

export CCACHE_BASEDIR=/virglrenderer
export CCACHE_DIR=/virglrenderer/ccache
export PATH="/usr/lib/ccache:$PATH"
mkdir -p $CCACHE_DIR
ccache -s


# If render node, like /dev/dri/renderD128, hasn't been set
# or exists use softpipe instead of HW GPU.
if [[ ! -c $RENDER_DEVICE ]]; then
    export LIBGL_ALWAYS_SOFTWARE=1
    export GALLIVM_PERF=no_filter_hacks
    export GALLIUM_DRIVER=softpipe
    LIMIT_TESTSET=--only-softpipe
fi

cd /virglrenderer
if [[ ! $LOCAL_DEV ]]; then
   ./autogen.sh --prefix=/usr/local --enable-debug --enable-tests
   VRENDTEST_USE_EGL_SURFACELESS=1 make distcheck
   if [ $? -ne 0 ]; then
       touch /virglrenderer/results/regressions_detected
       cp virglrenderer-0.7.0/_build/sub/tests/*.log /virglrenderer/results/
   fi
fi
make -j$(nproc) install

if [[ $LOCAL_DEV ]]; then
   cd /mesa
   mkdir -p build
   meson build/
   meson configure build/ -Dprefix=/usr/local -Dplatforms=drm,x11,wayland,surfaceless -Ddri-drivers=i965 -Dgallium-drivers=swrast,virgl,radeonsi,r600 -Dbuildtype=debugoptimized -Dllvm=true -Dglx=dri -Dgallium-vdpau=false -Dgallium-va=false -Dvulkan-drivers=[] -Dlibdir=lib
   ninja -C build/ install
fi

: '
cd /qemu
make -j$(nproc) install
'

: '
cd /VK-GL-CTS/build
#cmake -DDEQP_TARGET=x11_egl -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
cp -rf * /usr/local/VK-GL-CTS/.
'

ccache -s

: '
ln -s /usr/local/bin/qemu-system-x86_64 /usr/bin/qemu-system-x86_64
echo "Starting guest for ES"
mkdir -p /virglrenderer/results/es_host
rm -f core

fakemachine --qemuopts="-vga virtio -display egl-headless,gl=es" \
            --show-boot \
            --memory=8192 \
            -v /virglrenderer:/virglrenderer \
            -v /virglrenderer/results/es_host:/results \
            -- /virglrenderer/ci/run-deqp.sh
'

# Cut this short if the unit tests fail

if [ ! -f /virglrenderer/results/regressions_detected ]; then
    /virglrenderer/ci/run-deqp.sh --with-vtest $LIMIT_TESTSET
    /virglrenderer/ci/run-deqp.sh --host-gl --with-vtest $LIMIT_TESTSET
fi 

if [ -f /virglrenderer/results/regressions_detected ]; then
   exit 1
fi

