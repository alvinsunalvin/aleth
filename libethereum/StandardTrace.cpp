// Aleth: Ethereum C++ client, tools and libraries.
// Copyright 2014-2019 Aleth Authors.
// Licensed under the GNU General Public License, Version 3.

#include "StandardTrace.h"
#include "ExtVM.h"
#include <libevm/LegacyVM.h>
#include <numeric>

namespace dev
{
namespace eth
{
namespace
{
bool changesMemory(Instruction _inst)
{
    return _inst == Instruction::MSTORE || _inst == Instruction::MSTORE8 ||
           _inst == Instruction::MLOAD || _inst == Instruction::CREATE ||
           _inst == Instruction::CALL || _inst == Instruction::CALLCODE ||
           _inst == Instruction::SHA3 || _inst == Instruction::CALLDATACOPY ||
           _inst == Instruction::CODECOPY || _inst == Instruction::EXTCODECOPY ||
           _inst == Instruction::DELEGATECALL;
}
}  // namespace

StandardTrace::StandardTrace() : m_trace(Json::arrayValue) {}

bool changesStorage(Instruction _inst)
{
    return _inst == Instruction::SSTORE;
}

void StandardTrace::operator()(uint64_t _steps, uint64_t PC, Instruction inst, bigint newMemSize,
    bigint gasCost, bigint gas, VMFace const* _vm, ExtVMFace const* voidExt)
{
    (void)_steps;

    ExtVM const& ext = dynamic_cast<ExtVM const&>(*voidExt);
    auto vm = dynamic_cast<LegacyVM const*>(_vm);

    Json::Value r(Json::objectValue);

    Json::Value stack(Json::arrayValue);
    if (vm && !m_options.disableStack)
    {
        // Try extracting information about the stack from the VM is supported.
        for (auto const& i : vm->stack())
            stack.append(toCompactHexPrefixed(i, 1));
        r["stack"] = stack;
    }

    bool newContext = false;
    Instruction lastInst = Instruction::STOP;

    if (m_lastInst.size() == ext.depth)
    {
        // starting a new context
        assert(m_lastInst.size() == ext.depth);
        m_lastInst.push_back(inst);
        newContext = true;
    }
    else if (m_lastInst.size() == ext.depth + 2)
    {
        m_lastInst.pop_back();
        lastInst = m_lastInst.back();
    }
    else if (m_lastInst.size() == ext.depth + 1)
    {
        // continuing in previous context
        lastInst = m_lastInst.back();
        m_lastInst.back() = inst;
    }
    else
    {
        cwarn << "GAA!!! Tracing VM and more than one new/deleted stack frame between steps!";
        cwarn << "Attmepting naive recovery...";
        m_lastInst.resize(ext.depth + 1);
    }

    Json::Value memJson(Json::arrayValue);
    if (vm && !m_options.disableMemory && (changesMemory(lastInst) || newContext))
    {
        for (unsigned i = 0; i < vm->memory().size(); i += 32)
        {
            bytesConstRef memRef(vm->memory().data() + i, 32);
            memJson.append(toHex(memRef));
        }
        r["memory"] = memJson;
    }

    if (!m_options.disableStorage &&
        (m_options.fullStorage || changesStorage(lastInst) || newContext))
    {
        Json::Value storage(Json::objectValue);
        for (auto const& i : ext.state().storage(ext.myAddress))
            storage[toCompactHexPrefixed(i.second.first, 1)] =
                toCompactHexPrefixed(i.second.second, 1);
        r["storage"] = storage;
    }

    if (m_showMnemonics)
        r["op"] = instructionInfo(inst).name;
    r["pc"] = toString(PC);
    r["gas"] = toString(gas);
    r["gasCost"] = toString(gasCost);
    r["depth"] = toString(ext.depth);
    if (!!newMemSize)
        r["memexpand"] = toString(newMemSize);

    m_trace.append(r);
}

std::string StandardTrace::styledJson() const
{
    return Json::StyledWriter().write(m_trace);
}

std::string StandardTrace::multilineTrace() const
{
    if (m_trace.empty())
        return {};

    // Each opcode trace on a separate line
    return std::accumulate(std::next(m_trace.begin()), m_trace.end(),
        Json::FastWriter().write(m_trace[0]),
        [](std::string a, Json::Value b) { return a + Json::FastWriter().write(b); });
}
}  // namespace eth
}  // namespace dev