#include <regex>
#include <stdexcept>
#include <string>
#include <QuantumDevice.hpp>

#define CL_HPP_TARGET_OPENCL_VERSION 300
#include "qrack/qfactory.hpp"

#define QSIM_CONFIG(numQubits) Qrack::CreateArrangedLayersFull(nw, md, sd, sh, bdt, pg, tn, hy, oc, numQubits, Qrack::ZERO_BCI, nullptr, Qrack::CMPLX_DEFAULT_ARG, false, true, hp)

std::string trim(std::string s)
{
    // Cut leading, trailing, and extra spaces
    // (See https://stackoverflow.com/questions/1798112/removing-leading-and-trailing-spaces-from-a-string#answer-1798170)
    return std::regex_replace(s, std::regex("^ +| +$|( ) +"), "$1");
}

struct QrackObservable {
    std::vector<Qrack::Pauli> obs;
    std::vector<bitLenInt> wires;
    QrackObservable()
    {
        // Intentionally left blank
    }
    QrackObservable(std::vector<Qrack::Pauli> o, std::vector<bitLenInt> w)
        : obs(o)
        , wires(w)
    {
        // Intentionally left blank
    }
};

struct QrackDevice final : public Catalyst::Runtime::QuantumDevice {
    bool tapeRecording;
    bool sh;
    bool tn;
    bool sd;
    bool md;
    bool bdt;
    bool oc;
    bool pg;
    bool hy;
    bool hp;
    bool nw;
    size_t shots;
    Qrack::real1_f noise_param;
    Qrack::QInterfacePtr qsim;
    std::map<QubitIdType, bitLenInt> qubit_map;
    std::vector<QrackObservable> obs_cache;
    std::vector<Qrack::QInterfaceEngine> simulatorType;

    // static constants for RESULT values
    static constexpr bool QRACK_RESULT_TRUE_CONST = true;
    static constexpr bool QRACK_RESULT_FALSE_CONST = false;

    inline void reverseWires()
    {
        const bitLenInt end = qsim->GetQubitCount() - 1U;
        const bitLenInt mid = qsim->GetQubitCount() >> 1U;
        for (bitLenInt i = 0U; i < mid; ++i) {
            qsim->Swap(i, end - i);
        }
    }

    inline auto getDeviceWires(const std::vector<QubitIdType> &wires) -> std::vector<bitLenInt>
    {
        std::vector<bitLenInt> res;
        res.reserve(wires.size());
        std::transform(wires.begin(), wires.end(), std::back_inserter(res), [this](auto w) {
            const auto& it = qubit_map.find(w);
            if (it == qubit_map.end()) {
                throw std::invalid_argument("Qubit ID not in wire map: " + std::to_string(w));
            }
            return it->second;
        });
        return res;
    }

    inline auto wiresToMask(const std::vector<bitLenInt> &wires) -> bitCapInt
    {
        bitCapInt mask = Qrack::ZERO_BCI;
        for (const bitLenInt& target : wires) {
            mask = mask | Qrack::pow2(target);
        }

        return mask;
    }

