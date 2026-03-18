#include "Graphs/SVFG.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"
#include "DDA/DDAPass.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "WPA/FlowSensitive.h"
#include "MSSA/SVFGBuilder.h"

using namespace llvm;
using namespace std;
using namespace SVF;

/*!
 * An example to print points-to set of an LLVM value
 */
std::string printPts(PointerAnalysis *pta, const SVFVar *svfval) {

  std::string str;
  raw_string_ostream rawstr(str);

  NodeID pNodeId = svfval->getId();
  const PointsTo &pts = pta->getPts(pNodeId);
  for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
    rawstr << " " << *ii << " ";
    const SVFVar *targetObj = pta->getPAG()->getSVFVar(*ii);
    rawstr << "(" << targetObj->toString() << ")\t ";
    if (*ii == 10 || *ii == 11)
        rawstr << " <-- this is the node we're interested in! ";
  }

  return rawstr.str();
}

std::string printPts(PointerAnalysis *pta, const NodeID svfval_nodeID) {

  std::string str;
  raw_string_ostream rawstr(str);

  NodeID pNodeId = svfval_nodeID;
  const PointsTo &pts = pta->getPts(pNodeId);
  for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
    rawstr << " " << *ii << " ";
    const SVFVar *targetObj = pta->getPAG()->getSVFVar(*ii);
    rawstr << "(" << targetObj->toString() << ")\t ";
    if (*ii == 10 || *ii == 11)
        rawstr << " <-- this is the node we're interested in! ";
  }

  return rawstr.str();
}


int main(int argc, char **argv) {

  std::vector<std::string> moduleNameVec;
  moduleNameVec =
      OptionBase::parseOptions(argc, argv, "Whole Program Points-to Analysis",
                               "[options] <input-bitcode...>");

  LLVMModuleSet::buildSVFModule(moduleNameVec);

  /// Build Program Assignment Graph (SVFIR)
  SVFIRBuilder builder;
  SVFIR *pag = builder.build();

  // E: Run flow-sensitive analysis
  FlowSensitive* fspta = FlowSensitive::createFSWPA(pag);

  PointerAnalysis *pta = fspta;

  // for (auto it = pag->begin(); it != pag->end(); ++it)
  // {
  //   unsigned int id = it->first;
  //   const SVFVar *var = pag->getSVFVar(id);

  //   // we're only interested in pointer variables
  //   if (!var->isPointer()) continue;

  //   const PointsTo &pts = pta->getPts(var->getId());
  //   if (pts.empty()) continue;

  //   cout << "====================================" << endl;
  //   cout << "Variable: " << var->toString() << endl;
  //   cout << "Points-to: " << printPts(pta, var) << endl;
  // }

  // using the SVFIR::getSVFVarMap function as suggested by the author
  const auto id_to_node_map = pag->getSVFVarMap(); // it is a map from NodeID to NodeType*
  for (const auto& entry : id_to_node_map) {
    NodeID id = entry.first;
    const SVFVar* var = pag->getSVFVar(id);

    // we're only interested in pointer variables
    if (!var->isPointer()) continue;

    const PointsTo &pts = pta->getPts(var->getId());
    if (pts.empty()) continue;

    cout << "====================================" << endl;
    cout << "Variable: " << var->toString() << endl;
    cout << "Points-to: " << printPts(pta, var) << endl;
  }

  cout << "====================================" << endl;
  cout << "====================================" << endl;
  cout << "====================================" << endl;
  cout << "====================================" << endl;



  for (int i=1; i<=18; ++i)
  {
    cout << "Variable: " << i << endl;
    cout << "Points-to: " << printPts(pta, i) << endl;

  }
  
  // // clean up memory
  SVFIR::releaseSVFIR();
  FlowSensitive::releaseFSWPA();

  LLVMModuleSet::getLLVMModuleSet()->dumpModulesToFile(".svf.bc");
  SVF::LLVMModuleSet::releaseLLVMModuleSet();

  llvm::llvm_shutdown();
  return 0;
}
