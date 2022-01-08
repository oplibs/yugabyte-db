import json
import logging
import re
import os
import typing

import cpp_parser

TEST_SUFFIX = "_test"
CMAKE_FILE = "CMakeLists.txt"
IGNORED_HEADERS = re.compile(
    R"^google/protobuf/generated_enum_reflection\.h$")
SRC_DIRS = ['src/yb', 'ent/src/yb']
BUILD_DIR = os.path.join('build', 'latest')

parsed_files = {}


# Add rhs to lhs, setting value to `true` if it is `true` in at least one dict.
def merge_headers(lhs: typing.Dict[str, bool], rhs: typing.Dict[str, bool]):
    for header, trivial in rhs.items():
        if header not in lhs:
            lhs[header] = trivial
        else:
            lhs[header] = trivial or lhs[header]


# Container for system and project headers.
class Headers:
    def __init__(self):
        self.system: typing.Dict[str, bool] = dict()
        self.project: typing.Dict[str, bool] = dict()

    def extend(self, rhs):
        merge_headers(self.system, rhs.system)
        merge_headers(self.project, rhs.project)

    # Generate precompiled headers file for specified library.
    def generate(self, path) -> typing.List[str]:
        libname = make_lib_name(path)
        categories = ([], [], [], [])
        for header in sorted(self.system.keys()):
            if IGNORED_HEADERS.match(header) or not self.system[header]:
                continue
            idx = cpp_parser.header_category(header, True)
            if header == 'ev++.h':
                categories[idx].append(
                    '#undef EV_ERROR // On mac is it defined as some number, '
                    'but ev++.h uses it in enum')
            categories[idx].append("#include <{}>".format(header))
        for header in sorted(self.project.keys()):
            if IGNORED_HEADERS.match(header) or not self.project[header]:
                continue
            if libname == 'gutil':
                continue
            if header.startswith('yb/gutil') or \
                    (libname != 'util' and header.startswith('yb/util')):
                categories[3].append('#include "{}"'.format(header))

        lines = [
            "// Copyright (c) YugaByte, Inc.",
            "// This file was auto generated by python/yb/gen_pch.py",
            "#pragma once",
        ]
        for category in categories:
            if len(category) == 0:
                continue
            lines.append('')
            for include in category:
                lines.append(include)
        return lines


# Get headers for specified file.
def process_file(fname: str) -> Headers:
    if fname in parsed_files:
        return parsed_files[fname]
    result = Headers()
    parsed_file = cpp_parser.parse_file(fname)
    if parsed_file.trivial:
        for include in parsed_file.includes:
            if include.system:
                result.system[include.name] = include.trivial
            else:
                result.project[include.name] = include.trivial
            if include.trivial and not include.system:
                for path in ('src', os.path.join(BUILD_DIR, 'src')):
                    include_path = os.path.join(path, include.name)
                    if os.path.exists(include_path):
                        result.extend(process_file(include_path))
                        break

    parsed_files[fname] = result
    return result


# Split path into tokens.
def split_path(path: str) -> typing.List[str]:
    result = []
    while len(path) != 0:
        file_name = os.path.basename(path)
        if file_name == 'CMakeFiles':
            result.clear()
        else:
            result.append(file_name)
        path = os.path.dirname(path)
    return result


def make_lib_name(path: str) -> str:
    path_tokens = split_path(path)
    for i in range(len(path_tokens)):
        if path_tokens[i] in {'yb', 'cql', 'yql', 'redis'}:
            return '_'.join(reversed(path_tokens[:i]))
    raise ValueError("Unable to determine library for {}".format(path))