    void applyNamedOperation(const std::string &name, const std::vector<bitLenInt> &wires,
                             const bool& inverse, const std::vector<double> &params)
    {
        if (name == "PauliX") {
            // Self-adjoint, so ignore "inverse"
            if (wires.size() > 1U) {
                qsim->XMask(wiresToMask(wires));
            } else {
                qsim->X(wires[0U]);
            }
        } else if (name == "PauliY") {
            // Self-adjoint, so ignore "inverse"
            if (wires.size() > 1U) {
                qsim->YMask(wiresToMask(wires));
            } else {
                qsim->Y(wires[0U]);
            }
        } else if (name == "PauliZ") {
            // Self-adjoint, so ignore "inverse"
            if (wires.size() > 1U) {
                qsim->ZMask(wiresToMask(wires));
            } else {
                qsim->Z(wires[0U]);
            }
        } else if (name == "SX") {
            for (const bitLenInt& target : wires) {
                if (inverse) {
                    qsim->ISqrtX(target);
                } else {
                    qsim->SqrtX(target);
                }
            }
        } else if (name == "MultiRZ") {
            for (const bitLenInt& target : wires) {
                qsim->RZ(inverse ? -params[0U] : params[0U], target);
            }
        } else if (name == "Hadamard") {
            for (const bitLenInt& target : wires) {
                qsim->H(target);
            }
        } else if (name == "S") {
            for (const bitLenInt& target : wires) {
                if (inverse) {
                    qsim->IS(target);
                } else {
                    qsim->S(target);
                }
            }
        } else if (name == "T") {
            for (const bitLenInt& target : wires) {
                if (inverse) {
                    qsim->IT(target);
                } else {
                    qsim->T(target);
                }
            }
        } else if (name == "SWAP") {
            if (wires.size() != 2U) {
                throw std::invalid_argument("SWAP must have exactly two target qubits!");
            }
            qsim->Swap(wires[0U], wires[1U]);
        } else if (name == "ISWAP") {
            if (wires.size() != 2U) {
                throw std::invalid_argument("ISWAP must have exactly two target qubits!");
            }
            if (inverse) {
                qsim->ISwap(wires[0U], wires[1U]);
            } else {
                qsim->IISwap(wires[0U], wires[1U]);
            }
        } else if (name == "PSWAP") {
            if (wires.size() != 2U) {
                throw std::invalid_argument("PSWAP must have exactly two target qubits!");
            }
            const std::vector<bitLenInt> c { wires[0U] };
            qsim->CU(c, wires[1U], ZERO_R1, ZERO_R1, inverse ? -params[0U] : params[0U]);
            qsim->Swap(wires[0U], wires[1U]);
            qsim->CU(c, wires[1U], ZERO_R1, ZERO_R1, inverse ? -params[0U] : params[0U]);
        } else if (name == "PhaseShift") {
            const Qrack::complex bottomRight = exp(Qrack::I_CMPLX * (Qrack::real1)(inverse ? -params[0U] : params[0U]));
            for (const bitLenInt& target : wires) {
                qsim->Phase(Qrack::ONE_CMPLX, bottomRight, target);
            }
        } else if (name == "RX") {
            for (const bitLenInt& target : wires) {
                qsim->RX(inverse ? -params[0U] : params[0U], target);
            }
        } else if (name == "RY") {
            for (const bitLenInt& target : wires) {
                qsim->RY(inverse ? -params[0U] : params[0U], target);
            }
        } else if (name == "RZ") {
            for (const bitLenInt& target : wires) {
                qsim->RZ(inverse ? -params[0U] : params[0U], target);
            }
        } else if (name == "Rot") {
            const Qrack::real1 phi = inverse ? -params[2U] : params[0U];
            const Qrack::real1 theta = inverse ? -params[1U] : params[1U];
            const Qrack::real1 omega = inverse ? -params[0U] : params[2U];
            const Qrack::real1 cos0 = (Qrack::real1)cos(theta / 2);
            const Qrack::real1 sin0 = (Qrack::real1)sin(theta / 2);
            const Qrack::complex expP = exp(Qrack::I_CMPLX * (phi + omega) * HALF_R1);
            const Qrack::complex expM = exp(Qrack::I_CMPLX * (phi - omega) * HALF_R1);
            const Qrack::complex mtrx[4U]{
                cos0 / expP, -sin0 * expM,
                sin0 / expM, cos0 * expP
            };
            for (const bitLenInt& target : wires) {
                qsim->Mtrx(mtrx, target);
            }
        } else if (name == "U3") {
            for (const bitLenInt& target : wires) {
                if (inverse) {
                    qsim->U(target, -params[0U], -params[2U], -params[1U]);
                } else {
                    qsim->U(target, params[0U], params[1U], params[2U]);
                }
            }
        } else if (name != "Identity") {
            throw std::domain_error("Unrecognized gate name: " + name);
        }
    }

