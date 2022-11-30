# Copyright (c) Yugabyte, Inc.

import argparse
import os
import sys
import logging

from typing import Dict, List, Any, Union, Optional

from yb.common_util import set_env_vars_from_build_root, init_logging

from overrides import overrides, EnforceOverrides


class YbBuildToolBase(EnforceOverrides):
    """
    A base class for command-line tools that are part of YugabyteDB build.
    """
    arg_parser_created: bool
    arg_parser: argparse.ArgumentParser
    args: argparse.Namespace

    def get_description(self) -> Optional[str]:
        """
        Returns the description of the command-line tool that is shown when invoked with --help.
        By default, the description is taken from the tool class, or from the module containing
        the tool's class, in that order of preference.
        """
        class_docstring = self.__class__.__doc__
        if class_docstring is not None and class_docstring.strip():
            return class_docstring
        return sys.modules[self.__class__.__module__].__doc__

    def get_arg_parser_kwargs(self) -> Dict[str, Any]:
        return dict(description=self.get_description())

    def __init__(self) -> None:
        self.arg_parser_created = False

    def run(self) -> None:
        """
        The top-level function used to run the tool.
        """
        self.create_arg_parser()
        self.parse_args()
        self.validate_and_process_args()
        self.run_impl()

    def parse_args(self) -> None:
        self.args = self.arg_parser.parse_args()
        init_logging(verbose=self.args.verbose)

    def get_default_build_root(self) -> Optional[str]:
        return None

    def validate_and_process_args(self) -> None:
        if hasattr(self.args, 'build_root'):
            if self.args.build_root is None:
                default_build_root = self.get_default_build_root()
                if default_build_root is not None:
                    self.args.build_root = default_build_root
                    logging.info("Determined default build root as: %s", self.args.build_root)
                else:
                    raise ValueError(
                        '--build_root (or BUILD_ROOT environment variable) not specified, and '
                        'cannot determine the default build root')
            set_env_vars_from_build_root(self.args.build_root)

    def run_impl(self) -> None:
        """
        The overridable function containing the tool's functionality.
        """
        raise NotImplementedError()

    def add_command_line_args(self) -> None:
        """
        Can be overridden to add more command-line arguments to the parser.
        """
        pass

    def create_arg_parser(self) -> None:
        # Don't allow to run this function multiple times.
        if self.arg_parser_created:
            raise RuntimeError("Cannot create the argument parser multiple times")

        self.arg_parser = argparse.ArgumentParser(**self.get_arg_parser_kwargs())

        self.arg_parser.add_argument(
            '--build_root',
            default=os.environ.get('BUILD_ROOT'),
            help='YugabyteDB build root directory')

        self.arg_parser.add_argument(
            '--compiler_type',
            default=os.getenv('YB_COMPILER_TYPE'),
            help='Compiler type, e.g. gcc or clang')

        self.arg_parser.add_argument(
            '--thirdparty_dir',
            default=os.getenv('YB_THIRDPARTY_DIR'),
            help='YugabyteDB third-party dependencies directory')

        self.arg_parser.add_argument(
            '--verbose',
            help='Enable verbose output',
            action='store_true'
        )

        self.add_command_line_args()
