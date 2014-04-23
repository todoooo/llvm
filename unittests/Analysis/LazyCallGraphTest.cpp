//===- LazyCallGraphTest.cpp - Unit tests for the lazy CG analysis --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"
#include <memory>

using namespace llvm;

namespace {

std::unique_ptr<Module> parseAssembly(const char *Assembly) {
  auto M = make_unique<Module>("Module", getGlobalContext());

  SMDiagnostic Error;
  bool Parsed =
      ParseAssemblyString(Assembly, M.get(), Error, M->getContext()) == M.get();

  std::string ErrMsg;
  raw_string_ostream OS(ErrMsg);
  Error.print("", OS);

  // A failure here means that the test itself is buggy.
  if (!Parsed)
    report_fatal_error(OS.str().c_str());

  return M;
}

// IR forming a call graph with a diamond of triangle-shaped SCCs:
//
//         d1
//        /  \
//       d3--d2
//      /     \
//     b1     c1
//   /  \    /  \
//  b3--b2  c3--c2
//       \  /
//        a1
//       /  \
//      a3--a2
//
// All call edges go up between SCCs, and clockwise around the SCC.
static const char DiamondOfTriangles[] =
     "define void @a1() {\n"
     "entry:\n"
     "  call void @a2()\n"
     "  call void @b2()\n"
     "  call void @c3()\n"
     "  ret void\n"
     "}\n"
     "define void @a2() {\n"
     "entry:\n"
     "  call void @a3()\n"
     "  ret void\n"
     "}\n"
     "define void @a3() {\n"
     "entry:\n"
     "  call void @a1()\n"
     "  ret void\n"
     "}\n"
     "define void @b1() {\n"
     "entry:\n"
     "  call void @b2()\n"
     "  call void @d3()\n"
     "  ret void\n"
     "}\n"
     "define void @b2() {\n"
     "entry:\n"
     "  call void @b3()\n"
     "  ret void\n"
     "}\n"
     "define void @b3() {\n"
     "entry:\n"
     "  call void @b1()\n"
     "  ret void\n"
     "}\n"
     "define void @c1() {\n"
     "entry:\n"
     "  call void @c2()\n"
     "  call void @d2()\n"
     "  ret void\n"
     "}\n"
     "define void @c2() {\n"
     "entry:\n"
     "  call void @c3()\n"
     "  ret void\n"
     "}\n"
     "define void @c3() {\n"
     "entry:\n"
     "  call void @c1()\n"
     "  ret void\n"
     "}\n"
     "define void @d1() {\n"
     "entry:\n"
     "  call void @d2()\n"
     "  ret void\n"
     "}\n"
     "define void @d2() {\n"
     "entry:\n"
     "  call void @d3()\n"
     "  ret void\n"
     "}\n"
     "define void @d3() {\n"
     "entry:\n"
     "  call void @d1()\n"
     "  ret void\n"
     "}\n";

TEST(LazyCallGraphTest, BasicGraphFormation) {
  std::unique_ptr<Module> M = parseAssembly(DiamondOfTriangles);
  LazyCallGraph CG(*M);

  // The order of the entry nodes should be stable w.r.t. the source order of
  // the IR, and everything in our module is an entry node, so just directly
  // build variables for each node.
  auto I = CG.begin();
  LazyCallGraph::Node *A1 = *I++;
  EXPECT_EQ("a1", A1->getFunction().getName());
  LazyCallGraph::Node *A2 = *I++;
  EXPECT_EQ("a2", A2->getFunction().getName());
  LazyCallGraph::Node *A3 = *I++;
  EXPECT_EQ("a3", A3->getFunction().getName());
  LazyCallGraph::Node *B1 = *I++;
  EXPECT_EQ("b1", B1->getFunction().getName());
  LazyCallGraph::Node *B2 = *I++;
  EXPECT_EQ("b2", B2->getFunction().getName());
  LazyCallGraph::Node *B3 = *I++;
  EXPECT_EQ("b3", B3->getFunction().getName());
  LazyCallGraph::Node *C1 = *I++;
  EXPECT_EQ("c1", C1->getFunction().getName());
  LazyCallGraph::Node *C2 = *I++;
  EXPECT_EQ("c2", C2->getFunction().getName());
  LazyCallGraph::Node *C3 = *I++;
  EXPECT_EQ("c3", C3->getFunction().getName());
  LazyCallGraph::Node *D1 = *I++;
  EXPECT_EQ("d1", D1->getFunction().getName());
  LazyCallGraph::Node *D2 = *I++;
  EXPECT_EQ("d2", D2->getFunction().getName());
  LazyCallGraph::Node *D3 = *I++;
  EXPECT_EQ("d3", D3->getFunction().getName());
  EXPECT_EQ(CG.end(), I);

  // Build vectors and sort them for the rest of the assertions to make them
  // independent of order.
  std::vector<std::string> Nodes;

  for (LazyCallGraph::Node *N : *A1)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("a2", Nodes[0]);
  EXPECT_EQ("b2", Nodes[1]);
  EXPECT_EQ("c3", Nodes[2]);
  Nodes.clear();

  EXPECT_EQ(A2->end(), std::next(A2->begin()));
  EXPECT_EQ("a3", A2->begin()->getFunction().getName());
  EXPECT_EQ(A3->end(), std::next(A3->begin()));
  EXPECT_EQ("a1", A3->begin()->getFunction().getName());

  for (LazyCallGraph::Node *N : *B1)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("b2", Nodes[0]);
  EXPECT_EQ("d3", Nodes[1]);
  Nodes.clear();

  EXPECT_EQ(B2->end(), std::next(B2->begin()));
  EXPECT_EQ("b3", B2->begin()->getFunction().getName());
  EXPECT_EQ(B3->end(), std::next(B3->begin()));
  EXPECT_EQ("b1", B3->begin()->getFunction().getName());

  for (LazyCallGraph::Node *N : *C1)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("c2", Nodes[0]);
  EXPECT_EQ("d2", Nodes[1]);
  Nodes.clear();

  EXPECT_EQ(C2->end(), std::next(C2->begin()));
  EXPECT_EQ("c3", C2->begin()->getFunction().getName());
  EXPECT_EQ(C3->end(), std::next(C3->begin()));
  EXPECT_EQ("c1", C3->begin()->getFunction().getName());

  EXPECT_EQ(D1->end(), std::next(D1->begin()));
  EXPECT_EQ("d2", D1->begin()->getFunction().getName());
  EXPECT_EQ(D2->end(), std::next(D2->begin()));
  EXPECT_EQ("d3", D2->begin()->getFunction().getName());
  EXPECT_EQ(D3->end(), std::next(D3->begin()));
  EXPECT_EQ("d1", D3->begin()->getFunction().getName());

  // Now lets look at the SCCs.
  auto SCCI = CG.postorder_scc_begin();

  LazyCallGraph::SCC *D = *SCCI++;
  for (LazyCallGraph::Node *N : *D)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("d1", Nodes[0]);
  EXPECT_EQ("d2", Nodes[1]);
  EXPECT_EQ("d3", Nodes[2]);
  EXPECT_EQ(3u, Nodes.size());
  Nodes.clear();

  LazyCallGraph::SCC *C = *SCCI++;
  for (LazyCallGraph::Node *N : *C)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("c1", Nodes[0]);
  EXPECT_EQ("c2", Nodes[1]);
  EXPECT_EQ("c3", Nodes[2]);
  EXPECT_EQ(3u, Nodes.size());
  Nodes.clear();

  LazyCallGraph::SCC *B = *SCCI++;
  for (LazyCallGraph::Node *N : *B)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("b1", Nodes[0]);
  EXPECT_EQ("b2", Nodes[1]);
  EXPECT_EQ("b3", Nodes[2]);
  EXPECT_EQ(3u, Nodes.size());
  Nodes.clear();

  LazyCallGraph::SCC *A = *SCCI++;
  for (LazyCallGraph::Node *N : *A)
    Nodes.push_back(N->getFunction().getName());
  std::sort(Nodes.begin(), Nodes.end());
  EXPECT_EQ("a1", Nodes[0]);
  EXPECT_EQ("a2", Nodes[1]);
  EXPECT_EQ("a3", Nodes[2]);
  EXPECT_EQ(3u, Nodes.size());
  Nodes.clear();

  EXPECT_EQ(CG.postorder_scc_end(), SCCI);
}

}
