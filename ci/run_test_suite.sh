#!/bin/bash

trap "{ rm -f $TMP_TEST_FILE; }" EXIT

# Setup paths and import util functions
. $(dirname $(readlink -f "$0"))/util.sh

TESTS=""
BACKENDS=""
NUM_THREADS=$(expr $(expr $(nproc) + 2) / 3)

COMPARE_BACKENDS=0
COMPARE_PREVIOUS=0
USE_HOST_GLES=0
TEST_APP="deqp"
VERIFY_UNRELIABLE_TESTS=0

parse_input()
{
   while  [ -n "$1" ]; do
      case $1 in

      -u|--unreliable)
         VERIFY_UNRELIABLE_TESTS=1
         ;;

      -a|--all-backends)
         BACKENDS=""
         BACKENDS="$BACKENDS vtest-softpipe"
         BACKENDS="$BACKENDS vtest-llvmpipe"
         BACKENDS="$BACKENDS vtest-gpu"
         BACKENDS="$BACKENDS softpipe"
         BACKENDS="$BACKENDS llvmpipe"
         BACKENDS="$BACKENDS gpu"
         ;;

   	-t|--test)
         TEST_NAME="$2"
         shift
         if [ -z "$TMP_TEST_FILE" ]; then
            TMP_TEST_FILE=$(mktemp /tmp/deqp_test.XXXXXX)
            TESTS="$TESTS custom"
         fi
         echo "$TEST_NAME" >> "$TMP_TEST_FILE"
         ;;

   	-v|--vtest)
         BACKENDS=""
         BACKENDS="$BACKENDS vtest-softpipe"
         BACKENDS="$BACKENDS vtest-llvmpipe"
         BACKENDS="$BACKENDS vtest-gpu"
         ;;

      --host-gles)
         USE_HOST_GLES=1
         ;;

      --host-gl)
         USE_HOST_GLES=0
         ;;

      -b|--backend)
         NEW_BACKEND="$2"
         shift
         BACKENDS="$BACKENDS $NEW_BACKEND"
         ;;

      -j|--threads)
         NUM_THREADS=$2
         shift
         ;;

   	--gles2)
         TESTS="$TESTS gles2"
   	   ;;

   	--gles3)
         TESTS="$TESTS gles3"
   	   ;;

   	--gles31)
         TESTS="$TESTS gles31"
   	   ;;

      -d|--deqp)
         TEST_APP="deqp"
         ;;

      -p|--piglit)
         TEST_APP="piglit"
         ;;

      -cp|--compare-previous)
         # Compare results against previous runs
         COMPARE_PREVIOUS=1
         ;;

   	*)
   	   echo "Unknown flag $1"
   	   exit 1
      esac
      shift
   done

   if [[ -z $BACKENDS ]]; then
      BACKENDS="gpu"
   fi

   # These two options are incompatible, and one has to be disabled
   if [ $VERIFY_UNRELIABLE_TESTS -ne 0 ]; then
      COMPARE_PREVIOUS=0
   fi
}

