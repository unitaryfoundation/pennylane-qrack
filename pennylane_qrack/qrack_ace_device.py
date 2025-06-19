# Copyright 2020 Xanadu Quantum Technologies Inc.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
Base device class for PennyLane-Qrack.
"""
from functools import reduce
import cmath, math
import os
import pathlib
import sys
import itertools as it

import numpy as np

# PennyLane v0.42 introduced the `exceptions` module and will raise
# deprecation warnings if they are imported from the top-level module.

# This ensures backwards compatibility with older versions of PennyLane.
try:
    from pennylane.exceptions import DeviceError, QuantumFunctionError
except (ModuleNotFoundError, ImportError) as import_error:
    from pennylane import DeviceError, QuantumFunctionError

from pennylane.devices import QubitDevice
from pennylane.ops import (
    BasisState,
    Adjoint,
)
from pennylane.wires import Wires

from pyqrack import QrackAceBackend, Pauli

from ._version import __version__
from sys import platform as _platform

# tolerance for numerical errors
tolerance = 1e-10


class QrackAceDevice(QubitDevice):
    """Qrack Ace device"""

    name = "Qrack device"
    short_name = "qrack.ace"
    pennylane_requires = ">=0.11.0"
    version = __version__
    author = "Daniel Strano, adapted from Steven Oud and Xanadu"

    _capabilities = {
        "model": "qubit",
        "tensor_observables": True,
        "inverse_operations": True,
        "returns_state": False,
    }

    _observable_map = {
        "PauliX": Pauli.PauliX,
        "PauliY": Pauli.PauliY,
        "PauliZ": Pauli.PauliZ,
        "Identity": Pauli.PauliI,
        "Hadamard": None,
        "Hermitian": None,
        "Prod": None,
        # "Sum": None,
        # "SProd": None,
        # "Exp": None,
        # "Projector": None,
        # "Hamiltonian": None,
        # "SparseHamiltonian": None
    }

    observables = _observable_map.keys()
    operations = {
        "Identity",
        "C(Identity)",
        "MultiRZ",
        "C(MultiRZ)",
        "CRX",
        "CRY",
        "CRZ",
        "CRot",
        "SWAP",
        "ISWAP",
        "PSWAP",
        "CNOT",
        "CY",
        "CZ",
        "S",
        "T",
        "RX",
        "RY",
        "RZ",
        "PauliX",
        "C(PauliX)",
        "PauliY",
        "C(PauliY)",
        "PauliZ",
        "C(PauliZ)",
        "Hadamard",
        "SX",
        "PhaseShift",
        "C(PhaseShift)",
        "U3",
        "Rot",
        "ControlledPhaseShift",
        "CPhase",
    }

    config_filepath = pathlib.Path(
        os.path.dirname(sys.modules[__name__].__file__) + "/QrackDeviceConfig.toml"
    )

    # Use "hybrid" stabilizer optimization? (Default is "true"; non-Clifford circuits will fall back to near-Clifford or universal simulation)
    isStabilizerHybrid = False
    # Use "tensor network" optimization? (Default is "false"; prevents dynamic qubit de-allocation; might function sub-optimally with "hybrid" stabilizer enabled)
    isTensorNetwork = False
    # Use Schmidt decomposition optimizations? (Default is "true")
    isSchmidtDecompose = True
    # Distribute Schmidt-decomposed qubit subsystems to multiple GPUs or accelerators, if available? (Default is "true"; mismatched device capacities might hurt overall performance)
    isSchmidtDecomposeMulti = True
    # Use "quantum binary decision diagram" ("QBDD") methods? (Default is "false"; note that QBDD is CPU-only)
    isBinaryDecisionTree = False
    # Use GPU acceleration? (Default is "true")
    isOpenCL = True
    # Use multi-GPU (or "multi-page") acceleration? (Default is "false")
    isPaged = True
    # Use CPU/GPU method hybridization? (Default is "false")
    isCpuGpuHybrid = True
    # Allocate GPU buffer from general host heap? (Default is "false"; "true" might improve performance or reliability in certain cases, like if using an Intel HD as accelerator)
    isHostPointer = True if os.environ.get("PYQRACK_HOST_POINTER_DEFAULT_ON") else False
    # Noise parameter. (Default is "0"; depolarizing noise intensity can also be controlled by "QRACK_GATE_DEPOLARIZATION" environment variable)
    noise = 0
    # How many full simulation columns, between border columns
    long_range_columns=4
    # How many full simulation rows, between border rows
    long_range_rows=4
    # Whether to transpose rows and columns
    is_transpose=False

    def __init__(self, wires=0, shots=None, **kwargs):
        options = dict(kwargs)
        if "isStabilizerHybrid" in options:
            self.isStabilizerHybrid = options["isStabilizerHybrid"]
        if "isTensorNetwork" in options:
            self.isTensorNetwork = options["isTensorNetwork"]
        if "isSchmidtDecompose" in options:
            self.isSchmidtDecompose = options["isSchmidtDecompose"]
        if "isBinaryDecisionTree" in options:
            self.isBinaryDecisionTree = options["isBinaryDecisionTree"]
        if "isOpenCL" in options:
            self.isOpenCL = options["isOpenCL"]
        if "isPaged" in options:
            self.isPaged = options["isPaged"]
        if "isCpuGpuHybrid" in options:
            self.isCpuGpuHybrid = options["isCpuGpuHybrid"]
        if "isHostPointer" in options:
            self.isHostPointer = options["isHostPointer"]
        if "noise" in options:
            self.noise = options["noise"]
            if (self.noise != 0) and (shots is None):
                raise ValueError("Shots must be finite for noisy simulation (not analytical mode).")
        if "long_range_columns" in options:
            self.long_range_columns = options["long_range_columns"]
        if "long_range_rows" in options:
            self.long_range_rows = options["long_range_rows"]
        if "is_transpose" in options:
            self.is_transpose = options["is_transpose"]

        super().__init__(wires=wires, shots=shots)
        self.shots = shots
        self._state = QrackAceBackend(
            self.num_wires,
            long_range_columns=self.long_range_columns,
            long_range_rows=self.long_range_rows,
            is_transpose=self.is_transpose,
            isStabilizerHybrid=self.isStabilizerHybrid,
            isTensorNetwork=self.isTensorNetwork,
            isSchmidtDecompose=self.isSchmidtDecompose,
            isBinaryDecisionTree=self.isBinaryDecisionTree,
            isOpenCL=self.isOpenCL,
            isCpuGpuHybrid=self.isCpuGpuHybrid,
            isHostPointer=self.isHostPointer,
            noise=self.noise,
        )
        self.device_kwargs = {
            "long_range_columns": self.long_range_columns,
            "long_range_rows": self.long_range_rows,
            "is_transpose": self.is_transpose,
            "is_hybrid_stabilizer": self.isStabilizerHybrid,
            "is_tensor_network": self.isTensorNetwork,
            "is_schmidt_decompose": self.isSchmidtDecompose,
            "is_schmidt_decompose_parallel": self.isSchmidtDecomposeMulti,
            "is_qpdd": self.isBinaryDecisionTree,
            "is_gpu": self.isOpenCL,
            "is_paged": self.isPaged,
            "is_hybrid_cpu_gpu": self.isCpuGpuHybrid,
            "is_host_pointer": self.isHostPointer,
            "noise": self.noise,
        }
        self._circuit = []

    def _reverse_state(self):
        end = self.num_wires - 1
        mid = self.num_wires >> 1
        for i in range(mid):
            self._state.swap(i, end - i)

    def apply(self, operations, **kwargs):
        """Apply the circuit operations to the state.

        This method serves as an auxiliary method to :meth:`~.QrackDevice.apply`.

        Args:
            operations (List[pennylane.Operation]): operations to be applied
        """

        self._circuit = self._circuit + operations
        if self.noise == 0:
            self._apply()
            self._circuit = []
        # else: Defer application until shots or expectation values are requested

    def _apply(self):
        for op in self._circuit:
            if isinstance(op, BasisState):
                self._apply_basis_state(op)
            else:
                self._apply_gate(op)

    def _apply_basis_state(self, op):
        """Initialize a basis state"""
        wires = self.map_wires(Wires(op.wires))
        par = op.parameters[0]
        wire_count = len(wires)
        n_basis_state = len(par)

        if not set(par).issubset({0, 1}):
            raise ValueError("BasisState parameter must consist of 0 or 1 integers.")
        if n_basis_state != wire_count:
            raise ValueError("BasisState parameter and wires must be of equal length.")

        for i in range(wire_count):
            index = wires.labels[i]
            if par[i] != self._state.m(index):
                self._state.x(index)

    def _apply_gate(self, op):
        """Apply native qrack gate"""

        opname = op.name
        if isinstance(op, Adjoint):
            op = op.base
            opname = op.name + ".inv"

        par = op.parameters

        if opname == "MultiRZ":
            device_wires = self.map_wires(op.wires)
            for q in device_wires:
                self._state.r(Pauli.PauliZ, par[0], q)
            return

        # translate op wire labels to consecutive wire labels used by the device
        device_wires = self.map_wires(
            (op.control_wires + op.wires) if op.control_wires else op.wires
        )

        if opname in [
            "".join(p)
            for p in it.product(
                [
                    "CNOT",
                    "C(PauliX)",
                ],
                ["", ".inv"],
            )
        ]:
            self._state.mcx(device_wires.labels[:-1], device_wires.labels[-1])
        elif opname in ["C(PauliY)", "C(PauliY).inv"]:
            self._state.mcy(device_wires.labels[:-1], device_wires.labels[-1])
        elif opname in ["C(PauliZ)", "C(PauliZ).inv"]:
            self._state.mcz(device_wires.labels[:-1], device_wires.labels[-1])
        elif opname in ["SWAP", "SWAP.inv"]:
            self._state.swap(device_wires.labels[0], device_wires.labels[1])
        elif opname == "ISWAP":
            self._state.iswap(device_wires.labels[0], device_wires.labels[1])
        elif opname == "ISWAP.inv":
            self._state.adjiswap(device_wires.labels[0], device_wires.labels[1])
        elif opname in ["CY", "CY.inv", "C(CY)", "C(CY).inv"]:
            self._state.mcy(device_wires.labels[:-1], device_wires.labels[-1])
        elif opname in ["CZ", "CZ.inv", "C(CZ)", "C(CZ).inv"]:
            self._state.mcz(device_wires.labels[:-1], device_wires.labels[-1])
        elif opname == "S":
            for label in device_wires.labels:
                self._state.s(label)
        elif opname == "S.inv":
            for label in device_wires.labels:
                self._state.adjs(label)
        elif opname == "T":
            for label in device_wires.labels:
                self._state.t(label)
        elif opname == "T.inv":
            for label in device_wires.labels:
                self._state.adjt(label)
        elif opname == "RX":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliX, par[0], label)
        elif opname == "RX.inv":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliX, -par[0], label)
        elif opname == "RY":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliY, par[0], label)
        elif opname == "RY.inv":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliY, -par[0], label)
        elif opname == "RZ":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliZ, par[0], label)
        elif opname == "RZ.inv":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliZ, -par[0], label)
        elif opname in ["PauliX", "PauliX.inv"]:
            for label in device_wires.labels:
                self._state.x(label)
        elif opname in ["PauliY", "PauliY.inv"]:
            for label in device_wires.labels:
                self._state.y(label)
        elif opname in ["PauliZ", "PauliZ.inv"]:
            for label in device_wires.labels:
                self._state.z(label)
        elif opname in ["Hadamard", "Hadamard.inv"]:
            for label in device_wires.labels:
                self._state.h(label)
        elif opname == "SX":
            half_pi = math.pi / 2
            for label in device_wires.labels:
                self._state.u(label, half_pi, -half_pi, half.pi)
        elif opname == "SX.inv":
            half_pi = math.pi / 2
            for label in device_wires.labels:
                self._state.u(label, -half_pi, -half.pi, half_pi)
        elif opname == "PhaseShift":
            half_par = par[0] / 2
            for label in device_wires.labels:
                self._state.u(label, 0, half_par, half_par)
        elif opname == "PhaseShift.inv":
            half_par = par[0] / 2
            for label in device_wires.labels:
                self._state.u(label, 0, -half_par, -half_par)
        elif opname == "U3":
            for label in device_wires.labels:
                self._state.u(label, par[0], par[1], par[2])
        elif opname == "U3.inv":
            for label in device_wires.labels:
                self._state.u(label, -par[0], -par[2], -par[1])
        elif opname == "Rot":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliZ, par[0], label)
                self._state.r(Pauli.PauliY, par[1], label)
                self._state.r(Pauli.PauliZ, par[2], label)
        elif opname == "Rot.inv":
            for label in device_wires.labels:
                self._state.r(Pauli.PauliZ, -par[2], label)
                self._state.r(Pauli.PauliY, -par[1], label)
                self._state.r(Pauli.PauliZ, -par[0], label)
        elif opname not in [
            "Identity",
            "Identity.inv",
            "C(Identity)",
            "C(Identity).inv",
        ]:
            raise DeviceError(f"Operation {opname} is not supported on a {self.short_name} device.")

    def analytic_probability(self, wires=None):
        raise DeviceError(f"analytic_probability is not supported on a {self.short_name} device. (Specify a finite number of shots, instead.)")

    def expval(self, observable, **kwargs):
        if self.shots is None:
            if isinstance(observable.name, list):
                b = [self._observable_map[obs] for obs in observable.name]
            elif observable.name == "Prod":
                b = [self._observable_map[obs.name] for obs in observable.operands]
            else:
                b = [self._observable_map[observable.name]]

            # exact expectation value
            if callable(observable.eigvals):
                eigvals = self._asarray(observable.eigvals(), dtype=self.R_DTYPE)
            else:  # older version of pennylane
                eigvals = self._asarray(observable.eigvals, dtype=self.R_DTYPE)
            prob = self.probability(wires=observable.wires)
            return self._dot(eigvals, prob)

        # estimate the ev
        return np.mean(self.sample(observable))

    def _generate_sample(self):
        rev_sample = self._state.m_all()
        sample = 0
        for i in range(self.num_wires):
            if (rev_sample & (1 << i)) > 0:
                sample |= 1 << (self.num_wires - (i + 1))
        return sample

    def generate_samples(self):
        if self.shots is None:
            raise QuantumFunctionError(
                "The number of shots has to be explicitly set on the device "
                "when using sample-based measurements."
            )

        if self.noise != 0:
            samples = []
            for _ in range(self.shots):
                self._state.reset_all()
                self._apply()
                samples.append(self._generate_sample())
            self._samples = QubitDevice.states_to_binary(np.array(samples), self.num_wires)
            self._circuit = []

            return self._samples

        if self.shots == 1:
            self._samples = QubitDevice.states_to_binary(
                np.array([self._generate_sample()]), self.num_wires
            )

            return self._samples

        samples = np.array(
            self._state.measure_shots(list(range(self.num_wires - 1, -1, -1)), self.shots)
        )
        self._samples = QubitDevice.states_to_binary(samples, self.num_wires)

        return self._samples

    def reset(self):
        for i in range(self.num_wires):
            if self._state.m(i):
                self._state.x(i)
