#!/bin/bash

set -x

#DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends ninja-build meson

# Let .gitlab-ci or local ci runner set
# desired thread count
NUM_THREADS=${NUM_THREADS:-"$(nproc)"}
export NUM_THREADS
echo "Using $NUM_THREADS threads"

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

if [[ $LOCAL_MESA ]]; then
   cd $LOCAL_MESA && \
   mkdir -p build  && \
   meson build/ && \
   meson configure build/ -Dprefix=/usr/local -Dplatforms=drm,x11,wayland,surfaceless -Ddri-drivers=i965 -Dgallium-drivers=swrast,virgl,radeonsi,r600 -Dbuildtype=debugoptimized -Dllvm=true -Dglx=dri -Dgallium-vdpau=false -Dgallium-va=false -Dvulkan-drivers=[] -Dlibdir=lib && \
   ninja -C build/ install -j $NUM_THREADS
   if [ $? -ne 0 ]; then
      exit 1
   fi
fi


rm -rf /virglrenderer/results/
mkdir -p /virglrenderer/results/
cd /virglrenderer
./autogen.sh --prefix=/usr/local --enable-debug --enable-tests
VRENDTEST_USE_EGL_SURFACELESS=1 make check
if [ $? -ne 0 ]; then
    touch /virglrenderer/results/regressions_detected
fi
mkdir -p /virglrenderer/results/make_check
cp tests/test*.log /virglrenderer/results/make_check/
make -j$NUM_THREADS install


# Stop testing process if a failure have been found
if [ -f /virglrenderer/results/regressions_detected ]; then
   exit 1
fi

ccache -s


# Stop testing process if a failure have been found
if [ -f /virglrenderer/results/regressions_detected ]; then
   exit 1
fi

/virglrenderer/ci/run-deqp.sh --with-vtest $LIMIT_TESTSET
/virglrenderer/ci/run-deqp.sh --host-gl --with-vtest $LIMIT_TESTSET

# Return test pass/fail
if [ -f /virglrenderer/results/regressions_detected ]; then
   exit 1
fi
