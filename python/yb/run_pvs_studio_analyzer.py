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

"""
Runs the PVS Studio static analyzer on YugabyteDB's C/C++ code.
"""

import argparse
import os
import subprocess
from subprocess import CalledProcessError
import logging
import multiprocessing
from overrides import overrides

from yugabyte_pycommon import mkdir_p  # type: ignore
from yb.common_util import YB_SRC_ROOT, find_executable, rm_rf, check_call_and_log
from yb.tool_base import YbBuildToolBase
from yb.compile_commands import (
    get_compile_commands_file_path, COMBINED_RAW_DIR_NAME, filter_compile_commands)


PVS_ANALYZER_EXIT_CODE_DETAILS = {
    0: "Analysis was successfully completed, no issues were found in the source code",
    1: "Preprocessing failed on some file(s)",
    2: "Indicates that analyzer license will expire in less than a month",
    3: "Analysis was interrupted",
    4: "Error (crash) during analysis of some source file(s)",
    5: "Indicates that analyzer license has expired",
    6: "License expiration warning suppression flag was used with non-expiring license"
}


class PvsStudioAnalyzerTool(YbBuildToolBase):
    def __init__(self) -> None:
        super().__init__()

    @overrides
    def run_impl(self) -> None:
        self.run_pvs_analyzer()

    @overrides
    def add_command_line_args(self) -> None:
        self.arg_parser.add_argument(
            '--file_name_regex',
            help='Regular expression of the source file names to analyze')

    def run_pvs_analyzer(self) -> None:
        pvs_config_path = os.path.join(self.args.build_root, 'PVS-Studio.cfg')

        rules_config_path = os.path.join(YB_SRC_ROOT, 'yugabytedb.pvsconfig')
        if not os.path.exists(rules_config_path):
            raise IOError(
                "PVS Studio rules configuration file does not exist: %s" % rules_config_path)

        with open(pvs_config_path, 'w') as pvs_config_file:
            pvs_config_file.write(
                '# This file was automatically generated by %s.\n'
                'rules-config=%s\n' % (
                    __file__,
                    rules_config_path))

        pvs_output_dir = os.path.join(self.args.build_root, 'pvs_output')
        mkdir_p(pvs_output_dir)
        pvs_log_path = os.path.join(pvs_output_dir, 'pvs_results.log')
        if os.path.exists(pvs_log_path):
            logging.info("Removing existing file %s", pvs_log_path)
            os.remove(pvs_log_path)

        combined_raw_compile_commands_path = get_compile_commands_file_path(
            self.args.build_root, COMBINED_RAW_DIR_NAME)

        if not os.path.exists(combined_raw_compile_commands_path):
            raise IOError("Raw compilation commands file does not exist: %s" %
                          combined_raw_compile_commands_path)

        if self.args.file_name_regex:
            compile_commands_path = os.path.join(
                self.args.build_root, 'raw_compile_commands_filtered_for_analyze.json')
            filter_compile_commands(
                combined_raw_compile_commands_path,
                compile_commands_path,
                self.args.file_name_regex)
        else:
            compile_commands_path = combined_raw_compile_commands_path

        if not os.path.exists(compile_commands_path):
            raise IOError("Compilation commands file does not exist: %s" %
                          compile_commands_path)

        pvs_studio_analyzer_executable = find_executable('pvs-studio-analyzer', must_find=True)
        assert pvs_studio_analyzer_executable is not None

        plog_converter_executable = find_executable('plog-converter', must_find=True)
        assert plog_converter_executable is not None

        analyzer_cmd_line = [
            pvs_studio_analyzer_executable,
            'analyze',
            '--cfg',
            pvs_config_path,
            '--file',
            compile_commands_path,
            '--output-file',
            pvs_log_path,
            '-j',
            str(multiprocessing.cpu_count()),
            '--disableLicenseExpirationCheck'
        ]

        analyzer_exit_code = 0
        try:
            check_call_and_log(analyzer_cmd_line)
        except CalledProcessError as analyzer_error:
            analyzer_exit_code = analyzer_error.returncode
            if analyzer_exit_code in PVS_ANALYZER_EXIT_CODE_DETAILS:
                logging.info(
                    "Details for PVS Studio Analyzer return code %d: %s",
                    analyzer_exit_code,
                    PVS_ANALYZER_EXIT_CODE_DETAILS[analyzer_exit_code])

        if not os.path.exists(pvs_log_path):
            raise IOError(
                "PVS Studio Analyzer failed to generate output file at %s", pvs_log_path)
        if analyzer_exit_code not in PVS_ANALYZER_EXIT_CODE_DETAILS:
            raise IOError("Unrecognized PVS Studio Analyzer exit code: %s", analyzer_exit_code)

        pvs_output_path = os.path.join(pvs_output_dir, 'pvs_tasks.csv')
        log_converter_cmd_line_csv = [
            plog_converter_executable,
            '--renderTypes',
            'tasklist',
            '--output',
            pvs_output_path,
            pvs_log_path
        ]
        check_call_and_log(log_converter_cmd_line_csv)

        html_output_dir = os.path.join(pvs_output_dir, 'pvs_html')
        if os.path.exists(html_output_dir):
            logging.info("Deleting the existing directory %s", html_output_dir)
            rm_rf(html_output_dir)
        log_converter_cmd_line_html = [
            plog_converter_executable,
            '--renderTypes',
            'fullhtml',
            '--srcRoot',
            YB_SRC_ROOT,
            '--output',
            html_output_dir,
            pvs_log_path
        ]
        check_call_and_log(log_converter_cmd_line_html)


def main() -> None:
    PvsStudioAnalyzerTool().run()


if __name__ == '__main__':
    main()
