/**------------------------------------------------------------------------
 * All Rights Reserved
 * Author: Diego Macrini
 *-----------------------------------------------------------------------*/
#include <RootHeader.h>
#include "LemonBGMatcher.h"
#include <lemon/matching.h>
#include <limits>

using namespace vpl;

typedef LemonBGMatcher BGM;

const double BGM::DOUBLE_MAX = 1000000; //std::numeric_limits<double>::max() * 0.5;

LemonBGMatcher::~LemonBGMatcher()
{
	delete m_pWeightMap;
	delete m_pIndexMap;
}

void BGM::Init(unsigned numNodes1, unsigned numNodes2)
{
	ASSERT(!m_pIndexMap);

	unsigned i, j;
	
	m_nodes1.resize(numNodes1);
	m_nodes2.resize(numNodes2);
	m_edges.resize(numNodes1, numNodes2);

	m_bg.reserveNode(numNodes1 + numNodes2);
	m_bg.reserveEdge(numNodes1 * numNodes2);

	// Map indices to nodes
	for (i = 0; i < numNodes1; i++)
		m_nodes1[i] = m_bg.addNode();

	for (j = 0; j < numNodes2; j++)
		m_nodes2[j] = m_bg.addNode();

	// Map node-index pairs to edges
	for (i = 0; i < numNodes1; i++)
		for (j = 0; j < numNodes2; j++)
			m_edges(i, j) = m_bg.addEdge(m_nodes1[i], m_nodes2[j]);

	// Map nodes to indices
	m_pIndexMap = new Indices(m_bg);

	for (i = 0; i < numNodes1; i++)
		(*m_pIndexMap)[m_nodes1[i]] = i;

	for (j = 0; j < numNodes2; j++)
		(*m_pIndexMap)[m_nodes2[j]] = j;
}

/*!
	Since the input patrix contains costs, the values are
	negated, so tha max weight is min cost.

	m_hasNegatedWeights is set to true.
*/
void BGM::SetEdgeCosts(const Matrix& costMat)
{
	ASSERT(m_edges.ni() == costMat.rows() && 
		m_edges.nj() == costMat.cols());

	if (!m_pWeightMap)
		m_pWeightMap = new Graph::EdgeMap<double>(m_bg);

	Graph::EdgeMap<double>& w = *m_pWeightMap;

	unsigned i, j;

	// Store the fact that the weights are negated versions of
	// the given costs
	m_hasNegatedWeights = true;

	for (i = 0; i < costMat.rows(); i++)
		for (j = 0; j < costMat.cols(); j++)
			w[m_edges(i, j)] = DOUBLE_MAX - costMat(i, j);
}

/*!
	rowMap(r) = c means that col c is assigned to row r.

	colMap(c) = r means that row r is assigned to column c.
*/
double BGM::SolveMaxWeightedMatching(UIntVector* pRowMap, UIntVector* pColMap)
{
	ASSERT(m_pWeightMap);

	lemon::MaxWeightedMatching<Graph, Weights> mwm(m_bg, *m_pWeightMap);
    
	mwm.run();

	if (pRowMap)
	{
		for (unsigned i = 0; i < m_nodes1.size(); i++)
			(*pRowMap)[i] = (*m_pIndexMap)[mwm.mate(m_nodes1[i])];
	}

	if (pColMap)
	{
		for (unsigned j = 0; j < m_nodes2.size(); j++)
			(*pColMap)[j] = (*m_pIndexMap)[mwm.mate(m_nodes2[j])];
	}

	double w = mwm.matchingWeight();

	return (m_hasNegatedWeights) ? 
		DOUBLE_MAX - mwm.matchingWeight() : mwm.matchingWeight();
}

/*#include "ObjectRecognizer.h"
#include "ObjectLearner.h"
#include "ExactTreeMatcher.h"
#include <ShapeParsing/ShapeParser.h>
#include <Tools/UserArguments.h>
#include <VideoParserGUI/DrawingUtils.h>*/

//using namespace vpl;

