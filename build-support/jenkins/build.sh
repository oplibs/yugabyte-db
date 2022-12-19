#!/usr/bin/env bash

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
# This script is invoked from the Jenkins builds to build YB and run all the unit tests.
#
# Environment variables may be used to customize operation:
#   BUILD_TYPE: Default: debug
#     Maybe be one of asan|tsan|debug|release|coverage|lint
#
#   YB_BUILD_CPP
#   Default: 1
#     Build and test C++ code if this is set to 1.
#
#   YB_SKIP_BUILD
#   Default: 0
#     Skip building C++ and Java code, only run tests if this is set to 1 (useful for debugging).
#     This option is actually handled by yb_build.sh.
#
#   YB_BUILD_JAVA
#   Default: 1
#     Build and test java code if this is set to 1.
#
#   DONT_DELETE_BUILD_ROOT
#   Default: 0 (meaning build root will be deleted) on Jenkins, 1 (don't delete) locally.
#     Skip deleting BUILD_ROOT (useful for debugging).
#
#   YB_COMPILE_ONLY
#   Default: 0
#     Compile the code and build a package, but don't run tests.
#
#   YB_RUN_AFFECTED_TESTS_ONLY
#   Default: 0
#     Try to auto-detect the set of C++ tests to run for the current set of changes relative to
#     origin/master.

#
# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

echo "Build script ${BASH_SOURCE[0]} is running"

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}/../common-test-env.sh"

readonly COMMON_YB_BUILD_ARGS_FOR_CPP_BUILD=(
  --no-rebuild-thirdparty
  --skip-java
)

# -------------------------------------------------------------------------------------------------
# Functions

build_cpp_code() {
  # Save the source root just in case, but this should not be necessary as we will typically run
  # this function in a separate process in case it is building code in a non-standard location
  # (i.e. in a separate directory where we rollback the last commit for regression tracking).
  local old_yb_src_root=$YB_SRC_ROOT

  expect_num_args 1 "$@"
  set_yb_src_root "$1"

  heading "Building C++ code in $YB_SRC_ROOT."

  remote_opt=""
  if [[ ${YB_REMOTE_COMPILATION:-} == "1" ]]; then
    # This helps with our background script resizing the build cluster, because it looks at all
    # running build processes with the "--remote" option as of 08/2017.
    remote_opt="--remote"
  fi

  # Delegate the actual C++ build to the yb_build.sh script.
  #
  # We're explicitly disabling third-party rebuilding here as we've already built third-party
  # dependencies (or downloaded them, or picked an existing third-party directory) above.

  local yb_build_args=(
    "${COMMON_YB_BUILD_ARGS_FOR_CPP_BUILD[@]}"
    "${BUILD_TYPE}"
  )

  if using_ninja; then
    # TODO: remove this code when it becomes clear why CMake sometimes gets re-run.
    log "Building a dummy target to check if Ninja re-runs CMake (it should not)."
    # The "-d explain" option will make Ninja explain why it is building a particular target.
    (
      time "${YB_SRC_ROOT}/yb_build.sh" ${remote_opt} \
        --make-ninja-extra-args "-d explain" \
        --target dummy_target \
        "${yb_build_args[@]}"
    )
  fi

  time "$YB_SRC_ROOT/yb_build.sh" ${remote_opt} "${yb_build_args[@]}"

  log "Finished building C++ code (see timing information above)"

  remove_latest_symlink

  # Restore the old source root. See the comment at the top.
  set_yb_src_root "$old_yb_src_root"
}

# cleanup() {
#   if [[ -n ${BUILD_ROOT:-} && ${DONT_DELETE_BUILD_ROOT} == "0" ]]; then
#     log "Running the script to clean up build artifacts..."
#     "$YB_BUILD_SUPPORT_DIR/jenkins/post-build-clean.sh"
#   fi
# }

# =================================================================================================
# Main script
# =================================================================================================

log "Running with Bash version $BASH_VERSION"

cd "$YB_SRC_ROOT"
if ! "$YB_BUILD_SUPPORT_DIR/common-build-env-test.sh"; then
  fatal "Test of the common build environment failed, cannot proceed."
fi

log "Removing old JSON-based test report files"
(
  set -x
  find . -name "*_test_report.json" -exec rm -f '{}' \;
  rm -f test_results.json test_failures.json
)

activate_virtualenv
set_pythonpath

