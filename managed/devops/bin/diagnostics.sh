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

activate_virtualenv --with-system-python-path
cd "$yb_devops_home"

python $(which ybcloud.py) aws diagnostics "$@"