compare_previous()
{
   if [ ! -f $PREVIOUS_RESULTS_DIR/results.txt ]; then
      return 2
   fi

   # The wildcard here will match ""/"_radeonsi"/"_i915"/etc
   # which enables us to ignore tests by driver
   # BUT: We're not able to get the driver name and
   # use it to disambiguate between HW-based drivers
   IGNORE_TESTS=$(cat $IGNORE_TESTS_FILE 2>/dev/null)

   # Avoid introducing changes while doing this comparison
   TMP_RESULTS=$(mktemp /tmp/virgl_ci_results.XXXXXX)
   TMP_PREV_RESULTS=$(mktemp /tmp/virgl_ci_previous_results.XXXXXX)
   cp $RESULTS_DIR/results.txt $TMP_RESULTS
   cp $PREVIOUS_RESULTS_DIR/results.txt $TMP_PREV_RESULTS

   for TEST in $IGNORE_TESTS; do
      sed -i "\:$TEST:d" $TMP_RESULTS $TMP_PREV_RESULTS
   done

   if [ "$TEST_APP" = "piglit" ]; then
      # This distinction adds too much variability
      sed -i "s/crash/fail/g" $TMP_RESULTS $TMP_PREV_RESULTS
   elif [ "$TEST_APP" = "deqp" ]; then
      # This distinction adds too much variability
      sed -i "s/QualityWarning/Pass/g" $TMP_RESULTS $TMP_PREV_RESULTS
      sed -i "s/CompatibilityWarning/Pass/g" $TMP_RESULTS $TMP_PREV_RESULTS
   fi

   # Sort results files
   sort -V $TMP_RESULTS -o $TMP_RESULTS
   sort -V $TMP_PREV_RESULTS -o $TMP_PREV_RESULTS

   diff -u $TMP_PREV_RESULTS $TMP_RESULTS 2>&1 > $RESULTS_DIR/regression_diff.txt
   if [ $? -ne 0 ]; then
      touch $VIRGL_PATH/results/regressions_detected
      return 1
   else
      rm -rf $RESULTS_DIR/regression_diff.txt
      return 0
   fi
}

interpret_results()
{
   PASSED_TESTS="$1"
   TOTAL_TESTS="$2"

   if [ $COMPARE_PREVIOUS -ne 0 ]; then
      compare_previous
      case $? in
         0)
         echo "Pass - matches previous results"
         return 0
         ;;

         1)
         echo "Fail - diff against previous results: $RESULTS_DIR/regression_diff.txt"
         return 1
         ;;

         2)
         echo "Pass - no previous results, but passed $PASSED_TESTS/$TOTAL_TESTS tests"
         return 0
         ;;

         *)
         echo "BUG!"
         return 1
         ;;
      esac
   else
      if [ $PASSED_TESTS -eq $TOTAL_TESTS ] && [ $TOTAL_TESTS -ne 0 ]; then
         echo "Pass - passed $PASSED_TESTS/$TOTAL_TESTS tests"
         return 0
      else
         echo "Fail - passed $PASSED_TESTS/$TOTAL_TESTS tests: $RESULTS_DIR/results.txt"
         return 1
      fi
   fi
}

run_vtest_server()
{
   (
   if [ $USE_HOST_GLES -ne 0 ]; then
      VTEST_USE_EGL_SURFACELESS=1 \
      VTEST_USE_GLES=1 \
      virgl_test_server &>$VTEST_LOG_FILE &
   else
      VTEST_USE_EGL_SURFACELESS=1 \
      virgl_test_server &>$VTEST_LOG_FILE &
   fi
   )
}

