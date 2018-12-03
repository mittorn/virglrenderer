#!/bin/bash

set -x

ONLY_GLES2=no

for arg
do
    case "$arg" in
        --with-vtest)
            WITH_VTEST=1
            ;;
        --host-gl)
            HOST_GL=1
            ;;
        --only-softpipe)
            ONLY_SOFTPIPE_PASS=yes
            ;;
        *)
            echo "Unknown argument"
            exit 1
            ;;
     esac
done

export PATH=$PATH:/usr/local/go/bin
export LD_LIBRARY_PATH=/usr/local/lib64:/usr/local/lib:/usr/local/lib/x86_64-linux-gnu
#export MESA_GLES_VERSION_OVERRIDE=3.1

if [[ -n $WITH_VTEST ]]; then
   nohup Xvfb :0 -screen 0 1024x768x24 &
else
   startx &
fi
sleep 3
export DISPLAY=:0

if [[ -n "$LIBGL_ALWAYS_SOFTWARE" ]]; then
    HOST_GALLIUM_DRIVER="_${GALLIUM_DRIVER}"
fi

if [[ -n "$HOST_GL" ]]; then
    echo "Running tests with GL on the host"
    RESULTS_DIR=/virglrenderer/results/gl_host${HOST_GALLIUM_DRIVER}
    PREVIOUS_RESULTS_DIR=/virglrenderer/ci/previous_results/gl_host${HOST_GALLIUM_DRIVER}
else
    echo "Running tests with GLES on the host"
    RESULTS_DIR=/virglrenderer/results/es_host${HOST_GALLIUM_DRIVER}
    PREVIOUS_RESULTS_DIR=/virglrenderer/ci/previous_results/es_host${HOST_GALLIUM_DRIVER}
fi
mkdir -p $RESULTS_DIR

if [[ -n "$WITH_VTEST" ]]; then
    if [[ -n "$HOST_GL" ]]; then
        if [[ -n "$ONLY_SOFTPIPE_PASS" ]]; then
            # Softpipe only supports GL 3.3, but for guest GLES 3.1
            # one GL 4.4 property is needed
            VTEST_USE_EGL_SURFACELESS=1 nohup /virglrenderer/vtest/virgl_test_server >$RESULTS_DIR/vtest_gl.log 2>&1  &
        else
            VTEST_USE_EGL_SURFACELESS=1 nohup /virglrenderer/vtest/virgl_test_server >$RESULTS_DIR/vtest_gl.log  2>&1 &
        fi
   else
       VTEST_USE_EGL_SURFACELESS=1 VTEST_USE_GLES=1 nohup /virglrenderer/vtest/virgl_test_server >$RESULTS_DIR/vtest_gles.log 2>&1  &
   fi

   sleep 1

   # Don't set the GALLIUM_DRIVER to virpipe before
   # initializing virgl_test_server
   export GALLIUM_DRIVER=virpipe
fi

if [[ "x$ONLY_SOFTPIPE_PASS"="xyes" ]] ; then
    time deqp --threads=$NUM_THREADS \
     --cts-build-dir=/usr/local/VK-GL-CTS/ \
     --test-names-file=/virglrenderer/ci/softpipe-gles-pass.txt \
     --print-failing \
     --results-file=$RESULTS_DIR/deqp_results.txt
else
    time deqp --threads=$NUM_THREADS \
     --cts-build-dir=/usr/local/VK-GL-CTS/ \
     --test-names-file=/virglrenderer/ci/deqp-gles2-list.txt \
     --test-names-file=/virglrenderer/ci/deqp-gles3-list.txt \
     --test-names-file=/virglrenderer/ci/deqp-gles31-list.txt \
     --print-failing \
     --results-file=$RESULTS_DIR/deqp_results.txt
