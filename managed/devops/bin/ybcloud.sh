#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -e
. "${BASH_SOURCE%/*}"/common.sh

activate_virtualenv

if [[ -d "$YB_INSTALLED_MODULES_DIR" ]]; then
  "$PYTHON_EXECUTABLE" "$yb_devops_home"/opscli/ybops/scripts/ybcloud.py "$@"
else
  "$PYTHON_EXECUTABLE" "$(which ybcloud.py)" "$@"
fi
