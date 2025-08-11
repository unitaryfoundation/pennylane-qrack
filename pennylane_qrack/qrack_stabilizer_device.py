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

from pyqrack import QrackStabilizer, Pauli

from ._version import __version__
from sys import platform as _platform

# tolerance for numerical errors
tolerance = 1e-10


class QrackStabilizerDevice(QubitDevice):
    """Qrack Stabilizer device"""

    name = "Qrack stabilizer device"
    short_name = "qrack.stabilizer"
    pennylane_requires = ">=0.11.0"
    version = __version__
    author = "Daniel Strano, adapted from Steven Oud and Xanadu"

    _capabilities = {
        "model": "qubit",
        "tensor_observables": True,
        "inverse_operations": True,
        "returns_state": True,
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
        "SWAP",
        "ISWAP",
        "PSWAP",
        "CNOT",
        "CY",
        "CZ",
        "S",
        "PauliX",
        "C(PauliX)",
        "PauliY",
        "C(PauliY)",
        "PauliZ",
        "C(PauliZ)",
        "Hadamard",
        "SX",
    }

    config_filepath = pathlib.Path(
        os.path.dirname(sys.modules[__name__].__file__) + "/QrackStabilizerDeviceConfig.toml"
    )

    def __init__(self, wires=0, shots=None, **kwargs):
        super().__init__(wires=wires, shots=shots)
        self.shots = shots
        self._state = QrackStabilizer(self.num_wires)
        self.device_kwargs = {}

    def _reverse_state(self):
        end = self.num_wires - 1
        mid = self.num_wires >> 1
        for i in range(mid):
            self._state.swap(i, end - i)

    def apply(self, operations, **kwargs):
        for op in operations:
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
        elif opname not in [
            "Identity",
            "Identity.inv",
            "C(Identity)",
            "C(Identity).inv",
        ]:
            raise DeviceError(f"Operation {opname} is not supported on a {self.short_name} device.")

    def analytic_probability(self, wires=None):
        """Return the (marginal) analytic probability of each computational basis state."""
        if self._state is None:
            return None

        all_probs = self._abs(self.state) ** 2
        prob = self.marginal_prob(all_probs, wires)

        if (not "QRACK_FPPOW" in os.environ) or (6 > int(os.environ.get("QRACK_FPPOW"))):
            tot_prob = 0
            for p in prob:
                tot_prob = tot_prob + p

            if tot_prob != 1.0:
                for i in range(len(prob)):
                    prob[i] = prob[i] / tot_prob

        return prob

    def expval(self, observable, **kwargs):
        if self.shots is None:
            if isinstance(observable.name, list):
                b = [self._observable_map[obs] for obs in observable.name]
            elif observable.name == "Prod":
                b = [self._observable_map[obs.name] for obs in observable.operands]
            else:
                b = [self._observable_map[observable.name]]

            if None not in b:
                # This will trigger Gaussian elimination,
                # so it only happens once.
                self._state.try_separate_1qb(0)
                # It's cheap to clone a stabilizer,
                # but we don't want to have to transform
                # back after terminal measurement.
                state_clone = self._state.clone()

                q = self.map_wires(observable.wires)
                for qb, base in zip(q, b):
                    match base:
                        case Pauli.PauliX:
                            state_clone.h(qb)
                        case Pauli.PauliY:
                            state_clone.adjs(qb)
                            state_clone.h(qb)
                b = [Pauli.PauliZ] * len(b)

                return self._state.pauli_expectation(q, b)

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

            return self._samples

        if self.shots == 1:
            self._samples = QubitDevice.states_to_binary(
                np.array([self._generate_sample()]), self.num_wires
            )

            return self._samples

        # QubitDevice.states_to_binary() doesn't work for >64qb. (Fix by Elara, the custom OpenAI GPT)
        samples = self._state.measure_shots(list(range(self.num_wires - 1, -1, -1)), self.shots)
        self._samples = np.array(
            [list(format(b, f"0{self.num_wires}b")) for b in samples], dtype=np.int8
        )

        return self._samples

    def reset(self):
        for i in range(self.num_wires):
            if self._state.m(i):
                self._state.x(i)