run_test_suite()
{
   local BACKEND="$1"
   local TEST_NAME="$2"

   local RET=0

#   echo "run_test_suite() OUTPUT_PATH: $OUTPUT_PATH"
#   echo "run_test_suite() LOG_FILE: $LOG_FILE"
#   echo "run_test_suite() RESULTS_FILE: $RESULTS_FILE"

   if [ $VERIFY_UNRELIABLE_TESTS -eq 1 ]; then
      UNRELIABLE_STRING="unreliable "
   fi

   if [[ $BACKEND == *"vtest"* ]]; then
      printf "Running ${UNRELIABLE_STRING}$TEST_APP-$TEST_NAME on vtest-$DRIVER_NAME: "
   else
      printf "Running ${UNRELIABLE_STRING}$TEST_APP-$TEST_NAME on $DRIVER_NAME: "
   fi

   if [ "$TEST_APP" = "piglit" ]; then

      # Don't run GLX tests
      PIGLIT_TESTS=" -x glx"

      if [ $VERIFY_UNRELIABLE_TESTS -eq 1 ]; then
         UNRELIABLE_TESTS=$(cat $IGNORE_TESTS_FILE 2>/dev/null)
         if [[ -z $UNRELIABLE_TESTS ]]; then
            echo "Ignore - No unreliable tests found"
            return 0
         fi

         for UNRELIABLE_TEST in $UNRELIABLE_TESTS; do
            PIGLIT_TESTS="$PIGLIT_TESTS -t $UNRELIABLE_TEST"
         done
      else
         PIGLIT_TESTS="$PIGLIT_TESTS -t $TEST_NAME"
      fi

      piglit run --platform x11_egl \
         -l verbose \
         $PIGLIT_TESTS \
         gpu \
         /tmp/  &> $LOG_FILE

      piglit summary console /tmp/ | grep -B 999999 "summary:" | grep -v "summary:" > $RESULTS_FILE

      TOTAL_TESTS=$(cat $RESULTS_FILE | wc -l)
      PASSED_TESTS=$(grep " pass" $RESULTS_FILE | wc -l)

      interpret_results $PASSED_TESTS $TOTAL_TESTS
      RET=$?

   elif [ "$TEST_APP" = "deqp" ]; then

      if [ $VERIFY_UNRELIABLE_TESTS -eq 1 ]; then
         TEST_FILE="$IGNORE_TESTS_FILE"
         if [ ! -f $TEST_FILE ]; then
            echo "Ignore - No unreliable tests found"
            return 0
         fi
      fi

      deqp  \
         --cts-build-dir $CTS_PATH/build \
         --test-names-file "$TEST_FILE" \
         --results-file "$RESULTS_FILE" \
         --threads $NUM_THREADS &> $LOG_FILE

#      echo "$(which dEQP): Returned $?"
#      echo "ls -la CTS_PATH/build: $CTS_PATH/build - $(ls -la $CTS_PATH/build)"
#      echo "ls -la TEST_FILE: $TEST_FILE - $(ls -la $TEST_FILE)"
#      echo "ls -la RESULTS_FILE: $RESULTS_FILE - $(ls -la $RESULTS_FILE)"
#      echo "LOG_FILE: $LOG_FILE - $(ls -la $LOG_FILE)"

      # Remove header
      sed -i "/#/d" $RESULTS_FILE

      # Sort results file to make diffs easier to read
      sort -V $RESULTS_FILE -o $RESULTS_FILE

      TOTAL_TESTS=$(cat $RESULTS_FILE | wc -l)
      PASSED_TESTS=$(grep " Pass" $RESULTS_FILE | wc -l)

      interpret_results "$PASSED_TESTS" "$TOTAL_TESTS"
      RET=$?

   else
      echo "Invalid test-application supplied: \"$TEST_APP\""
      exit 1
   fi

   return $RET
}

create_result_dir()
{
   if [[ -n $GALLIUM_DRIVER ]]; then
      HOST_DRIVER="_${GALLIUM_DRIVER}"
   elif [[ -n $DRIVER_NAME ]]; then
      HOST_DRIVER="_${DRIVER_NAME}"
   fi

   if [ $USE_HOST_GLES -eq 0 ]; then
      HOST_GL="gl"
   else
      HOST_GL="es"
   fi

   TEST_PATH=${HOST_GL}_host${HOST_DRIVER}/${TEST_APP}_${TEST_NAME}
   RESULTS_DIR=$VIRGL_PATH/results/${TEST_PATH}
   PREVIOUS_RESULTS_DIR=$VIRGL_PATH/ci/previous_results/${TEST_PATH}
   IGNORE_TESTS_FILE=$PREVIOUS_RESULTS_DIR/ignore_tests.txt

   mkdir -p "$RESULTS_DIR"

   export OUTPUT_PATH="${RESULTS_DIR}"
   export RESULTS_FILE="${OUTPUT_PATH}/results.txt"
   export LOG_FILE="${OUTPUT_PATH}/log.txt"
   export VTEST_LOG_FILE="${OUTPUT_PATH}/vtest_log.txt"
}

