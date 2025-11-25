# Copyright 2020 Xanadu Quantum Technologies Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#!/usr/bin/env python3
import os
import re
import sys
import subprocess
from setuptools import setup
from setuptools.command.build_py import build_py


class Build(build_py):
    def run(self):
        protoc_command = ["make", "build-deps"]
        if os.name != "nt":
            if subprocess.call(protoc_command) != 0:
                sys.exit(-1)
        super().run()


with open("./pennylane_qrack/_version.py") as f:
    (version,) = re.findall('__version__ = "(.*)"', f.read())

requirements = ["pennylane>=0.39.0", "pyqrack>=1.30.0", "numpy>=1.16"]

info = {
    "name": "pennylane-qrack",
    "version": version,
    "maintainer": "vm6502q",
    "maintainer_email": "stranoj@gmail.com",
    "url": "http://github.com/vm6502q",
    "license": "Apache License 2.0",
    "packages": ["pennylane_qrack"],
    "cmdclass": {"build_py": Build},
    "entry_points": {
        "pennylane.plugins": ["qrack.simulator = pennylane_qrack.qrack_device:QrackDevice", "qrack.ace = pennylane_qrack.qrack_ace_device:QrackAceDevice", "qrack.stabilizer = pennylane_qrack.qrack_stabilizer_device:QrackStabilizerDevice"]
    },
    "description": "PennyLane plugin for Qrack.",
    "long_description": open("README.rst").read(),
    "long_description_content_type": "text/x-rst",
    "provides": ["pennylane_qrack"],
    "install_requires": requirements,
    "package_data": {"pennylane_qrack": ["QrackDeviceConfig.toml"]},
    "include_package_data": True,
}

classifiers = [
    "Development Status :: 4 - Beta",
    "Environment :: Console",
    "Intended Audience :: Science/Research",
    "License :: OSI Approved :: Apache Software License",
    "Natural Language :: English",
    "Operating System :: POSIX",
    "Operating System :: MacOS :: MacOS X",
    "Operating System :: POSIX :: Linux",
    "Operating System :: Microsoft :: Windows",
    "Programming Language :: Python",
    # Make sure to specify here the versions of Python supported
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3.9",
    "Programming Language :: Python :: 3.10",
    "Programming Language :: Python :: 3.11",
    "Programming Language :: Python :: 3.12",
    "Programming Language :: Python :: 3 :: Only",
    "Topic :: Scientific/Engineering :: Quantum Computing",
]

setup(classifiers=classifiers, **info)