/*#include <iostream>
#include <sstream>
#include <vector>
#include <queue>
#include <cstdlib>

#include <lemon/matching.h>
#include <lemon/smart_graph.h>
#include <lemon/concepts/graph.h>
#include <lemon/concepts/maps.h>
#include <lemon/lgf_reader.h>
#include <lemon/math.h>

#define check(rc, msg)                                                  \
  {                                                                     \
    if(!(rc)) {                                                         \
      std::cerr << __FILE__ ":" << __LINE__ << ": error: "              \
                << msg << std::endl;                                    \
      abort();                                                          \
    } else { }                                                          \
  }    

using namespace std;
using namespace lemon;

GRAPH_TYPEDEFS(SmartGraph);


const int lgfn = 3;
const std::string lgf[lgfn] = {
  "@nodes\n"
  "label\n"
  "0\n"
  "1\n"
  "2\n"
  "3\n"
  "4\n"
  "5\n"
  "6\n"
  "7\n"
  "@edges\n"
  "     label  weight\n"
  "7 4  0      984\n"
  "0 7  1      73\n"
  "7 1  2      204\n"
  "2 3  3      583\n"
  "2 7  4      565\n"
  "2 1  5      582\n"
  "0 4  6      551\n"
  "2 5  7      385\n"
  "1 5  8      561\n"
  "5 3  9      484\n"
  "7 5  10     904\n"
  "3 6  11     47\n"
  "7 6  12     888\n"
  "3 0  13     747\n"
  "6 1  14     310\n",

  "@nodes\n"
  "label\n"
  "0\n"
  "1\n"
  "2\n"
  "3\n"
  "4\n"
  "5\n"
  "6\n"
  "7\n"
  "@edges\n"
  "     label  weight\n"
  "2 5  0      710\n"
  "0 5  1      241\n"
  "2 4  2      856\n"
  "2 6  3      762\n"
  "4 1  4      747\n"
  "6 1  5      962\n"
  "4 7  6      723\n"
  "1 7  7      661\n"
  "2 3  8      376\n"
  "1 0  9      416\n"
  "6 7  10     391\n",

  "@nodes\n"
  "label\n"
  "0\n"
  "1\n"
  "2\n"
  "3\n"
  "4\n"
  "5\n"
  "6\n"
  "7\n"
  "@edges\n"
  "     label  weight\n"
  "6 2  0      553\n"
  "0 7  1      653\n"
  "6 3  2      22\n"
  "4 7  3      846\n"
  "7 2  4      981\n"
  "7 6  5      250\n"
  "5 2  6      539\n",
};

void checkMaxMatchingCompile()
{
  typedef concepts::Graph Graph;
  typedef Graph::Node Node;
  typedef Graph::Edge Edge;
  typedef Graph::EdgeMap<bool> MatMap;

  Graph g;
  Node n;
  Edge e;
  MatMap mat(g);

  MaxMatching<Graph> mat_test(g);
  const MaxMatching<Graph>&
    const_mat_test = mat_test;

  mat_test.init();
  mat_test.greedyInit();
  mat_test.matchingInit(mat);
  mat_test.startSparse();
  mat_test.startDense();
  mat_test.run();

  const_mat_test.matchingSize();
  const_mat_test.matching(e);
  const_mat_test.matching(n);
  const MaxMatching<Graph>::MatchingMap& mmap =
    const_mat_test.matchingMap();
  e = mmap[n];
  const_mat_test.mate(n);

  MaxMatching<Graph>::Status stat =
    const_mat_test.status(n);
  const MaxMatching<Graph>::StatusMap& smap =
    const_mat_test.statusMap();
  stat = smap[n];
  const_mat_test.barrier(n);
}

void checkMaxWeightedMatchingCompile()
{
  typedef concepts::Graph Graph;
  typedef Graph::Node Node;
  typedef Graph::Edge Edge;
  typedef Graph::EdgeMap<int> WeightMap;

  Graph g;
  Node n;
  Edge e;
  WeightMap w(g);

  MaxWeightedMatching<Graph> mat_test(g, w);
  const MaxWeightedMatching<Graph>&
    const_mat_test = mat_test;

  mat_test.init();
  mat_test.start();
  mat_test.run();

  const_mat_test.matchingWeight();
  const_mat_test.matchingSize();
  const_mat_test.matching(e);
  const_mat_test.matching(n);
  const MaxWeightedMatching<Graph>::MatchingMap& mmap =
    const_mat_test.matchingMap();
  e = mmap[n];
  const_mat_test.mate(n);

  int k = 0;
  const_mat_test.dualValue();
  const_mat_test.nodeValue(n);
  const_mat_test.blossomNum();
  const_mat_test.blossomSize(k);
  const_mat_test.blossomValue(k);
}

void checkMaxWeightedPerfectMatchingCompile()
{
  typedef concepts::Graph Graph;
  typedef Graph::Node Node;
  typedef Graph::Edge Edge;
  typedef Graph::EdgeMap<int> WeightMap;

  Graph g;
  Node n;
  Edge e;
  WeightMap w(g);

  MaxWeightedPerfectMatching<Graph> mat_test(g, w);
  const MaxWeightedPerfectMatching<Graph>&
    const_mat_test = mat_test;

  mat_test.init();
  mat_test.start();
  mat_test.run();

  const_mat_test.matchingWeight();
  const_mat_test.matching(e);
  const_mat_test.matching(n);
  const MaxWeightedPerfectMatching<Graph>::MatchingMap& mmap =
    const_mat_test.matchingMap();
  e = mmap[n];
  const_mat_test.mate(n);

  int k = 0;
  const_mat_test.dualValue();
  const_mat_test.nodeValue(n);
  const_mat_test.blossomNum();
  const_mat_test.blossomSize(k);
  const_mat_test.blossomValue(k);
}

void checkMatching(const SmartGraph& graph,
                   const MaxMatching<SmartGraph>& mm) {
  int num = 0;

  IntNodeMap comp_index(graph);
  UnionFind<IntNodeMap> comp(comp_index);

  int barrier_num = 0;

  for (NodeIt n(graph); n != INVALID; ++n) {
    check(mm.status(n) == MaxMatching<SmartGraph>::EVEN ||
          mm.matching(n) != INVALID, "Wrong Gallai-Edmonds decomposition");
    if (mm.status(n) == MaxMatching<SmartGraph>::ODD) {
      ++barrier_num;
    } else {
      comp.insert(n);
    }
  }

  for (EdgeIt e(graph); e != INVALID; ++e) {
    if (mm.matching(e)) {
      check(e == mm.matching(graph.u(e)), "Wrong matching");
      check(e == mm.matching(graph.v(e)), "Wrong matching");
      ++num;
    }
    check(mm.status(graph.u(e)) != MaxMatching<SmartGraph>::EVEN ||
          mm.status(graph.v(e)) != MaxMatching<SmartGraph>::MATCHED,
          "Wrong Gallai-Edmonds decomposition");

    check(mm.status(graph.v(e)) != MaxMatching<SmartGraph>::EVEN ||
          mm.status(graph.u(e)) != MaxMatching<SmartGraph>::MATCHED,
          "Wrong Gallai-Edmonds decomposition");

    if (mm.status(graph.u(e)) != MaxMatching<SmartGraph>::ODD &&
        mm.status(graph.v(e)) != MaxMatching<SmartGraph>::ODD) {
      comp.join(graph.u(e), graph.v(e));
    }
  }

  std::set<int> comp_root;
  int odd_comp_num = 0;
  for (NodeIt n(graph); n != INVALID; ++n) {
    if (mm.status(n) != MaxMatching<SmartGraph>::ODD) {
      int root = comp.find(n);
      if (comp_root.find(root) == comp_root.end()) {
        comp_root.insert(root);
        if (comp.size(n) % 2 == 1) {
          ++odd_comp_num;
        }
      }
    }
  }

  check(mm.matchingSize() == num, "Wrong matching");
  check(2 * num == countNodes(graph) - (odd_comp_num - barrier_num),
         "Wrong matching");
  return;
}

void checkWeightedMatching(const SmartGraph& graph,
                   const SmartGraph::EdgeMap<int>& weight,
                   const MaxWeightedMatching<SmartGraph>& mwm) {
  for (SmartGraph::EdgeIt e(graph); e != INVALID; ++e) {
    if (graph.u(e) == graph.v(e)) continue;
    int rw = mwm.nodeValue(graph.u(e)) + mwm.nodeValue(graph.v(e));

    for (int i = 0; i < mwm.blossomNum(); ++i) {
      bool s = false, t = false;
      for (MaxWeightedMatching<SmartGraph>::BlossomIt n(mwm, i);
           n != INVALID; ++n) {
        if (graph.u(e) == n) s = true;
        if (graph.v(e) == n) t = true;
      }
      if (s == true && t == true) {
        rw += mwm.blossomValue(i);
      }
    }
    rw -= weight[e] * mwm.dualScale;

    check(rw >= 0, "Negative reduced weight");
    check(rw == 0 || !mwm.matching(e),
          "Non-zero reduced weight on matching edge");
  }

  int pv = 0;
  for (SmartGraph::NodeIt n(graph); n != INVALID; ++n) {
    if (mwm.matching(n) != INVALID) {
      check(mwm.nodeValue(n) >= 0, "Invalid node value");
      pv += weight[mwm.matching(n)];
      SmartGraph::Node o = graph.target(mwm.matching(n));
      check(mwm.mate(n) == o, "Invalid matching");
      check(mwm.matching(n) == graph.oppositeArc(mwm.matching(o)),
            "Invalid matching");
    } else {
      check(mwm.mate(n) == INVALID, "Invalid matching");
      check(mwm.nodeValue(n) == 0, "Invalid matching");
    }
  }

  int dv = 0;
  for (SmartGraph::NodeIt n(graph); n != INVALID; ++n) {
    dv += mwm.nodeValue(n);
  }

  for (int i = 0; i < mwm.blossomNum(); ++i) {
    check(mwm.blossomValue(i) >= 0, "Invalid blossom value");
    check(mwm.blossomSize(i) % 2 == 1, "Even blossom size");
    dv += mwm.blossomValue(i) * ((mwm.blossomSize(i) - 1) / 2);
  }

  check(pv * mwm.dualScale == dv * 2, "Wrong duality");

  return;
}

void checkWeightedPerfectMatching(const SmartGraph& graph,
                          const SmartGraph::EdgeMap<int>& weight,
                          const MaxWeightedPerfectMatching<SmartGraph>& mwpm) {
  for (SmartGraph::EdgeIt e(graph); e != INVALID; ++e) {
    if (graph.u(e) == graph.v(e)) continue;
    int rw = mwpm.nodeValue(graph.u(e)) + mwpm.nodeValue(graph.v(e));

    for (int i = 0; i < mwpm.blossomNum(); ++i) {
      bool s = false, t = false;
      for (MaxWeightedPerfectMatching<SmartGraph>::BlossomIt n(mwpm, i);
           n != INVALID; ++n) {
        if (graph.u(e) == n) s = true;
        if (graph.v(e) == n) t = true;
      }
      if (s == true && t == true) {
        rw += mwpm.blossomValue(i);
      }
    }
    rw -= weight[e] * mwpm.dualScale;

    check(rw >= 0, "Negative reduced weight");
    check(rw == 0 || !mwpm.matching(e),
          "Non-zero reduced weight on matching edge");
  }

  int pv = 0;
  for (SmartGraph::NodeIt n(graph); n != INVALID; ++n) {
    check(mwpm.matching(n) != INVALID, "Non perfect");
    pv += weight[mwpm.matching(n)];
    SmartGraph::Node o = graph.target(mwpm.matching(n));
    check(mwpm.mate(n) == o, "Invalid matching");
    check(mwpm.matching(n) == graph.oppositeArc(mwpm.matching(o)),
          "Invalid matching");
  }

  int dv = 0;
  for (SmartGraph::NodeIt n(graph); n != INVALID; ++n) {
    dv += mwpm.nodeValue(n);
  }

  for (int i = 0; i < mwpm.blossomNum(); ++i) {
    check(mwpm.blossomValue(i) >= 0, "Invalid blossom value");
    check(mwpm.blossomSize(i) % 2 == 1, "Even blossom size");
    dv += mwpm.blossomValue(i) * ((mwpm.blossomSize(i) - 1) / 2);
  }

  check(pv * mwpm.dualScale == dv * 2, "Wrong duality");

  return;
}


int main() {

  for (int i = 0; i < lgfn; ++i) {
    SmartGraph graph;
    SmartGraph::EdgeMap<int> weight(graph);

    istringstream lgfs(lgf[i]);
    graphReader(graph, lgfs).
      edgeMap("weight", weight).run();

    bool perfect;
    {
      MaxMatching<SmartGraph> mm(graph);
      mm.run();
      checkMatching(graph, mm);
      perfect = 2 * mm.matchingSize() == countNodes(graph);
    }

    {
      MaxWeightedMatching<SmartGraph> mwm(graph, weight);
      mwm.run();
      checkWeightedMatching(graph, weight, mwm);
    }

    {
      MaxWeightedMatching<SmartGraph> mwm(graph, weight);
      mwm.init();
      mwm.start();
      checkWeightedMatching(graph, weight, mwm);
    }

    {
      MaxWeightedPerfectMatching<SmartGraph> mwpm(graph, weight);
      bool result = mwpm.run();

      check(result == perfect, "Perfect matching found");
      if (perfect) {
        checkWeightedPerfectMatching(graph, weight, mwpm);
      }
    }

    {
      MaxWeightedPerfectMatching<SmartGraph> mwpm(graph, weight);
      mwpm.init();
      bool result = mwpm.start();

      check(result == perfect, "Perfect matching found");
      if (perfect) {
        checkWeightedPerfectMatching(graph, weight, mwpm);
      }
    }
  }

  return 0;
}
*/