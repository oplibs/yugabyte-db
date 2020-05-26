#!/usr/bin/env python3

# Copyright (c) YugaByte, Inc.
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

# Split a long line that is assumed to be a command line into multiple lines, so that the result
# could still be pasted into a terminal. The max line length could be approximate.

import sys

line_length = 0
buffer = ''
for line in sys.stdin:
    for c in line.rstrip():
        if c.isspace() and line_length > 80:
            print(buffer + c + '\\\\')
            buffer = ''
            line_length = 0
        else:
            buffer += c
            line_length += 1

if buffer:
    sys.stdout.write(buffer)
