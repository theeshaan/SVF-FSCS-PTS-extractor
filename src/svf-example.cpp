//===- cxt-pts.cpp -- Context-sensitive points-to dumper -----------------===//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2022>  <Yulei Sui>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

#include "DDA/ContextDDA.h"
#include "DDA/DDAClient.h"
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/Options.h"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace SVF;
using namespace llvm;

namespace
{

std::string findExtAPIPath()
{
    if (const char* svfPath = std::getenv("SVF_PATH"))
    {
        std::filesystem::path candidate = std::filesystem::path(svfPath) / ".." / "lib" / "extapi.bc";
        if (std::filesystem::exists(candidate))
            return candidate.lexically_normal().string();
    }

    const std::vector<std::filesystem::path> fallbacks = {
        std::filesystem::path("Release-build/lib/extapi.bc"),
        std::filesystem::path("Debug-build/lib/extapi.bc"),
        std::filesystem::path("../SVF/Release-build/lib/extapi.bc"),
        std::filesystem::path("../SVF/Debug-build/lib/extapi.bc")
    };

    for (const auto& candidate : fallbacks)
    {
        if (std::filesystem::exists(candidate))
            return candidate.lexically_normal().string();
    }

    return "";
}

std::string renderSVFVar(const SVFVar* var)
{
    std::string text = var->toString();
    for (char& ch : text)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
            ch = ' ';
    }
    return text;
}

std::string renderValueName(const Value* value)
{
    if (value == nullptr)
        return "(unknown)";

    std::string name = value->getName().str();
    if (!name.empty())
        return name;

    LLVMModuleSet* llvmModuleSet = LLVMModuleSet::getLLVMModuleSet();
    if (llvmModuleSet->hasValueNode(value))
        return renderSVFVar(SVFIR::getPAG()->getSVFVar(llvmModuleSet->getValueNode(value)));

    return "(unnamed)";
}

std::string renderObjectName(const SVFVar* obj)
{
    if (obj == nullptr)
        return "(unknown)";

    std::string name = obj->getName();
    if (!name.empty())
        return name;

    return renderSVFVar(obj);
}

struct PointerVariable
{
    std::string name;
    const Value* storage = nullptr;
    const Value* finalValue = nullptr;
};

bool isTrackedPointerStorage(const Value* value)
{
    if (const auto* allocaInst = dyn_cast<AllocaInst>(value))
        return allocaInst->getAllocatedType()->isPointerTy();

    if (const auto* globalVar = dyn_cast<GlobalVariable>(value))
        return globalVar->getValueType()->isPointerTy();

    return false;
}

std::vector<PointerVariable> collectPointerVariables()
{
    LLVMModuleSet* llvmModuleSet = LLVMModuleSet::getLLVMModuleSet();
    std::vector<PointerVariable> variables;
    std::map<const Value*, std::size_t> indexByStorage;

    for (Module& module : llvmModuleSet->getLLVMModules())
    {
        for (GlobalVariable& global : module.globals())
        {
            if (!isTrackedPointerStorage(&global))
                continue;

            PointerVariable variable;
            variable.name = renderValueName(&global);
            variable.storage = &global;
            indexByStorage[variable.storage] = variables.size();
            variables.push_back(variable);
        }

        for (Function& function : module)
        {
            for (Instruction& inst : instructions(function))
            {
                if (!isa<AllocaInst>(inst) || !isTrackedPointerStorage(&inst))
                    continue;

                PointerVariable variable;
                variable.name = renderValueName(&inst);
                variable.storage = &inst;
                indexByStorage[variable.storage] = variables.size();
                variables.push_back(variable);
            }
        }
    }

    for (Module& module : llvmModuleSet->getLLVMModules())
    {
        for (Function& function : module)
        {
            for (Instruction& inst : instructions(function))
            {
                const auto* store = dyn_cast<StoreInst>(&inst);
                if (store == nullptr || !store->getValueOperand()->getType()->isPointerTy())
                    continue;

                const Value* storage = store->getPointerOperand()->stripPointerCasts();
                auto found = indexByStorage.find(storage);
                if (found == indexByStorage.end())
                    continue;

                variables[found->second].finalValue = store->getValueOperand()->stripPointerCasts();
            }
        }
    }

    return variables;
}

std::string renderPointsToSet(PointerAnalysis* pta, NodeID pointerId)
{
    const PointsTo& pts = pta->getPts(pointerId);
    if (pts.empty())
        return "(empty)";

    std::set<std::string> renderedTargets;
    for (NodeID objId : pts)
        renderedTargets.insert(renderObjectName(pta->getPAG()->getSVFVar(objId)));

    std::ostringstream oss;
    bool first = true;
    for (const std::string& target : renderedTargets)
    {
        if (!first)
            oss << ", ";
        first = false;
        oss << target;
    }
    return oss.str();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        errs() << "Usage: " << argv[0] << " <input.bc> [additional SVF options]\n";
        errs() << "Runs flow- and context-sensitive DDA and prints: Pointer<TAB>Pointees.\n";
        return 1;
    }

    std::vector<std::string> args = {
        argv[0],
        "-query=all",
        "-cxt",
        "-stat=false",
        "-print-query-pts=false",
        "-print-all-pts=false"
    };

    const std::string extapi = findExtAPIPath();
    if (!extapi.empty())
        args.push_back("-extapi=" + extapi);

    for (int i = 2; i < argc; ++i)
        args.emplace_back(argv[i]);
    args.emplace_back(argv[1]);

    std::vector<char*> optionArgv;
    optionArgv.reserve(args.size());
    for (std::string& arg : args)
        optionArgv.push_back(arg.data());

    std::vector<std::string> moduleNameVec = OptionBase::parseOptions(
        static_cast<int>(optionArgv.size()),
        optionArgv.data(),
        "Flow- and Context-Sensitive Points-to Dumper",
        "[options] <input-bitcode...>");

    LLVMModuleSet::buildSVFModule(moduleNameVec);

    SVFIRBuilder builder;
    SVFIR* pag = builder.build();

    auto client = std::make_unique<DDAClient>();
    std::unique_ptr<ContextDDA> pta = std::make_unique<ContextDDA>(pag, client.get());
    LLVMModuleSet* llvmModuleSet = LLVMModuleSet::getLLVMModuleSet();

    pta->initialize();

    std::vector<PointerVariable> variables = collectPointerVariables();
    std::vector<std::pair<std::string, NodeID>> queries;
    std::set<NodeID> analyzedNodeIds;

    for (const PointerVariable& variable : variables)
    {
        if (variable.finalValue == nullptr || !llvmModuleSet->hasValueNode(variable.finalValue))
            continue;

        NodeID rhsNodeId = llvmModuleSet->getValueNode(variable.finalValue);
        const SVFVar* rhsNode = pag->getSVFVar(rhsNodeId);
        if (rhsNode == nullptr || !pag->isValidTopLevelPtr(rhsNode))
            continue;

        queries.emplace_back(variable.name, rhsNodeId);
        analyzedNodeIds.insert(rhsNodeId);
    }

    for (NodeID nodeId : analyzedNodeIds)
        pta->computeDDAPts(nodeId);

    pta->finalize();

    outs() << "Pointer\tPointees\n";
    for (const auto& query : queries)
    {
        outs() << query.first << '\t' << renderPointsToSet(pta.get(), query.second) << '\n';
    }

    pta.reset();
    client.reset();

    SVFIR::releaseSVFIR();
    LLVMModuleSet::releaseLLVMModuleSet();
    llvm_shutdown();
    return 0;
}