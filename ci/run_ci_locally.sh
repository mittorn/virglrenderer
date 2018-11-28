#!/bin/bash

PROJECT_PATH="$(dirname $(readlink -f "$0"))/.."

cd ${PROJECT_PATH}

DOCKER_DRIVER=overlay2
DOCKER_IMAGE=virglrenderer/ci

# Use slightly less than half of available threads
NUM_THREADS=$(expr $(expr $(nproc) + 2) / 3)
RENDER_DEVICE=/dev/dri/renderD128

LOCAL_MESA_PATH="../$PROJECT_PATH/mesa"
LOCAL_VIRGL_PATH="${PROJECT_PATH}"

if test $NUM_THREADS -gt 0; then THREAD_CONFIG="-e NUM_THREADS=$NUM_THREADS"; fi
if test -e $RENDER_DEVICE; then RD_CONFIG="--device=$RENDER_DEVICE -e RENDER_DEVICE=$RENDER_DEVICE"; fi
if test -e $LOCAL_MESA_PATH; then LOCAL_MESA="-v $LOCAL_MESA_PATH:/local_mesa -e LOCAL_MESA=/local_mesa"; fi
if test -e $LOCAL_VIRGL_PATH; then LOCAL_VIRGL="-v $LOCAL_VIRGL_PATH:/virglrenderer -e LOCAL_VIRGL=/virglrenderer"; fi

echo THREAD_CONFIG=$THREAD_CONFIG
echo RD_CONFIG=$RD_CONFIG
echo LOCAL_MESA=$LOCAL_MESA
echo LOCAL_VIRGL=$LOCAL_VIRGL

rm -rf $PROJECT_PATH/results
mkdir -p $PROJECT_PATH/results

time docker build -t $DOCKER_IMAGE -f ci/Dockerfile --cache-from $DOCKER_IMAGE:latest ci

time docker run \
     -it \
     --ulimit core=99999999999:99999999999 \
     $THREAD_CONFIG \
     $RD_CONFIG \
     $LOCAL_MESA \
     $LOCAL_VIRGL \
     $DOCKER_IMAGE:latest \
     /virglrenderer/ci/run-tests.sh 2>&1 | tee results/log.txt