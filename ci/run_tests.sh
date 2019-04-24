#!/bin/bash

run_setup()
{
   set -x

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
        exit 1
      fi
   fi

   VIRGL_PATH="/virglrenderer"
   rm -rf $VIRGL_PATH/results/
   mkdir -p $VIRGL_PATH/results/

   if [ -d "$VIRGL_PATH" ]; then
       cd $VIRGL_PATH
       ./autogen.sh --prefix=/usr/local --enable-debug --enable-tests
       make -j$NUM_THREADS install
   fi

   CI_DIR=$(dirname $(readlink -f "$0"))
   cd $CI_DIR
}

run_make_check()
{
   (
      cd /virglrenderer
      mkdir -p /virglrenderer/results/make_check
      VRENDTEST_USE_EGL_SURFACELESS=1 make -j$NUM_THREADS check --no-print-directory
      RET=$?
      cp tests/test*.log /virglrenderer/results/make_check/
      return $RET
   )
}

run_deqp()
{
   UNRELIABLE="$1"
   OGL_BACKEND="$2"
   SUITE="$3"

   if [ $UNRELIABLE -eq 1 ]; then
      TEST_UNRELIABLE="--unreliable"
   fi

   if [ "$SUITE" = "gl" ]; then
      TEST_SUITE="--gl30 --gl31 --gl32"
   fi

   if [ "$SUITE" = "gles" ]; then
      TEST_SUITE="--gles2 --gles3 --gles31"
   fi

   BACKENDS="--backend vtest-softpipe"
   if [[ -z "$SOFTWARE_ONLY" ]]; then
      BACKENDS="${BACKENDS} --backend vtest-gpu"
   fi

   ./run_test_suite.sh --deqp ${TEST_SUITE} \
      --host-${OGL_BACKEND} \
      ${TEST_UNRELIABLE} \
      ${BACKENDS} \
      --compare-previous

   return $?
}

run_piglit()
{
   UNRELIABLE="$1"
   OGL_BACKEND="$2"

   if [ $UNRELIABLE -eq 1 ]; then
      TEST_UNRELIABLE="--unreliable"
   fi

   BACKENDS="--backend vtest-softpipe"
   if [[ -z "$SOFTWARE_ONLY" ]]; then
      BACKENDS="${BACKENDS} --backend vtest-gpu"
   fi

   ./run_test_suite.sh --piglit --gles2 --gles3 \
      --host-${OGL_BACKEND} \
      ${TEST_UNRELIABLE} \
      ${BACKENDS} \
      --compare-previous

   return $?
}

parse_input()
{
   RET=0
   while  [ -n "$1" ]; do
      echo ""
      UNRELIABLE=0

      case $1 in
         --make-check)
         run_make_check
         ;;

         --deqp-gl-unreliable)
         UNRELIABLE=1
         ;& #Fallthrough

         --deqp-gl-gl-tests)
         run_deqp $UNRELIABLE gl gl
         ;;

         --deqp-gl-gles-tests)
         run_deqp $UNRELIABLE gl gles
         ;;

         --deqp-gles-unreliable)
         UNRELIABLE=1
         ;& #Fallthrough

         --deqp-gles-gl-tests)
         run_deqp $UNRELIABLE gles gl
         ;;

         --deqp-gles-gles-tests)
         run_deqp $UNRELIABLE gles gles
         ;;

         --piglit-gl-unreliable)
         UNRELIABLE=1
         ;& #Fallthrough

         --piglit-gl)
         run_piglit $UNRELIABLE gl
         ;;

         --piglit-gles-unreliable)
         UNRELIABLE=1
         ;& #Fallthrough

         --piglit-gles)
         run_piglit $UNRELIABLE gles
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

run_setup
parse_input $@
