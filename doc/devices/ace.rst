The Simulator device
====================

You can instantiate the Qrack ACE device in PennyLane as follows:

.. code-block:: python

    import pennylane as qml

    dev = qml.device('qrack.ace', wires=2, shots=1024)

This device is highly optimized for transverse-field Ising model, but it has no analytical mode and must have finite shots.
A simple quantum function that returns the expectation value of a measurement and depends on three classical input
parameters would look like:

.. code-block:: python

    @qjit # optional, for JIT compilation
    @qml.qnode(dev)
    def circuit(x, y, z):
        qml.RZ(z, wires=[0])
        qml.RY(y, wires=[0])
        qml.RX(x, wires=[0])
        qml.CNOT(wires=[0, 1])
        return qml.expval(qml.PauliZ(wires=1))

You can then execute the circuit like any other function to get the quantum mechanical expectation value.

.. code-block:: python

    circuit(0.2, 0.1, 0.3)

Device options
~~~~~~~~~~~~~~

By default, Qrack will automatically and "transparently" switch between CPU-based and GPU-based methods as would
be most appropriate for best performance. Qrack will also automatically use the "hybrid simulation algorithm"
"layers" that tend to work best for general "BQP-complete" tasks like random circuit sampling.

If you'd like to use a special-case algorithm, like quantum binary decision diagrams (QBDD), or if you want more
direct control over choice of algorithm and hardware used, there is a series of boolean options users can toggle
like so:

.. code-block:: python

    dev = qml.device('qrack.simulator', wires=2, isBinaryDecisionTree=True)


This is a complete list of boolean options and their meanings:

.. rst-class:: docstable

    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | **Parameter**             | **Default** | **Description**                                                                     |
    +===========================+=============+=====================================================================================+
    | `isStabilizerHybrid`      | False       | Use "hybrid" stabilizer optimization?                                               |
    |                           |             | (Non-Clifford circuits will fall back to near-Clifford or universal simulation.)    |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `isTensorNetwork`         | True        | Use "tensor network" optimization?                                                  |
    |                           |             | (This option locally simplifies circuits, just-in-time, before running them.)       |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `isSchmidtDecompose`      | True        | Use Schmidt decomposition optimizations?                                            |
    |                           |             |                                                                                     |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `isSchmidtDecomposeMulti` | True        | Distribute Schmidt-decomposed qubit subsystems to multiple GPUs or accelerators?    |
    |                           |             | (Mismatched device capacities might hurt overall performance.)                      |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `isBinaryDecisionTree`    | False       | Use "quantum binary decision diagram" ("QBDD") methods?                             |
    |                           |             | (Note that QBDD is CPU-only.)                                                       |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `isOpenCL`                | True        | Use GPU acceleration?                                                               |
    |                           |             |                                                                                     |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `isHostPointer`           | False       | Allocate GPU buffer from general host heap?                                         |
    |                           |             | (Might improve performance or reliability, like when accelerating on an Intel HD)   |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `noise`                   | 0           | Depolarizing noise parameter                                                        |
    |                           |             | (Noise intensity also responds to "QRACK_GATE_DEPOLARIZATION" environment variable) |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `long_range_columns`      | 4           | Number of ideal simulation columns between semi-classical boundary columns          |
    |                           |             |                                                                                     |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `long_range_rows`         | 4           | Number of ideal simulation rows between semi-classical boundary rows                |
    |                           |             |                                                                                     |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+
    | `is_transpose`            | False       | Transpose rows and columns?                                                         |
    |                           |             |                                                                                     |
    +---------------------------+-------------+-------------------------------------------------------------------------------------+


Supported operations
~~~~~~~~~~~~~~~~~~~~

The ``qrack.ace`` device only supports (all PennyLane) single-qubit gates and singly-controlled Pauli gates.