# We change YB_RUN_JAVA_TEST_METHODS_SEPARATELY in a subshell in a few places and that is OK.
# shellcheck disable=SC2031
export YB_RUN_JAVA_TEST_METHODS_SEPARATELY=1

export TSAN_OPTIONS=""

if is_mac; then
  # This is needed to make sure we're using Homebrew-installed CMake on Mac OS X.
  export PATH=/usr/local/bin:$PATH
fi

# gather core dumps
ulimit -c unlimited

detect_architecture

BUILD_TYPE=${BUILD_TYPE:-debug}
build_type=$BUILD_TYPE
normalize_build_type
readonly build_type

BUILD_TYPE=$build_type
readonly BUILD_TYPE
export BUILD_TYPE

export YB_USE_NINJA=1

set_cmake_build_type_and_compiler_type

if [[ ${YB_DOWNLOAD_THIRDPARTY:-auto} == "auto" ]]; then
  log "Setting YB_DOWNLOAD_THIRDPARTY=1 automatically"
  export YB_DOWNLOAD_THIRDPARTY=1
fi
log "YB_DOWNLOAD_THIRDPARTY=$YB_DOWNLOAD_THIRDPARTY"

# This is normally done in set_build_root, but we need to decide earlier because this is factored
# into the decision of whether to use LTO.
decide_whether_to_use_linuxbrew

if [[ -z ${YB_LINKING_TYPE:-} ]]; then
  if ! is_mac && [[
        ${YB_COMPILER_TYPE} =~ ^clang[0-9]+$ &&
        ${BUILD_TYPE} == "release"
      ]]; then
    export YB_LINKING_TYPE=full-lto
  else
    export YB_LINKING_TYPE=dynamic
  fi
  log "Automatically decided to set YB_LINKING_TYPE to ${YB_LINKING_TYPE} based on:" \
      "YB_COMPILER_TYPE=${YB_COMPILER_TYPE}," \
      "BUILD_TYPE=${BUILD_TYPE}," \
      "YB_USE_LINUXBREW=${YB_USE_LINUXBREW}," \
      "YB_LINUXBREW_DIR=${YB_LINUXBREW_DIR:-undefined}."
else
  log "YB_LINKING_TYPE is already set to ${YB_LINKING_TYPE}"
fi
log "YB_LINKING_TYPE=${YB_LINKING_TYPE}"
export YB_LINKING_TYPE

# -------------------------------------------------------------------------------------------------
# Build root setup and build directory cleanup
# -------------------------------------------------------------------------------------------------

# shellcheck disable=SC2119
set_build_root

set_common_test_paths

# As soon as we know build root, we need to do the necessary workspace cleanup.
if is_jenkins; then
  # Delete the build root by default on Jenkins.
  DONT_DELETE_BUILD_ROOT=${DONT_DELETE_BUILD_ROOT:-0}
else
  log "Not running on Jenkins, not deleting the build root by default."
  # Don't delete the build root by default.
  DONT_DELETE_BUILD_ROOT=${DONT_DELETE_BUILD_ROOT:-1}
fi

# Remove testing artifacts from the previous run before we do anything else. Otherwise, if we fail
# during the "build" step, Jenkins will archive the test logs from the previous run, thinking they
# came from this run, and confuse us when we look at the failed build.

build_root_deleted=false
if [[ ${DONT_DELETE_BUILD_ROOT} == "0" ]]; then
  if [[ -L ${BUILD_ROOT} ]]; then
    # If the build root is a symlink, we have to find out what it is pointing to and delete that
    # directory as well.
    build_root_real_path=$( readlink "${BUILD_ROOT}" )
    log "BUILD_ROOT ('${BUILD_ROOT}') is a symlink to '${build_root_real_path}'"
    rm -rf "${build_root_real_path}"
    unlink "${BUILD_ROOT}"
    build_root_deleted=true
  else
    log "Deleting BUILD_ROOT ('$BUILD_ROOT')."
    ( set -x; rm -rf "$BUILD_ROOT" )
    build_root_deleted=true
  fi
fi

if [[ ${build_root_deleted} == "false" ]]; then
  log "Skipped deleting BUILD_ROOT ('${BUILD_ROOT}'), only deleting ${YB_TEST_LOG_ROOT_DIR}."
  rm -rf "${YB_TEST_LOG_ROOT_DIR}"
fi

