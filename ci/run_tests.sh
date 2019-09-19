#!/bin/bash

run_setup()
{
   set -x

   use_meson=$1

   # Let .gitlab-ci or local ci runner set
   # desired thread count
   NUM_THREADS=${NUM_THREADS:-$(expr $(expr $(nproc) / 8) + 1)}
   export NUM_THREADS
   echo "Using $NUM_THREADS threads"

   export CCACHE_BASEDIR=/virglrenderer
   export CCACHE_DIR=/virglrenderer/ccache
   export PATH="/usr/lib/ccache:$PATH"
   mkdir -p $CCACHE_DIR
   ccache -s

   # To prevent hitting assertions such as the below:
   # sb/sb_sched.cpp:1207:schedule_alu: Assertion '!"unscheduled pending instructions"' failed.
   export R600_DEBUG=nosb

   # If render node, like /dev/dri/renderD128, has not been set
   # or exists use softpipe instead of HW GPU.
   if [[ ! -c $RENDER_DEVICE ]]; then
      export SOFTWARE_ONLY=1
   fi

   set +x

   if [[ $LOCAL_MESA ]]; then
      cd $LOCAL_MESA && \
      mkdir -p build  && \
      meson build/ && \
      meson configure build/ -Dprefix=/usr/local -Dplatforms=drm,x11,wayland,surfaceless -Ddri-drivers=i965 -Dgallium-drivers=swrast,virgl,radeonsi,r600 -Dbuildtype=debugoptimized -Dllvm=true -Dglx=dri -Dgallium-vdpau=false -Dgallium-va=false -Dvulkan-drivers=[] -Dlibdir=lib && \
      ninja -C build/ install -j $NUM_THREADS
      if [ $? -ne 0 ]; then
        meson setup --wipe build/
        ninja -C build/ install -j $NUM_THREADS || exit 1
      fi
   fi

   VIRGL_PATH="/virglrenderer"
   rm -rf $VIRGL_PATH/results/
   mkdir -p $VIRGL_PATH/results/

   if [ "x$use_meson" = "x" ]; then
      if [ -d "$VIRGL_PATH" ]; then
          cd $VIRGL_PATH
          ./autogen.sh --prefix=/usr/local --enable-debug --enable-tests
          make -j$NUM_THREADS install
      fi
   else
      if [ -d "$VIRGL_PATH" ]; then
          cd $VIRGL_PATH
          mkdir build
          meson build/ -Dprefix=/usr/local -Ddebug=true -Dtests=true --fatal-meson-warnings
          ninja -C build -j$NUM_THREADS install
      fi
   fi

   CI_DIR=$(dirname $(readlink -f "$0"))
   cd $CI_DIR
}

run_make_check()
{
   run_setup
   (
      cd /virglrenderer
      mkdir -p /virglrenderer/results/make_check
      VRENDTEST_USE_EGL_SURFACELESS=1 make -j$NUM_THREADS check --no-print-directory
      RET=$?
      cp tests/test*.log /virglrenderer/results/make_check/
      return $RET
   )
}

run_make_check_meson()
{
   run_setup meson
   (
      cd /virglrenderer/build
      mkdir -p /virglrenderer/results/make_check_meson
      VRENDTEST_USE_EGL_SURFACELESS=1 ninja -j$NUM_THREADS test
      RET=$?
      cp /virglrenderer/build/meson-logs/testlog.txt /virglrenderer/results/make_check_meson/
      return $RET
   )
}

run_deqp()
{
   run_setup
   OGL_BACKEND="$1"
   SUITE="$2"

   if [ "$SUITE" = "gl" ]; then
      TEST_SUITE="--gl30 --gl31 --gl32"
   fi

   if [ "$SUITE" = "gles" ]; then
      TEST_SUITE="--gles2 --gles3 --gles31"
   fi

   if [ "$SUITE" = "gles2" ]; then
      TEST_SUITE="--gles2"
   fi

   if [ "$SUITE" = "gles3" ]; then
      TEST_SUITE="--gles3"
   fi

   if [ "$SUITE" = "gles31" ]; then
      TEST_SUITE="--gles31"
   fi
   
   BACKENDS=""
   if [[ -z "$HARDWARE_ONLY" ]]; then
      BACKENDS="${BACKENDS} --backend vtest-softpipe"
   fi

   if [[ -z "$SOFTWARE_ONLY" ]]; then
      BACKENDS="${BACKENDS} --backend vtest-gpu"
   fi

   ./run_test_suite.sh --deqp ${TEST_SUITE} \
      --host-${OGL_BACKEND} \
      ${BACKENDS}

   return $?
}

run_piglit()
{
   run_setup

   OGL_BACKEND="$1"

   BACKENDS=""
   if [[ -z "$HARDWARE_ONLY" ]]; then
      BACKENDS="${BACKENDS} --backend vtest-softpipe"
   fi
   
   if [[ -z "$SOFTWARE_ONLY" ]]; then
      BACKENDS="${BACKENDS} --backend vtest-gpu"
   fi

   ./run_test_suite.sh --piglit --gles2 --gles3 \
      --host-${OGL_BACKEND} \
      ${BACKENDS}

   return $?
}

parse_input()
{
   RET=0
   while  [ -n "$1" ]; do
      echo ""

      case $1 in
         --make-check)
         run_make_check
         ;;

         --make-check-meson)
         run_make_check_meson
         ;;

         --deqp-gl-gl-tests)
         run_deqp gl gl
         ;;

         --deqp-gl-gles-tests)
         run_deqp gl gles
         ;;

         --deqp-gl-gles2-tests)
         run_deqp gl gles2
         ;;

         --deqp-gl-gles3-tests)
         run_deqp gl gles3
         ;;

         --deqp-gl-gles31-tests)
         run_deqp gl gles31
         ;;
         
         --deqp-gles-gl-tests)
         run_deqp gles gl
         ;;

         --deqp-gles-gles-tests)
         run_deqp gles gles
         ;;

         --deqp-gles-gles2-tests)
         run_deqp gles gles2
         ;;

         --deqp-gles-gles3-tests)
         run_deqp gles gles3
         ;;

         --deqp-gles-gles31-tests)
         run_deqp gles gles31
         ;;
         
         --piglit-gl)
         run_piglit gl
         ;;

         --piglit-gles)
         run_piglit gles
         ;;

         *)
         echo "Unknown test option $1"
         exit 1
      esac

      if [ $? -ne 0 ]; then
         RET=1
      fi

      shift
   done

   exit $RET
}

parse_input $@