fi
cp -rf /tmp/dEQP/* $RESULTS_DIR/.

# Remove header
sed -i "/#/d" $RESULTS_DIR/deqp_results.txt

# TODO: These tests are not reliable when run on radeonsi, someone should fix them and then remove these lines
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.fbo.msaa.2_samples.rgb8"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.fbo.msaa.4_samples.rgb8"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.fbo.msaa.8_samples.rgb8"

# TODO: These tests aren't reliable either
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.singlesample_texture"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_texture_1"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_texture_2"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_texture_4"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_texture_8"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.singlesample_rbo"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_rbo_1"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_rbo_2"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_rbo_4"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.sample_variables.sample_pos.correctness.multisample_rbo_8"

# TODO: These tests aren't reliable either
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.negative_api.shader.sampler_parameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.negative_api.shader.sampler_parameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.negative_api.shader.sampler_parameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.negative_api.shader.sampler_parameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES3.functional.negative_api.state.get_synciv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.sampler_parameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.sampler_parameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.sampler_parameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.sampler_parameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.srgb_decode_samplerparameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.srgb_decode_samplerparameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.srgb_decode_samplerparameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.shader.srgb_decode_samplerparameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.callbacks.state.get_synciv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.compute.exceed_atomic_counters_limit"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.sampler_parameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.sampler_parameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.sampler_parameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.sampler_parameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.srgb_decode_samplerparameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.srgb_decode_samplerparameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.srgb_decode_samplerparameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.shader.srgb_decode_samplerparameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.sampler_parameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.sampler_parameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.sampler_parameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.sampler_parameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.srgb_decode_samplerparameterfv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.srgb_decode_samplerparameterf"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.srgb_decode_samplerparameteriv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.shader.srgb_decode_samplerparameteri"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.get_error.state.get_synciv"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.debug.negative_coverage.log.state.get_synciv"


# TODO: These tests fail on softpipe, someone should fix them and then remove these lines
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.builtin_functions.uniform.findLSBMinusOne.lowp_vertex"
FLIP_FLOPS="$FLIP_FLOPS dEQP-GLES31.functional.shaders.builtin_functions.uniform.findLSBMinusOne.highp_fragment"

for TEST_NAME in $FLIP_FLOPS; do
    sed -i "\:$TEST_NAME:d" $RESULTS_DIR/deqp_results.txt $PREVIOUS_RESULTS_DIR/deqp_results.txt
done

# Sort results files
sort -V $RESULTS_DIR/deqp_results.txt -o $RESULTS_DIR/deqp_results.txt
sort -V $PREVIOUS_RESULTS_DIR/deqp_results.txt -o $PREVIOUS_RESULTS_DIR/deqp_results.txt

# These warnings add too much variability
sed -i "s/QualityWarning/Pass/g" $RESULTS_DIR/deqp_results.txt $PREVIOUS_RESULTS_DIR/deqp_results.txt
sed -i "s/CompatibilityWarning/Pass/g" $RESULTS_DIR/deqp_results.txt $PREVIOUS_RESULTS_DIR/deqp_results.txt

diff -u $PREVIOUS_RESULTS_DIR/deqp_results.txt $RESULTS_DIR/deqp_results.txt 2>&1 > $RESULTS_DIR/deqp_diff.txt
if [ $? -ne 0 ]; then
   cat $RESULTS_DIR/deqp_diff.txt
   touch /virglrenderer/results/regressions_detected
else
   rm $RESULTS_DIR/deqp_diff.txt
fi


if [[ "x$ONLY_SOFTPIPE_PASS" != "xyes" ]] ; then
    mkdir -p $RESULTS_DIR/piglit

    PIGLIT_TESTS="-x glx"
    if [[ -z "$HOST_GL" ]]; then
        PIGLIT_TESTS="$PIGLIT_TESTS -t gles2 -t gles3"
    fi

    # Hits this assertion on i965:
    # compiler/brw_fs_visitor.cpp:444: void fs_visitor::emit_fb_writes(): Assertion `!prog_data->dual_src_blend || key->nr_color_regions == 1` failed
    PIGLIT_TESTS="$PIGLIT_TESTS -x arb_blend_func_extended-fbo-extended-blend-pattern_gles2"

    time piglit run --platform x11_egl \
         --jobs $NUM_JOBS \
         $PIGLIT_TESTS \
         gpu \
         $RESULTS_DIR/piglit

    piglit summary console $RESULTS_DIR/piglit | head -n -17 > $RESULTS_DIR/piglit/results.txt

    # TODO: These tests are not reliable when run on radeonsi, someone should fix them and then remove these lines
    FLIP_FLOPS=""
    FLIP_FLOPS="$FLIP_FLOPS spec/arb_framebuffer_srgb/blit renderbuffer srgb_to_linear downsample enabled clear"
    FLIP_FLOPS="$FLIP_FLOPS spec/arb_framebuffer_srgb/blit texture srgb_to_linear msaa enabled clear"
    FLIP_FLOPS="$FLIP_FLOPS spec/arb_shader_image_load_store/shader-mem-barrier/fragment shader/'volatile' qualifier memory barrier test/modulus="
    FLIP_FLOPS="$FLIP_FLOPS spec/arb_shader_image_load_store/shader-mem-barrier/fragment shader/'coherent' qualifier memory barrier test/modulus="
    FLIP_FLOPS="$FLIP_FLOPS spec/arb_shader_image_load_store/shader-mem-barrier/tessellation control shader/'volatile' qualifier memory barrier test/modulus="
    FLIP_FLOPS="$FLIP_FLOPS spec/arb_shader_image_load_store/shader-mem-barrier/tessellation control shader/'coherent' qualifier memory barrier test/modulus="
    FLIP_FLOPS="$FLIP_FLOPS spec/ext_transform_instanced/draw-auto instanced"
    for TEST_NAME in $FLIP_FLOPS; do
        sed -i "\:$TEST_NAME:d" $RESULTS_DIR/piglit/results.txt $PREVIOUS_RESULTS_DIR/piglit_results.txt
    done

    # This distinction adds too much variability
    sed -i "s/crash/fail/g" $RESULTS_DIR/piglit/results.txt $PREVIOUS_RESULTS_DIR/piglit_results.txt

    # Sort results files
    sort -V $RESULTS_DIR/piglit/results.txt -o $RESULTS_DIR/piglit/results.txt
    sort -V $PREVIOUS_RESULTS_DIR/piglit_results.txt -o $PREVIOUS_RESULTS_DIR/piglit_results.txt

    diff -u $PREVIOUS_RESULTS_DIR/piglit_results.txt $RESULTS_DIR/piglit/results.txt 2>&1 > $RESULTS_DIR/piglit_diff.txt
    if [ $? -ne 0 ]; then
        cat $RESULTS_DIR/piglit_diff.txt
        touch /virglrenderer/results/regressions_detected
    else
        rm $RESULTS_DIR/piglit_diff.txt
    fi

fi

killall virgl_test_server