if is_jenkins; then
  if [[ ${build_root_deleted} == "true" ]]; then
    log "Deleting yb-test-logs from all subdirectories of ${YB_BUILD_PARENT_DIR} so that Jenkins " \
        "does not get confused with old JUnit-style XML files."
    ( set -x; rm -rf "$YB_BUILD_PARENT_DIR"/*/yb-test-logs )

    log "Deleting old packages from '${YB_BUILD_PARENT_DIR}'"
    ( set -x; rm -rf "${YB_BUILD_PARENT_DIR}/yugabyte-"*"-$build_type-"*".tar.gz" )
  else
    log "No need to delete yb-test-logs or old packages, build root already deleted."
  fi
fi

mkdir_safe "${BUILD_ROOT}"

readonly BUILD_ROOT
export BUILD_ROOT

# -------------------------------------------------------------------------------------------------
# End of build root setup and build directory cleanup
# -------------------------------------------------------------------------------------------------

# We need to set this prior to the first invocation of yb_build.sh.
export YB_SKIP_FINAL_LTO_LINK=1

"${YB_SRC_ROOT}/yb_build.sh" --cmake-unit-tests

find_or_download_ysql_snapshots
find_or_download_thirdparty
validate_thirdparty_dir
detect_toolchain
log_thirdparty_and_toolchain_details
find_make_or_ninja_and_update_cmake_opts

log "YB_USE_NINJA=$YB_USE_NINJA"
log "YB_NINJA_PATH=${YB_NINJA_PATH:-undefined}"

set_java_home

export YB_DISABLE_LATEST_SYMLINK=1
remove_latest_symlink

if is_jenkins; then
  log "Running on Jenkins, will re-create the Python virtualenv"
  # YB_RECREATE_VIRTUALENV is used in common-build-env.sh.
  # shellcheck disable=SC2034
  YB_RECREATE_VIRTUALENV=1
fi

log "Running with PATH: ${PATH}"

set +e
for python_command in python python2 python2.7 python3; do
  log "Location of $python_command: $( which "$python_command" )"
done
set -e

log "Running Python tests"
time run_python_tests
log "Finished running Python tests (see timing information above)"

log "Running a light-weight lint script on our Java code"
time lint_java_code
log "Finished running a light-weight lint script on the Java code"

# TODO: deduplicate this with similar logic in yb-jenkins-build.sh.
YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
YB_BUILD_CPP=${YB_BUILD_CPP:-1}

if [[ -z ${YB_RUN_AFFECTED_TESTS_ONLY:-} ]] && is_jenkins_phabricator_build; then
  log "YB_RUN_AFFECTED_TESTS_ONLY is not set, and this is a Jenkins Phabricator test." \
      "Setting YB_RUN_AFFECTED_TESTS_ONLY=1 automatically."
  export YB_RUN_AFFECTED_TESTS_ONLY=1
fi
export YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY:-0}
log "YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY}"

export YB_SKIP_BUILD=${YB_SKIP_BUILD:-0}
if [[ ${YB_SKIP_BUILD} == "1" ]]; then
  export NO_REBUILD_THIRDPARTY=1
fi

YB_SKIP_CPP_COMPILATION=${YB_SKIP_CPP_COMPILATION:-0}
YB_COMPILE_ONLY=${YB_COMPILE_ONLY:-0}

CTEST_OUTPUT_PATH="${BUILD_ROOT}"/ctest.log
CTEST_FULL_OUTPUT_PATH="${BUILD_ROOT}"/ctest-full.log

TEST_LOG_DIR="${BUILD_ROOT}/test-logs"

# If we're running inside Jenkins (the BUILD_ID is set), then install an exit handler which will
# clean up all of our build results.
if is_jenkins; then
  trap cleanup EXIT
fi

configure_remote_compilation

export NO_REBUILD_THIRDPARTY=1

THIRDPARTY_BIN=$YB_SRC_ROOT/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

# Configure the build
#

cd "$BUILD_ROOT"

if [[ $YB_RUN_AFFECTED_TESTS_ONLY == "1" ]]; then
  (
    set -x
    # Remove the compilation command file, even if we have not deleted the build root.
    rm -f "$BUILD_ROOT/compile_commands.json"
  )
fi

if [[ ${YB_ENABLE_STATIC_ANALYZER:-auto} == "auto" ]]; then
  if is_clang &&
     is_linux &&
     [[ $build_type =~ ^(debug|release)$ ]] &&
     is_jenkins_master_build
  then
    if true; then
      log "Not enabling Clang static analyzer. Will enable in clang/Linux builds in the future."
    else
      # TODO: re-enable this when we have time to sift through analyzer warnings.
      export YB_ENABLE_STATIC_ANALYZER=1
      log "Enabling Clang static analyzer (this is a clang Linux $build_type build)"
    fi
  else
    log "Not enabling Clang static analyzer (this is not a clang Linux debug/release build):" \
        "OSTYPE=$OSTYPE, YB_COMPILER_TYPE=$YB_COMPILER_TYPE, build_type=$build_type"
  fi
else
  log "YB_ENABLE_STATIC_ANALYZER is already set to $YB_ENABLE_STATIC_ANALYZER," \
      "not setting automatically"
fi

# We have a retry loop around CMake because it sometimes fails due to NFS unavailability.
declare -i -r MAX_CMAKE_RETRIES=3
declare -i cmake_attempt_index=1
while true; do
  if "${YB_SRC_ROOT}/yb_build.sh" "${BUILD_TYPE}" --cmake-only --no-remote; then
    log "CMake succeeded after attempt $cmake_attempt_index"
    break
  fi
  if [[ $cmake_attempt_index -eq ${MAX_CMAKE_RETRIES} ]]; then
    fatal "CMake failed after ${MAX_CMAKE_RETRIES} attempts, giving up."
  fi
  heading "CMake failed at attempt $cmake_attempt_index, re-trying"
  (( cmake_attempt_index+=1 ))
done

# Only enable test core dumps for certain build types.
if [[ ${BUILD_TYPE} != "asan" ]]; then
  # TODO: actually make this take effect. The issue is that we might not be able to set ulimit
  # unless the OS configuration enables us to.
  export YB_TEST_ULIMIT_CORE=unlimited
fi

detect_num_cpus

declare -i EXIT_STATUS=0

set +e
if [[ -d /tmp/yb-port-locks ]]; then
  # Allow other users to also run minicluster tests on this machine.
  chmod a+rwx /tmp/yb-port-locks
fi
set -e

FAILURES=""

if [[ ${YB_BUILD_CPP} == "1" ]] && ! which ctest >/dev/null; then
  fatal "ctest not found, won't be able to run C++ tests"
fi

export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=1

# -------------------------------------------------------------------------------------------------
# Build C++ code regardless of YB_BUILD_CPP, because we'll also need it for Java tests.
# -------------------------------------------------------------------------------------------------

heading "Building C++ code"

build_cpp_code "$YB_SRC_ROOT"

log "Disk usage after C++ build:"
show_disk_usage

# We can grep for this line in the log to determine the stage of the build job.
log "ALL OF YUGABYTE C++ BUILD FINISHED"

# -------------------------------------------------------------------------------------------------
# End of the C++ code build, except maybe the final LTO linking step.
# -------------------------------------------------------------------------------------------------

# -------------------------------------------------------------------------------------------------
# Running initdb
# -------------------------------------------------------------------------------------------------

export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=0

if [[ ${BUILD_TYPE} != "tsan" ]]; then
  declare -i initdb_attempt_index=1
  declare -i -r MAX_INITDB_ATTEMPTS=3

  while [[ $initdb_attempt_index -le ${MAX_INITDB_ATTEMPTS} ]]; do
    log "Creating initial system catalog snapshot (attempt $initdb_attempt_index)"
    if ! time "${YB_SRC_ROOT}/yb_build.sh" "${BUILD_TYPE}" initdb --skip-java; then
      initdb_err_msg="Failed to create initial sys catalog snapshot at "
      initdb_err_msg+="attempt ${initdb_attempt_index}"
      log "${initdb_err_msg}. PostgreSQL tests may take longer."
      FAILURES+="$initdb_err_msg"$'\n'
      EXIT_STATUS=1
    else
      log "Successfully created initial system catalog snapshot at attempt ${initdb_attempt_index}."
      break
    fi
    (( initdb_attempt_index+=1 ))
  done
  if [[ ${initdb_attempt_index} -gt ${MAX_INITDB_ATTEMPTS} ]]; then
    fatal "Failed to run create initial sys catalog snapshot after ${MAX_INITDB_ATTEMPTS} attempts."
  fi
fi

# -------------------------------------------------------------------------------------------------
# Dependency graph analysis allowing to determine what tests to run.
# -------------------------------------------------------------------------------------------------

if [[ $YB_RUN_AFFECTED_TESTS_ONLY == "1" ]]; then
  if ! ( set -x
         "${YB_SRC_ROOT}/python/yb/dependency_graph.py" \
           --build-root "${BUILD_ROOT}" \
           self-test \
           --rebuild-graph ); then
    # Trying to diagnose this error:
    # https://gist.githubusercontent.com/mbautin/c5c6f14714f7655c10620d8e658e1f5b/raw
    log "dependency_graph.py failed, listing all pb.{h,cc} files in the build directory"
    ( set -x; find "$BUILD_ROOT" -name "*.pb.h" -or -name "*.pb.cc" )
    fatal "Dependency graph construction failed"
  fi
fi

# Save the current HEAD commit in case we build Java below and add a new commit. This is used for
# the following purposes:
# - So we can upload the release under the correct commit, from Jenkins, to then be picked up from
#   itest, from the snapshots bucket.
# - For picking up the changeset corresponding the the current diff being tested and detecting what
#   tests to run in Phabricator builds. If we just diff with origin/master, we'll always pick up
#   pom.xml changes we've just made, forcing us to always run Java tests.
current_git_commit=$(git rev-parse HEAD)

# -------------------------------------------------------------------------------------------------
# Final LTO linking
# -------------------------------------------------------------------------------------------------

export YB_SKIP_FINAL_LTO_LINK=0
if [[ ${YB_LINKING_TYPE} == *-lto ]]; then
  yb_build_cmd_line_for_lto=(
    "${YB_SRC_ROOT}/yb_build.sh"
    "${BUILD_TYPE}" --skip-java --force-run-cmake
  )

  if [[ $( grep -E 'MemTotal: .* kB' /proc/meminfo ) =~ ^.*\ ([0-9]+)\ .*$ ]]; then
    total_mem_kb=${BASH_REMATCH[1]}
    # LTO linking uses about 12.5 GB for building one binary. Try to avoid OOM.
    yb_build_parallelism_for_lto=$(( total_mem_kb / (13 * 1024 * 1024) ))
    if [[ ${yb_build_parallelism_for_lto} -lt 1 ]]; then
      yb_build_parallelism_for_lto=1
    fi
    log "Total memory size: ${total_mem_kb} KB," \
        "using LTO linking parallelism ${yb_build_parallelism_for_lto}."
  else
    log "Warning: could not determine total amount of memory, using parallelism of 1 for LTO."
    yb_build_parallelism_for_lto=1
  fi
  yb_build_cmd_line_for_lto+=( "-j${yb_build_parallelism_for_lto}" )

  log "Performing final LTO linking"
  ( set -x; "${yb_build_cmd_line_for_lto[@]}" )
fi

# -------------------------------------------------------------------------------------------------
# Java build
# -------------------------------------------------------------------------------------------------

export YB_MVN_LOCAL_REPO=$BUILD_ROOT/m2_repository

java_build_failed=false
if [[ ${YB_BUILD_JAVA} == "1" && ${YB_SKIP_BUILD} != "1" ]]; then
  set_mvn_parameters

  heading "Building Java code..."
  if [[ -n ${JAVA_HOME:-} ]]; then
    export PATH=${JAVA_HOME}/bin:${PATH}
  fi

  build_yb_java_code_in_all_dirs clean

  heading "Java 'clean' build is complete, will now actually build Java code"

  for java_project_dir in "${yb_java_project_dirs[@]}"; do
    pushd "${java_project_dir}"
    heading "Building Java code in directory '${java_project_dir}'"
    if ! build_yb_java_code_with_retries -DskipTests clean install; then
      EXIT_STATUS=1
      FAILURES+="Java build failed in directory '${java_project_dir}'"$'\n'
      java_build_failed=true
    else
      log "Java code build in directory '${java_project_dir}' SUCCEEDED"
    fi
    popd
  done

  if [[ ${java_build_failed} == "true" ]]; then
    fatal "Java build failed, stopping here."
  fi

  heading "Running a test locally to force Maven to download all test-time dependencies"
  (
    cd "${YB_SRC_ROOT}/java"
    build_yb_java_code test \
                       -Dtest=org.yb.client.TestTestUtils#testDummy \
                       "${MVN_OPTS_TO_DOWNLOAD_ALL_DEPS[@]}"
  )
  heading "Finished running a test locally to force Maven to download all test-time dependencies"

  # Tell gen_version_info.py to store the Git SHA1 of the commit really present in the code
  # being built, not our temporary commit to update pom.xml files.
  get_current_git_sha1
  export YB_VERSION_INFO_GIT_SHA1=$current_git_sha1

  collect_java_tests

  log "Finished building Java code (see timing information above)"
fi