class LibraryData:
    def __init__(self):
        self.num_files = 0
        self.instantiations: typing.Dict[str, int] = {}
        self.sources: typing.List[str] = []

    def generate(self, path: str):
        headers = Headers()

        for file in self.sources:
            headers.extend(process_file(file))

        if len(headers.system) == 0:
            return
        lines = headers.generate(path)
        first = True
        for inst, count in sorted(self.instantiations.items()):
            if count * 10 < self.num_files:
                continue
            if first:
                lines.append('')
                first = False
            lines.append('template class {};'.format(inst))

        body = ''
        for i in range(len(lines)):
            body += lines[i]
            body += '\n'

        libname = make_lib_name(path)
        pch_name = os.path.join(path, libname + '_pch.h')
        # Don't overwrite file with the same content.
        if os.path.exists(pch_name):
            with open(pch_name) as inp:
                if inp.read() == body:
                    logging.info("Skip {} {}".format(path, libname))
                    return
        with open(pch_name, 'w') as out:
            out.write(body)
        logging.info("Generate {} {}".format(path, libname))


class GenPch:
    def __init__(self):
        self.libs: typing.Dict[str, LibraryData] = dict()

    def execute(self):
        self.collect_libs()
        logging.info(self.libs.keys())

        # Generate precompiled header for each library.
        for path, lib in self.libs.items():
            lib.generate(path)

    def collect_instantiations(self):
        for root in SRC_DIRS:
            for (path, dirs, files) in os.walk(os.path.join(BUILD_DIR, root)):
                library: typing.Union[None, LibraryData] = None
                for file in files:
                    if not file.endswith('.json'):
                        continue
                    if library is None:
                        lib_path = self.lib_for_path(path, strip=True)
                        if lib_path not in self.libs:
                            raise ValueError("Library not yet defined for {}: {}".format(
                                path, lib_path))
                        library = self.libs[lib_path]
                    library.num_files += 1
                    with open(os.path.join(path, file), 'r') as inp:
                        times = json.load(inp)
                    current: typing.List[typing.Tuple[int, int, str]] = []
                    for evt in times['traceEvents']:
                        if evt['name'] != 'InstantiateClass' or 'args' not in evt:
                            continue
                        current.append((evt['ts'], evt['dur'], evt['args']['detail']))
                    finish = 0
                    for entry in sorted(current):
                        if entry[0] >= finish:
                            finish = entry[0] + entry[1]
                            if entry[2] in library.instantiations:
                                library.instantiations[entry[2]] += 1
                            else:
                                library.instantiations[entry[2]] = 1

    def lib_for_path(self, path: str, **kwargs) -> typing.Union[None, str]:
        if 'strip' in kwargs and kwargs['strip']:
            if path.startswith(BUILD_DIR):
                path = path[len(BUILD_DIR) + 1:]
            while len(path) > 0:
                basename = os.path.basename(path)
                path = os.path.dirname(path)
                if basename == 'CMakeFiles':
                    break

        for check_path in ('src/yb/yql/cql/ql', 'src/yb/yql/pggate'):
            if path.startswith(check_path):
                return check_path
        if path in self.libs or ('has_cmake' in kwargs and kwargs['has_cmake']):
            return path

        while len(path) != 0:
            if path.startswith('ent/'):
                path = path[4:]
            else:
                path = os.path.dirname(path)
            if path in self.libs:
                return path
        return None

    def collect_libs(self):
        # Enumerate all files and group them by library.
        for root in SRC_DIRS:
            for (path, dirs, files) in os.walk(root):
                if path.startswith(("src/yb/rocksdb/port/win", "src/yb/rocksdb/examples",
                                    "src/yb/rocksdb/tools/rdb")):
                    continue
                lib_path = self.lib_for_path(path, has_cmake=CMAKE_FILE in files)
                logging.info("Library determined for {}: {}".format(path, lib_path))
                if lib_path is None:
                    continue
                if lib_path not in self.libs:
                    self.libs[lib_path] = LibraryData()
                lib = self.libs[lib_path]
                for file in files:
                    if file.endswith('.cc'):
                        lib.sources.append(os.path.join(path, file))


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s")
    gen_pch = GenPch()
    gen_pch.execute()


if __name__ == '__main__':
    main()