    void applyNamedOperation(const std::string &name, const std::vector<bitLenInt> &control_wires,
                             const std::vector<bool> &control_values,
                             const std::vector<bitLenInt> &wires, const bool& inverse,
                             const std::vector<double> &params)
    {
        bitCapInt controlPerm = Qrack::ZERO_BCI;
        for (bitLenInt i = 0U; i < control_values.size(); ++i) {
            if (control_values[i]) {
                controlPerm = controlPerm | Qrack::pow2(i);
            }
        }

        QRACK_CONST Qrack::complex NEG_1_CMPLX(-ONE_R1, ZERO_R1);
        QRACK_CONST Qrack::complex NEG_I_CMPLX(ZERO_R1, ONE_R1);
        QRACK_CONST Qrack::complex SQRT1_2_CMPLX(Qrack::SQRT1_2_R1, ZERO_R1);
        QRACK_CONST Qrack::complex NEG_SQRT1_2_CMPLX(-Qrack::SQRT1_2_R1, ZERO_R1);
        QRACK_CONST Qrack::complex SQRTI_2_CMPLX(ZERO_R1, Qrack::SQRT1_2_R1);
        QRACK_CONST Qrack::complex NEG_SQRTI_2_CMPLX(ZERO_R1, -Qrack::SQRT1_2_R1);
        const Qrack::complex QBRTI_2_CMPLX(ZERO_R1, sqrt(Qrack::SQRT1_2_R1));
        const Qrack::complex NEG_QBRTI_2_CMPLX(ZERO_R1, sqrt(-Qrack::SQRT1_2_R1));

        QRACK_CONST Qrack::complex pauliX[4U] = { Qrack::ZERO_CMPLX, Qrack::ONE_CMPLX, Qrack::ONE_CMPLX, Qrack::ZERO_CMPLX };
        QRACK_CONST Qrack::complex pauliY[4U] = { Qrack::ZERO_CMPLX, NEG_I_CMPLX, Qrack::I_CMPLX, Qrack::ZERO_CMPLX };
        QRACK_CONST Qrack::complex pauliZ[4U] = { Qrack::ONE_CMPLX, Qrack::ZERO_CMPLX, Qrack::ZERO_CMPLX, NEG_1_CMPLX };
        QRACK_CONST Qrack::complex sqrtX[4U]{ Qrack::HALF_I_HALF_CMPLX, Qrack::HALF_NEG_I_HALF_CMPLX, Qrack::HALF_NEG_I_HALF_CMPLX, Qrack::HALF_I_HALF_CMPLX };
        QRACK_CONST Qrack::complex iSqrtX[4U]{ Qrack::HALF_NEG_I_HALF_CMPLX, Qrack::HALF_I_HALF_CMPLX, Qrack::HALF_I_HALF_CMPLX, Qrack::HALF_NEG_I_HALF_CMPLX };
        QRACK_CONST Qrack::complex hadamard[4U]{ SQRT1_2_CMPLX, SQRT1_2_CMPLX, SQRT1_2_CMPLX, NEG_SQRT1_2_CMPLX };

        if ((name == "PauliX") || (name == "CNOT") || (name == "Toffoli") || (name == "MultiControlledX")) {
            // Self-adjoint, so ignore "inverse"
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, pauliX, target, controlPerm);
            }
        } else if ((name == "PauliY") || (name == "CY")) {
            // Self-adjoint, so ignore "inverse"
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, pauliY, target, controlPerm);
            }
        } else if ((name == "PauliZ") || (name == "CZ")) {
            // Self-adjoint, so ignore "inverse"
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, pauliZ, target, controlPerm);
            }
        } else if (name == "SX") {
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, inverse ? iSqrtX : sqrtX, target, controlPerm);
            }
        } else if (name == "MultiRZ") {
            const Qrack::complex bottomRight = exp(Qrack::I_CMPLX * (Qrack::real1)((inverse ? -params[0U] : params[0U]) / 2));
            for (const bitLenInt& target : wires) {
                qsim->UCPhase(control_wires, conj(bottomRight), bottomRight, target, controlPerm);
            }
        } else if (name == "Hadamard") {
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, hadamard, target, controlPerm);
            }
        } else if (name == "S") {
            for (const bitLenInt& target : wires) {
                qsim->UCPhase(control_wires, Qrack::ONE_CMPLX, inverse ? NEG_SQRTI_2_CMPLX : SQRTI_2_CMPLX, target, controlPerm);
            }
        } else if (name == "T") {
            for (const bitLenInt& target : wires) {
                qsim->UCPhase(control_wires, Qrack::ONE_CMPLX, inverse ? NEG_QBRTI_2_CMPLX : QBRTI_2_CMPLX, target, controlPerm);
            }
        } else if ((name == "SWAP") || (name == "CSWAP")) {
            if (wires.size() != 2U) {
                throw std::invalid_argument("SWAP and CSWAP must have exactly two target qubits!");
            }
            for (bitLenInt i = 0U; i < control_wires.size(); ++i) {
                if (!control_values[i]) {
                    qsim->X(control_wires[i]);
                }
            }
            qsim->CSwap(control_wires, wires[0U], wires[1U]);
            for (bitLenInt i = 0U; i < control_wires.size(); ++i) {
                if (!control_values[i]) {
                    qsim->X(control_wires[i]);
                }
            }
        } else if (name == "ISWAP") {
            if (wires.size() != 2U) {
                throw std::invalid_argument("ISWAP must have exactly two target qubits!");
            }
            for (bitLenInt i = 0U; i < control_wires.size(); ++i) {
                if (!control_values[i]) {
                    qsim->X(control_wires[i]);
                }
            }
            std::vector<bitLenInt> mcp_wires(control_wires);
            mcp_wires.push_back(wires[0U]);
            qsim->MCPhase(mcp_wires, inverse ? -Qrack::I_CMPLX : Qrack::I_CMPLX, Qrack::ONE_CMPLX, wires[1U]);
            qsim->CSwap(control_wires, wires[0U], wires[1U]);
            qsim->MCPhase(mcp_wires, inverse ? -Qrack::I_CMPLX : Qrack::I_CMPLX, Qrack::ONE_CMPLX, wires[1U]);
            for (bitLenInt i = 0U; i < control_wires.size(); ++i) {
                if (!control_values[i]) {
                    qsim->X(control_wires[i]);
                }
            }
        } else if ((name == "PhaseShift") || (name == "ControlledPhaseShift") || (name == "CPhase")) {
            const Qrack::complex bottomRight = exp(Qrack::I_CMPLX * (Qrack::real1)(inverse ? -params[0U] : params[0U]));
            for (const bitLenInt& target : wires) {
                qsim->UCPhase(control_wires, Qrack::ONE_CMPLX, bottomRight, target, controlPerm);
            }
        } else if (name == "PSWAP") {
            std::vector<bitLenInt> c(control_wires);
            c.push_back(wires[0U]);
            qsim->CU(c, wires[1U], ZERO_R1, ZERO_R1, inverse ? -params[0U] : params[0U]);
            qsim->CSwap(control_wires, wires[0U], wires[1U]);
            qsim->CU(c, wires[1U], ZERO_R1, ZERO_R1, inverse ? -params[0U] : params[0U]);
        } else if ((name == "RX") || (name == "CRX")) {
            const Qrack::real1 cosine = (Qrack::real1)cos((inverse ? -params[0U] : params[0U]) / 2);
            const Qrack::real1 sine = (Qrack::real1)sin((inverse ? -params[0U] : params[0U]) / 2);
            const Qrack::complex mtrx[4U] = {
                Qrack::complex(cosine, ZERO_R1), Qrack::complex(ZERO_R1, -sine),
                Qrack::complex(ZERO_R1, -sine), Qrack::complex(cosine, ZERO_R1)
            };
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, mtrx, target, controlPerm);
            }
        } else if ((name == "RY") || (name == "CRY")) {
            const Qrack::real1 cosine = (Qrack::real1)cos((inverse ? -params[0U] : params[0U]) / 2);
            const Qrack::real1 sine = (Qrack::real1)sin((inverse ? -params[0U] : params[0U]) / 2);
            const Qrack::complex mtrx[4U] = {
                Qrack::complex(cosine, ZERO_R1), Qrack::complex(-sine, ZERO_R1),
                Qrack::complex(sine, ZERO_R1), Qrack::complex(cosine, ZERO_R1)
            };
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, mtrx, target, controlPerm);
            }
        } else if ((name == "RZ") || (name == "CRZ")) {
            const Qrack::complex bottomRight = exp(Qrack::I_CMPLX * (Qrack::real1)((inverse ? -params[0U] : params[0U]) / 2));
            for (const bitLenInt& target : wires) {
                qsim->UCPhase(control_wires, conj(bottomRight), bottomRight, target, controlPerm);
            }
        } else if ((name == "Rot") || (name == "CRot")) {
            const Qrack::real1 phi = inverse ? -params[2U] : params[0U];
            const Qrack::real1 theta = inverse ? -params[1U] : params[1U];
            const Qrack::real1 omega = inverse ? -params[0U] : params[2U];
            const Qrack::real1 cos0 = (Qrack::real1)cos(theta / 2);
            const Qrack::real1 sin0 = (Qrack::real1)sin(theta / 2);
            const Qrack::complex expP = exp(Qrack::I_CMPLX * (phi + omega) * HALF_R1);
            const Qrack::complex expM = exp(Qrack::I_CMPLX * (phi - omega) * HALF_R1);
            const Qrack::complex mtrx[4U]{
                cos0 / expP, -sin0 * expM,
                sin0 / expM, cos0 * expP
            };
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, mtrx, target, controlPerm);
            }
        } else if (name == "U3") {
            const Qrack::real1 th = params[0U];
            const Qrack::real1 ph = params[1U];
            const Qrack::real1 lm = params[2U];
            const Qrack::real1 cos0 = (Qrack::real1)cos(th / 2);
            const Qrack::real1 sin0 = (Qrack::real1)sin(th / 2);
            const Qrack::complex mtrx[4U]{
                Qrack::complex(cos0, ZERO_R1), sin0 * Qrack::complex((Qrack::real1)(-cos(lm)),
                (Qrack::real1)(-sin(lm))),
                sin0 * Qrack::complex((Qrack::real1)cos(ph), (Qrack::real1)sin(ph)),
                cos0 * Qrack::complex((Qrack::real1)cos(ph + lm), (Qrack::real1)sin(ph + lm))
            };
            Qrack::complex iMtrx[4U];
            Qrack::inv2x2(mtrx, iMtrx);
            for (const bitLenInt& target : wires) {
                qsim->UCMtrx(control_wires, inverse ? iMtrx : mtrx, target, controlPerm);
            }
        } else if (name != "Identity") {
            throw std::domain_error("Unrecognized gate name: " + name);
        }
    }

    QrackDevice([[maybe_unused]] std::string kwargs = "{}")
        : tapeRecording(false)
        , sh(true)
        , tn(true)
        , sd(true)
        , md(true)
        , bdt(false)
        , oc(true)
        , pg(false)
        , hy(false)
        , hp(false)
        , nw(false)
        , shots(1U)
        , noise_param(ZERO_R1_F)
        , qsim(nullptr)
    {
        // Cut leading '{' and trailing '}'
        kwargs.erase(0U, 1U);
        kwargs.erase(kwargs.size() - 1U);
        // Cut leading, trailing, and extra spaces
        kwargs = trim(kwargs);

        std::map<std::string, int> keyMap;
        keyMap["'is_hybrid_stabilizer'"] = 1;
        keyMap["'is_tensor_network'"] = 2;
        keyMap["'is_schmidt_decomposed'"] = 3;
        keyMap["'is_schmidt_decomposition_parallel'"] = 4;
        keyMap["'is_qbdd'"] = 5;
        keyMap["'is_gpu'"] = 6;
        keyMap["'is_paged'"] = 7;
        keyMap["'is_hybrid_cpu_gpu'"] = 8;
        keyMap["'is_host_pointer'"] =9;
        keyMap["'noise'"] = 10;

        size_t pos;
        while ((pos = kwargs.find(":")) != std::string::npos) {
            std::string key = trim(kwargs.substr(0, pos));
            kwargs.erase(0, pos + 1U);
            pos = kwargs.find(",");
            std::string value = trim(kwargs.substr(0, pos));
            kwargs.erase(0, pos + 1U);
            const bool val = (value == "True");
            switch (keyMap[key]) {
                case 1:
                    sh = val;
                    break;
                case 2:
                    tn = val;
                    break;
                case 3:
                    sd = val;
                    break;
                case 4:
                    md = val;
                    break;
                case 5:
                    bdt = val;
                    break;
                case 6:
                    oc =  val;
                    break;
                case 7:
                    pg =  val;
                    break;
                case 8:
                    hy =  val;
                    break;
                case 9:
                    hp = val;
                    break;
                case 10:
                    noise_param = std::stof(value);
                    nw = noise_param > ZERO_R1;
                    break;
                default:
                    break;
            }
        }

        qsim = QSIM_CONFIG(0U);
        if (noise_param > ZERO_R1) {
            qsim->SetNoiseParameter(noise_param);
        }
    }

    QrackDevice &operator=(const QuantumDevice &) = delete;
    QrackDevice(const QrackDevice &) = delete;
    QrackDevice(QrackDevice &&) = delete;
    QrackDevice &operator=(QuantumDevice &&) = delete;

    auto AllocateQubit() -> QubitIdType override {
        const QubitIdType label = qsim->GetQubitCount();
        qsim->Allocate(1U);
        qubit_map[label] = (bitLenInt)label;
        return label;
    }
    auto AllocateQubits(size_t num_qubits) -> std::vector<QubitIdType> override {
        std::vector<QubitIdType> ids(num_qubits);
        for (size_t i = 0U; i < num_qubits; ++i) {
            ids[i] = AllocateQubit();
        }
        return ids;
    }
    [[nodiscard]] auto Zero() const -> Result override { return const_cast<Result>(&QRACK_RESULT_FALSE_CONST); }
    [[nodiscard]] auto One() const -> Result override { return const_cast<Result>(&QRACK_RESULT_TRUE_CONST); }
    auto Observable(ObsId id, const std::vector<std::complex<double>> &matrix,
                    const std::vector<QubitIdType> &wires) -> ObsIdType override
    {
        RT_FAIL_IF(wires.size() != 1U, "Cannot have observables besides tensor products of Pauli observables");
        auto &&dev_wires = getDeviceWires(wires);

        Qrack::Pauli basis = Qrack::PauliI;
        switch (id) {
            case ObsId::PauliX:
                basis = Qrack::PauliX;
                break;
            case ObsId::PauliY:
                basis = Qrack::PauliY;
                break;
            case ObsId::PauliZ:
                basis = Qrack::PauliZ;
                break;
            default:
                break;
        }
        std::vector<Qrack::Pauli> bases;
        bases.reserve(dev_wires.size());
        for (size_t i = 0U; i < dev_wires.size(); ++i) {
            bases.emplace_back(basis);
        }
        obs_cache.push_back(QrackObservable(bases, dev_wires));

        return obs_cache.size() - 1U;
    }
    auto TensorObservable(const std::vector<ObsIdType> &obs) -> ObsIdType override
    {
        if (!obs.size()) {
            return -1;
        }
        QrackObservable o;
        for (const ObsIdType& id : obs) {
            if (id >= obs_cache.size()) {
                throw std::invalid_argument("Observable ID not in device cache: " + std::to_string(id));
            }
            const QrackObservable& i = obs_cache[id];
            o.obs.insert(o.obs.end(), i.obs.begin(), i.obs.end());
            o.wires.insert(o.wires.end(), i.wires.begin(), i.wires.end());
        }
        obs_cache.push_back(o);

        return obs_cache.size() - 1U;
    }
    auto HamiltonianObservable(const std::vector<double> &coeffs, const std::vector<ObsIdType> &obs)
        -> ObsIdType override
    {
        return -1;
    }
    auto Measure(QubitIdType id, std::optional<int> postselect) -> Result override {
        bool *ret = (bool *)malloc(sizeof(bool));
        if (postselect.has_value()) {
            *ret = qsim->ForceM(id, postselect.value());
        } else {
            *ret = qsim->M(id);
        }
        return ret;
    }

    void ReleaseQubit(QubitIdType label) override
    {
        // Measure to prevent denormalization
        const bitLenInt id = qubit_map[label];
        qsim->M(id);
        // Deallocate
        qsim->Dispose(id, 1U);
    }
    void ReleaseAllQubits() override
    {
        // State vector is left empty
        qsim = QSIM_CONFIG(0U);
        if (noise_param > ZERO_R1) {
            qsim->SetNoiseParameter(noise_param);
        }
    }
    [[nodiscard]] auto GetNumQubits() const -> size_t override
    {
        return qsim->GetQubitCount();
    }
    void SetDeviceShots(size_t s) override {
        if ((s > 1U) && (noise_param > ZERO_R1_F)) {
            throw std::domain_error("Shots > 1 can't be simulated with noise on the Qrack back end! (Likely, you want to set mcm_method=\"one-shot\" on your qnode, with multiple shots.)");
        }
        shots = s;
    }
    [[nodiscard]] auto GetDeviceShots() const -> size_t override { return shots; }
    void StartTapeRecording() override { tapeRecording = true; }
    void StopTapeRecording() override { tapeRecording = false; }
    void PrintState() override
    {
        const size_t numQubits = qsim->GetQubitCount();
        const size_t maxQPower = (size_t)((uint64_t)qsim->GetMaxQPower());
        const size_t maxLcv = maxQPower - 1U;
        size_t idx = 0U;
        std::cout << "*** State-Vector of Size " << maxQPower << " ***" << std::endl;
        std::cout << "[";
        std::unique_ptr<Qrack::complex> sv(new Qrack::complex[maxQPower]);
        qsim->GetQuantumState(sv.get());
        for (; idx < maxLcv; ++idx) {
            std::cout << sv.get()[idx] << ", ";
        }
        std::cout << sv.get()[idx] << "]" << std::endl;
    }
    void NamedOperation(const std::string &name, const std::vector<double> &params,
                        const std::vector<QubitIdType> &wires, bool inverse,
                        const std::vector<QubitIdType> &controlled_wires,
                        const std::vector<bool> &controlled_values) override
    {
        // Check the validity of number of qubits and parameters
        RT_FAIL_IF(controlled_wires.size() != controlled_values.size(), "Controlled wires/values size mismatch");

        // Convert wires to device wires
        auto &&dev_wires = getDeviceWires(wires);
        auto &&dev_controlled_wires = getDeviceWires(controlled_wires);
        std::vector<bool> dev_controlled_values(controlled_values);
        if ((name == "MultiControlledX")
            || (name == "CNOT")
            || (name == "CY")
            || (name == "CZ")
            || (name == "ControlledPhaseShift")
            || (name == "CPhase")
            || (name == "CRX")
            || (name == "CRY")
            || (name == "CRZ")
            || (name == "CRot")
            || (name == "Toffoli")) {
            const size_t end = dev_wires.size() - 1U;
            dev_controlled_wires.insert(dev_controlled_wires.end(), dev_wires.begin(), dev_wires.begin() + end);
            dev_wires.erase(dev_wires.begin(), dev_wires.begin() + end);
            const std::vector<bool> t(end, true);
            dev_controlled_values.insert(dev_controlled_values.end(), t.begin(), t.end());
        } else if (name == "CSWAP") {
            const size_t end = dev_wires.size() - 2U;
            dev_controlled_wires.insert(dev_controlled_wires.end(), dev_wires.begin(), dev_wires.begin() + end);
            dev_wires.erase(dev_wires.begin(), dev_wires.begin() + end);
            const std::vector<bool> t(end, true);
            dev_controlled_values.insert(dev_controlled_values.end(), t.begin(), t.end());
        }

        // Update the state-vector
        if (dev_controlled_wires.empty()) {
            applyNamedOperation(name, dev_wires, inverse, params);
        } else {
            applyNamedOperation(name, dev_controlled_wires, dev_controlled_values, dev_wires, inverse, params);
        }
    }
    void MatrixOperation(const std::vector<std::complex<double>> &matrix,
                         const std::vector<QubitIdType> &wires, bool inverse,
                         const std::vector<QubitIdType> &controlled_wires,
                         const std::vector<bool> &controlled_values) override
    {
        RT_FAIL_IF(controlled_wires.size() != controlled_values.size(), "Controlled wires/values size mismatch");
        RT_FAIL_IF(wires.size() != 1U, "Matrix operation can only have one target qubit!");

        // Convert wires to device wires
        // with checking validity of wires
        auto &&dev_wires = getDeviceWires(wires);
        auto &&dev_controlled_wires = getDeviceWires(controlled_wires);
        const Qrack::complex mtrx[4U] = {
            (Qrack::complex)matrix[0U], (Qrack::complex)matrix[1U],
            (Qrack::complex)matrix[2U], (Qrack::complex)matrix[3U]
        };
        Qrack::complex inv[4U];
        Qrack::inv2x2(mtrx, inv);

        // Update the state-vector
        if (dev_controlled_wires.empty()) {
            qsim->Mtrx(inverse ? inv : mtrx, dev_wires[0U]);
        } else {
            bitCapInt controlPerm = Qrack::ZERO_BCI;
            for (bitLenInt i = 0U; i < controlled_values.size(); ++i) {
                if (controlled_values[i]) {
                    controlPerm = controlPerm | Qrack::pow2(i);
                }
            }
            qsim->UCMtrx(dev_controlled_wires, inverse ? inv : mtrx, dev_wires[0U], controlPerm);
        }
    }
    auto Expval(ObsIdType id) -> double override
    {
        if (id >= obs_cache.size()) {
            throw std::invalid_argument("Observable ID not in device cache: " + std::to_string(id));
        }
        const QrackObservable& obs = obs_cache[id];
        return qsim->ExpectationPauliAll(obs.wires, obs.obs);
    }
    auto Var(ObsIdType id) -> double override
    {
        if (id >= obs_cache.size()) {
            throw std::invalid_argument("Observable ID not in device cache: " + std::to_string(id));
        }
        const QrackObservable& obs = obs_cache[id];
        return qsim->VariancePauliAll(obs.wires, obs.obs);
    }
    void State(DataView<std::complex<double>, 1>& sv) override
    {
        RT_FAIL_IF(sv.size() != (size_t)((uint64_t)qsim->GetMaxQPower()), "Invalid size for the pre-allocated state vector");
        reverseWires();
#if FPPOW == 6
        qsim->GetQuantumState(&(*(sv.begin())));
#else
        std::unique_ptr<Qrack::complex> _sv(new Qrack::complex[sv.size()]);
        qsim->GetQuantumState(_sv.get());
        std::copy(_sv.get(), _sv.get() + sv.size(), sv.begin());
#endif
        reverseWires();
    }
    void Probs(DataView<double, 1>& p) override
    {
        RT_FAIL_IF(p.size() != (size_t)((uint64_t)qsim->GetMaxQPower()), "Invalid size for the pre-allocated probabilities vector");
        reverseWires();
#if FPPOW == 6
        qsim->GetProbs(&(*(p.begin())));
#else
        std::unique_ptr<Qrack::real1> _p(new Qrack::real1[p.size()]);
        qsim->GetProbs(_p.get());
        std::copy(_p.get(), _p.get() + p.size(), p.begin());
#endif
        reverseWires();
    }
    void PartialProbs(DataView<double, 1> &p, const std::vector<QubitIdType> &wires) override
    {
        RT_FAIL_IF((size_t)((uint64_t)Qrack::pow2(wires.size())) != p.size(), "Invalid size for the pre-allocated probabilities vector");
        auto &&dev_wires = getDeviceWires(wires);
        std::reverse(dev_wires.begin(), dev_wires.end());
#if FPPOW == 6
        qsim->ProbBitsAll(dev_wires, &(*(p.begin())));
#else
        std::unique_ptr<Qrack::real1> _p(new Qrack::real1[p.size()]);
        qsim->ProbBitsAll(dev_wires, _p.get());
        std::copy(_p.get(), _p.get() + p.size(), p.begin());
#endif
    }
    void _SampleBody(const size_t numQubits, const std::map<bitCapInt, int>& q_samples, DataView<double, 2> &samples)
    {
        auto samplesIter = samples.begin();
        auto q_samplesIter = q_samples.begin();
        for (auto q_samplesIter = q_samples.begin(); q_samplesIter != q_samples.end(); ++q_samplesIter) {
            const bitCapInt sample = q_samplesIter->first;
            int shots = q_samplesIter->second;
            for (; shots > 0; --shots) {
                for (size_t wire = 0U; wire < numQubits; ++wire) {
                    *(samplesIter++) = bi_compare_0((sample >> wire) & 1U) ? 1.0 : 0.0;
                }
            }
        }
    }
    void Sample(DataView<double, 2> &samples, size_t shots) override
    {
        RT_FAIL_IF(samples.size() != shots * qsim->GetQubitCount(), "Invalid size for the pre-allocated samples");

        if ((shots > 1U) && (noise_param > ZERO_R1_F)) {
            throw std::domain_error("Shots > 1 can't be simulated with noise on the Qrack back end! (Likely, you want to set mcm_method=\"one-shot\" on your qnode, with multiple shots.)");
        }

        if (shots == 1U) {
            const bitCapInt rev_sample = qsim->MAll();
            const bitLenInt numQubits = qsim->GetQubitCount();
            bitCapInt sample = Qrack::ZERO_BCI;
            for (bitLenInt i = 0U; i < numQubits; ++i) {
                if (bi_compare_0(rev_sample & Qrack::pow2(i)) != 0) {
                    sample = sample | Qrack::pow2(numQubits - (i + 1U));
                }
            }
            const std::map<bitCapInt, int> q_samples{{sample, 1}};
            _SampleBody(numQubits, q_samples, samples);
            return;
        }

        std::vector<bitCapInt> qPowers(qsim->GetQubitCount());
        for (bitLenInt i = 0U; i < qPowers.size(); ++i) {
            qPowers[i] = Qrack::pow2(i);
        }
        const std::map<bitCapInt, int> q_samples = qsim->MultiShotMeasureMask(qPowers, shots);
        _SampleBody(qPowers.size(), q_samples, samples);
    }
    void PartialSample(DataView<double, 2> &samples, const std::vector<QubitIdType> &wires, size_t shots) override
    {
        RT_FAIL_IF(samples.size() != shots * wires.size(), "Invalid size for the pre-allocated samples");

        if ((shots > 1U) && (noise_param > ZERO_R1_F)) {
            throw std::domain_error("Shots > 1 can't be simulated with noise on the Qrack back end! (Likely, you want to set mcm_method=\"one-shot\" on your qnode, with multiple shots.)");
        }

        auto &&dev_wires = getDeviceWires(wires);

        if (shots == 1U) {
            const bitCapInt rev_sample = qsim->MAll();
            const bitLenInt numQubits = dev_wires.size();
            bitCapInt sample = Qrack::ZERO_BCI;
            for (bitLenInt i = 0U; i < numQubits; ++i) {
                if (bi_compare_0(rev_sample & Qrack::pow2(dev_wires[i])) != 0) {
                    sample = sample | Qrack::pow2(numQubits - (i + 1U));
                }
            }
            const std::map<bitCapInt, int> q_samples{{sample, 1}};
            _SampleBody(numQubits, q_samples, samples);
            return;
        }

        std::vector<bitCapInt> qPowers(dev_wires.size());
        for (size_t i = 0U; i < qPowers.size(); ++i) {
            qPowers[i] = Qrack::pow2(dev_wires[i]);
        }
        const std::map<bitCapInt, int> q_samples = qsim->MultiShotMeasureMask(qPowers, shots);
        _SampleBody(qPowers.size(), q_samples, samples);
    }
    void _CountsBody(const size_t numQubits, const std::map<bitCapInt, int>& q_samples, DataView<int64_t, 1> &counts)
    {
        for (auto q_samplesIter = q_samples.begin(); q_samplesIter != q_samples.end(); ++q_samplesIter) {
            const bitCapInt sample = q_samplesIter->first;
            int shots = q_samplesIter->second;
            std::bitset<1U << QBCAPPOW> basisState;
            for (size_t wire = 0; wire < numQubits; wire++) {
                basisState[wire] = bi_compare_0((sample >> wire) & 1U);
            }
            for (; shots > 0; --shots) {
                ++counts(static_cast<size_t>(basisState.to_ulong()));
            }
        }
    }
    void Counts(DataView<double, 1> &eigvals, DataView<int64_t, 1> &counts,
                size_t shots) override
    {
        const size_t numQubits = qsim->GetQubitCount();
        const size_t numElements = 1U << numQubits;

        RT_FAIL_IF(eigvals.size() != numElements || counts.size() != numElements,
                   "Invalid size for the pre-allocated counts");

        std::map<bitCapInt, int> q_samples;
        if (shots == 1U) {
            const bitCapInt rev_sample = qsim->MAll();
            const bitLenInt numQubits = qsim->GetQubitCount();
            bitCapInt sample = Qrack::ZERO_BCI;
            for (bitLenInt i = 0U; i < numQubits; ++i) {
                if (bi_compare_0(rev_sample & Qrack::pow2(i)) != 0) {
                    sample = sample | Qrack::pow2(numQubits - (i + 1U));
                }
            }
            q_samples[sample] = 1;
        } else {
            std::vector<bitCapInt> qPowers(numQubits);
            for (bitLenInt i = 0U; i < qPowers.size(); ++i) {
                qPowers[i] = Qrack::pow2(i);
            }
            q_samples = qsim->MultiShotMeasureMask(qPowers, shots);
        }

        std::iota(eigvals.begin(), eigvals.end(), 0);
        std::fill(counts.begin(), counts.end(), 0);

        _CountsBody(numQubits, q_samples, counts);
    }

    void PartialCounts(DataView<double, 1> &eigvals, DataView<int64_t, 1> &counts,
                       const std::vector<QubitIdType> &wires, size_t shots) override
    {
        const size_t numQubits = wires.size();
        const size_t numElements = 1U << numQubits;

        RT_FAIL_IF(eigvals.size() != numElements || counts.size() != numElements,
                   "Invalid size for the pre-allocated counts");

        if ((shots > 1U) && (noise_param > ZERO_R1_F)) {
            throw std::domain_error("Shots > 1 can't be simulated with noise on the Qrack back end! (Likely, you want to set mcm_method=\"one-shot\" on your qnode, with multiple shots.)");
        }

        auto &&dev_wires = getDeviceWires(wires);

        std::map<bitCapInt, int> q_samples;
        if (shots == 1U) {
            const bitCapInt rev_sample = qsim->MAll();
            const bitLenInt numQubits = dev_wires.size();
            bitCapInt sample = Qrack::ZERO_BCI;
            for (bitLenInt i = 0U; i < numQubits; ++i) {
                if (bi_compare_0(rev_sample & Qrack::pow2(dev_wires[i])) != 0) {
                    sample = sample | Qrack::pow2(numQubits - (i + 1U));
                }
            }
            q_samples[sample] = 1;
        } else {
            std::vector<bitCapInt> qPowers(dev_wires.size());
            for (size_t i = 0U; i < qPowers.size(); ++i) {
                qPowers[i] = Qrack::pow2(dev_wires[qPowers.size() - (i + 1U)]);
            }
            q_samples = qsim->MultiShotMeasureMask(qPowers, shots);
        }

        std::iota(eigvals.begin(), eigvals.end(), 0);
        std::fill(counts.begin(), counts.end(), 0);

        _CountsBody(numQubits, q_samples, counts);
    }

    void Gradient(std::vector<DataView<double, 1>> &, const std::vector<size_t> &) override {}
};

GENERATE_DEVICE_FACTORY(QrackDevice, QrackDevice);
