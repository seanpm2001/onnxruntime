#!/usr/bin/env python3

import json
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
REPO_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))

package_name = None
package_filename = None
package_url = None

registrations = []

with open(
    os.path.join(REPO_DIR, "tools", "ci_build", "github", "linux", "docker", "Dockerfile.manylinux2014_cuda11"),
    mode="r",
) as f:
    for line in f:
        if not line.strip():
            package_name = None
            package_filename = None
            package_url = None
        if package_filename is None:
            m = re.match(r"RUN\s+export\s+(.+?)_ROOT=(\S+).*", line)
            if m is not None:
                package_name = m.group(1)
                package_filename = m.group(2)
            else:
                m = re.match(r"RUN\s+export\s+(.+?)_VERSION=(\S+).*", line)
                if m is not None:
                    package_name = m.group(1)
                    package_filename = m.group(2)
        elif package_url is None:
            m = re.match(r"(.+?)_DOWNLOAD_URL=(\S+)", line)
            if m is not None:
                package_url = m.group(2)
                if package_name == "LIBXCRYPT":
                    package_url = m.group(2) + "/v" + package_filename + ".tar.gz"
                elif package_name == "CMAKE":
                    package_url = m.group(2) + "/v" + package_filename + "/cmake-" + package_filename + ".tar.gz"
                else:
                    package_url = m.group(2) + "/" + package_filename + ".tar.gz"
                registration = {
                    "Component": {
                        "Type": "other",
                        "other": {
                            "Name": package_name.lower(),
                            "Version": package_filename.split("-")[-1],
                            "DownloadUrl": package_url,
                        },
                        "comments": "manylinux dependency",
                    }
                }
                registrations.append(registration)
                package_name = None
                package_filename = None
                package_url = None


def normalize_path_separators(path):
    return path.replace(os.path.sep, "/")


proc = subprocess.run(
    [
        "git",
        "submodule",
        "foreach",
        "--quiet",
        "--recursive",
        "'{}' '{}' $toplevel/$sm_path".format(
            normalize_path_separators(sys.executable),
            normalize_path_separators(os.path.join(SCRIPT_DIR, "print_submodule_info.py")),
        ),
    ],
    check=True,
    cwd=REPO_DIR,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    universal_newlines=True,
)


submodule_lines = proc.stdout.splitlines()
for submodule_line in submodule_lines:
    (absolute_path, url, commit) = submodule_line.split(" ")
    registration = {
        "component": {
            "type": "git",
            "git": {
                "commitHash": commit,
                "repositoryUrl": url,
            },
            "comments": "git submodule at {}".format(
                normalize_path_separators(os.path.relpath(absolute_path, REPO_DIR))
            ),
        }
    }
    registrations.append(registration)

cgmanifest = {"Version": 1, "Registrations": registrations}

with open(os.path.join(SCRIPT_DIR, "generated", "cgmanifest.json"), mode="w") as generated_cgmanifest_file:
    print(json.dumps(cgmanifest, indent=2), file=generated_cgmanifest_file)