run_test_on_backends()
{
   local BACKENDS="$1"
   local TEST_NAME="$2"
   local TEST_FILE="$3"
   local RET=0

#   echo "run_test_on_backends() BACKENDS: $BACKENDS"
#   echo "run_test_on_backends() TEST_NAME: $TEST_NAME"
#   echo "run_test_on_backends() TEST_FILE: $TEST_FILE"

   for BACKEND in $BACKENDS; do
         unset DRIVER_NAME
         unset GALLIUM_DRIVER
         unset GALLIVM_PERF
         unset LIBGL_ALWAYS_SOFTWARE
         unset VTEST_USE_EGL_SURFACELESS

         # If the custom test is being run, we're probably debugging
         if [ "$TEST_NAME" = "custom" ]; then
            export MESA_DEBUG=1
         fi

         case $BACKEND in
            vtest-softpipe)
               export LIBGL_ALWAYS_SOFTWARE=1
               export GALLIUM_DRIVER=softpipe
               export DRIVER_NAME=$GALLIUM_DRIVER
               ;;

            vtest-llvmpipe)
               export LIBGL_ALWAYS_SOFTWARE=1
               export GALLIUM_DRIVER=llvmpipe
               export DRIVER_NAME=$GALLIUM_DRIVER
               ;;

            softpipe)
               export LIBGL_ALWAYS_SOFTWARE=1
               export GALLIUM_DRIVER=softpipe
               export DRIVER_NAME=$GALLIUM_DRIVER
               ;;

            llvmpipe)
               export GALLIVM_PERF=nopt,no_filter_hacks
               export LIBGL_ALWAYS_SOFTWARE=1
               export GALLIUM_DRIVER=llvmpipe
               export DRIVER_NAME=$GALLIUM_DRIVER
               ;;

            vtest-gpu|gpu)
               DEVICE_NAME=$(basename /dev/dri/renderD128)
               export DRIVER_NAME="$(basename `readlink /sys/class/drm/${DEVICE_NAME}/device/driver`)"
               ;;

            *)
               DEVICE_NAME=$(basename /dev/dri/renderD128)
               export DRIVER_NAME="$(basename `readlink /sys/class/drm/${DEVICE_NAME}/device/driver`)"

               if  [ "$BACKEND" != "$DRIVER_NAME" ]; then
                  echo "Invalid backend: $BACKEND"
                  exit 1
               fi
               ;;
         esac

         # This case statement is broken into two parts
         # because for the second part the LOG_FILE has
         # declared, which is needed to redirect FDs
         create_result_dir

         case $BACKEND in
            vtest-*)
               run_vtest_server
               export GALLIUM_DRIVER=virpipe
               ;;

            *)
               ;;
         esac

         run_test_suite "$BACKEND" "$TEST_NAME"

         if [ $? -ne 0 ]; then
            RET=1
         fi

         killall -q virgl_test_server
   done

   return $RET
}

run_all_tests()
{
   local BACKENDS=$1
   local TESTS=$2
   local RET=0

   if [ $USE_HOST_GLES -eq 0 ]; then
      echo "Running test(s) on GL Host"
      echo "--------------------------"
   else
      echo "Running test(s) on GLES Host"
      echo "----------------------------"
   fi
#   echo "run_all_tests() BACKENDS: $BACKENDS"
#   echo "run_all_tests() TESTS: $TESTS"


   for TEST in $TESTS; do
      case $TEST in
      custom)
         TEST_NAME="custom"
         TEST_FILE="$TMP_TEST_FILE"
         ;;
      gles2)
         TEST_NAME="gles2"
         TEST_FILE="$CTS_PATH/android/cts/master/gles2-master.txt"
   	   ;;

   	gles3)
         TEST_NAME="gles3"
         TEST_FILE="$CTS_PATH/android/cts/master/gles3-master.txt"
   	   ;;

   	gles31)
         TEST_NAME="gles31"
         TEST_FILE="$CTS_PATH/android/cts/master/gles31-master.txt"
   	   ;;

      *)
         echo "Invalid test: $TEST"
         exit 1
         ;;
      esac

      run_test_on_backends "$BACKENDS" "$TEST_NAME" "$TEST_FILE"

      if [ $? -ne 0 ]; then
         RET=1
      fi
   done

   exit $RET
}

setup()
{
   Xvfb :0 -screen 0 1024x768x24 &>/dev/null &
   export DISPLAY=:0
   sleep 2
}

setup
parse_input $@
run_all_tests "$BACKENDS" "$TESTS"