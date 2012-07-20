/**------------------------------------------------------------------------
 * All Rights Reserved
 * Author: Diego Macrini, Chris Whiten
 *-----------------------------------------------------------------------*/
#include <boost/graph/graphviz.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/dynamic_bitset.hpp>

#include <RootHeader.h>
#include "ObjectRecognizer.h"
#include <MachineLearning/ObjectLearner.h>
#include <ShapeParsing/ShapeParser.h>
#include <ShapeMatching/ShapeMatcher.h>
#include <Tools/UserArguments.h>
#include <Tools/NamedColor.h>
#include <VideoParserGUI/DrawingUtils.h>

using namespace vpl;
using namespace std;

extern UserArguments g_userArgs;

bool TEST_DRAWING_CORNERS = false; // visualization tests for convex corners.  Will eventually do experiments to determine how robust these corners are across video sequences.

struct Vertex
{
	string name;
	int model_id;
	int parse_id;
	int part_id;
};

struct Edge
{
	double score;
};

typedef boost::adjacency_list<boost::listS, boost::vecS, boost::directedS, Vertex, Edge> boost_graph;
typedef boost_graph::vertex_descriptor VertexID;
typedef boost_graph::edge_descriptor EdgeID;
typedef pair<int, int> Pair;


/*!
	Reads the parameters provided by the user. 

	It is called by VisSysComponent::Initialize(), which
	is in turn calledwhen the vis component is created.
	
	It is NOT an static function because the user is allowed
	to changed the parameters of a vision system component
	several times during the execution of a program. 
*/
void ObjectRecognizer::ReadParamsFromUserArguments()
{
	VisSysComponent::ReadParamsFromUserArguments();

	//g_userArgs.ReadArg("ObjectRecognizer", "modelDBPath", 
	//	"Path to the model object database", std::string(), &m_params.modelDBPath);

	g_userArgs.ReadArg("ObjectRecognizer", "matchingAlgoritm", 
		"Algorithm used to match graph-based representations of objects", 
		0, &m_params.matchingAlgorithm);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "excludeQueryObjectFromDB", 
		"Check if query object is in model database and removes all its views?", 
		true, &m_params.excludeQueryObjectFromDB);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "excludeQueryViewFromDB", 
		"Check if query object *view* is in model database and removes it?", 
		true, &m_params.excludeQueryViewFromDB);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "onlySumModelNodeMatches", 
		"Sum only the matching distance of model nodes?", 
		false, &m_params.onlySumModelNodeMatches);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "test_against_shape_contexts", 
		"Evaluate this approach against standard shape contexts", 
		false, &m_params.test_against_shape_contexts);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "use_importance_weights", 
		"Use weights that measure the importance of a shape part", 
		false, &m_params.use_importance_weights);
	
	g_userArgs.ReadBoolArg("ObjectRecognizer", "learn_importance_weights",
		"Learn weights that measure the importance of a shape part",
		false, &m_params.learn_importance_weights);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "learn_parsing_model", 
		"Learn which model should be used to parse each model class",
		false, &m_params.learn_parsing_model);

	g_userArgs.ReadBoolArg("ObjectRecognizer", "use_learned_parsing_model", 
		"Use a learned parsing model for each class",
		false, &m_params.use_learned_parsing_model);
}

void ObjectRecognizer::Initialize(vpl::graph::node v)
{
	VisSysComponent::Initialize(v);

	m_pObjectLearner = FindParentComponent(ObjectLearner);
	m_pShapeParser = FindParentComponent(ShapeParser);
	m_pShapeMatcher = FindParentComponent(ShapeMatcher);

	std::list<UserCommandInfo> cmds0;

	ShapeParseGraph::GetSwitchCommands(cmds0);

	m_params.overlayWarpQuery = false;

	cmds0.push_back(UserCommandInfo("warp", "overlay warped query", 
		'w', &m_params.overlayWarpQuery));

	RegisterUserSwitchCommands(0, cmds0);	
}

std::pair<VertexID, bool> getNodeByString2(boost_graph &g, string s)
{
	boost::graph_traits<boost_graph>::vertex_iterator vi, vi_end, next;
	boost::tie(vi, vi_end) = boost::vertices(g);
	for (next = vi; vi != vi_end; vi = next)
	{
		++next;
		if (g[*vi].name == s)
		{
			std::pair<VertexID, bool> ret(*vi, true);
			return ret;
		}
	}

	// not found
	std::pair<VertexID, bool> ret(VertexID(), false);
	return ret;
}

void printGraph(boost_graph &g, std::vector<EdgeID> &edges, std::string filename)
{
	std::ofstream out;
	out.open(filename);

	out << "digraph clique_graph {" << std::endl;
	out << "rankdir=LR;" << std::endl << "node [shape = circle];" << std::endl;

	for (unsigned i = 0; i < edges.size(); i++)
	{
		stringstream ss;
		ss << g[edges[i]].score;

		out <<  g[edges[i].m_source].name << " -> " <<  g[edges[i].m_target].name << " [ label = \"" << ss.str() << "\" ];" << std::endl;
	}
	out << "}" << std::endl;
	out.close();
}

bool isClique(const boost_graph &g, const std::vector<VertexID> &nodes, const std::vector<EdgeID> &edges, double &score)
{
	for (unsigned i = 0; i < nodes.size(); i++)
	{
		for (unsigned j = 0; j < nodes.size(); j++)
		{
			if (i == j) { continue; }
			if (!boost::edge(nodes[i], nodes[j], g).second) { return false;}
			else
			{
				score += g[boost::edge(nodes[i], nodes[j], g).first].score;
			}
		}
	}
	return true;
}

void increment(boost::dynamic_bitset<> &bitset)
{	
	for (unsigned loop = 0; loop < bitset.size(); ++loop)
	{
		if ((bitset[loop] ^= 0x1) == 0x1)
		{
			break;
		}
	}
}

void randomize(boost::dynamic_bitset<> &bitset)
{
	for (unsigned loop = 0; loop < bitset.size(); ++loop)
	{
		double rand_num = (double)rand()/RAND_MAX;

		if (rand_num < 0.1)
		{
			bitset[loop] = true;
		}
		else
		{
			bitset[loop] = false;
		}
	}
}

void decrement(boost::dynamic_bitset<> &bitset)
{
	for (unsigned loop = 0; loop < bitset.size(); ++loop)
	{
		if ((bitset[loop] ^= 0x1) == 0x0)
		{
			break;
		}
	}
}

unsigned int cliqueHelper3(const boost_graph &g, const std::vector<VertexID> &nodes, const std::vector<EdgeID> &edges, double &score)
{
	static boost::dynamic_bitset<> empty;
	empty.resize(nodes.size());

	unsigned largest_clique = 1;
	boost::dynamic_bitset<> set(nodes.size());
	
	for (unsigned i = 0; i < nodes.size(); ++i)
	{
		set[i] = true;
	}

	int i = 0;
	while ((largest_clique <= 1 || i < 50000) && i < 100000)
	{
		// build the next potential clique.
		// every bit that is a 1 in set means that that node is in the clique.
		std::vector<VertexID> clique;
		randomize(set);
		set[0] = true; // the first bit is the query node.  must always be on.

		for (unsigned loop = 0; loop < nodes.size(); ++loop)
		{
			if (set[loop])
			{
				clique.push_back(nodes[loop]);
			}
		}

		// if this is a clique..
		double maybe_score = 0;
		if ((clique.size() > largest_clique) && isClique(g, clique, edges, maybe_score))
		{
			largest_clique = clique.size();
			score = maybe_score;
			std::cout << "Largest clique is " << largest_clique << std::endl;
		}
		//decrement(set);
		//increment(set);
		i++;
	}

	return largest_clique;
}

void ObjectRecognizer::findMaxClique(int query_model_id, int query_parse_id, int query_shape_part, 
						std::vector<int> model_id, std::vector<int> model_parse_id, std::vector<int> model_part_id, 
						std::vector<double> matching_distance, int &clique_size, double &score)
{
	boost_graph g;
	SPGMatch gmatch;
	std::vector<VertexID> nodes;
	std::vector<EdgeID> edges;
	srand((unsigned) time(NULL));

	// set up query graph
	///////////////////////////////////////////
	const ModelHierarchy& modelHierarchy = m_pObjectLearner->GetModelHierarchy();
	const ShapeParseGraph &G_q = modelHierarchy.GetShapeParse(query_model_id, query_parse_id);
	int model_view = 0;
	int model_parse = 0;

	std::stringstream ss;
	ss << "model_" << query_model_id << "_parse_" << query_parse_id << "_part_" << query_shape_part;


	VertexID query_vid = boost::add_vertex(g);
	nodes.push_back(query_vid);
	g[query_vid].name = ss.str();
	g[query_vid].model_id = query_model_id;
	g[query_vid].parse_id = query_parse_id;
	g[query_vid].part_id = query_shape_part;

	
	ss.str("");

	// add all the models that the query matched.
	///////////////////////////////////////////
	for (unsigned i = 0; i < model_id.size(); ++i)
	{
		ss << "model_" << model_id[i] << "_parse_" << model_parse_id[i] << "_part_" << model_part_id[i];

		VertexID vid = boost::add_vertex(g);
		g[vid].name = ss.str();
		g[vid].model_id = model_id[i];
		g[vid].parse_id = model_parse_id[i];
		g[vid].part_id = model_part_id[i];
		nodes.push_back(vid);

		EdgeID eid;
		bool ok;
		boost::tie(eid, ok) = boost::add_edge(nodes[0], nodes[nodes.size() - 1], g);
		if (ok)
		{
			edges.push_back(eid);
			g[eid].score = matching_distance[i];
		}

		ss.str("");
	}

	//printGraph(g, edges, "only_query.dot");

	// build rest of graph
	///////////////////////////////////////////
	for (unsigned i = 0; i < model_id.size(); ++i)
	{
		// set query to model 'i'
		const ShapeParseGraph &query_spg = modelHierarchy.GetShapeParse(model_id[i], model_parse_id[i]);
		gmatch.queryParseIdx = model_parse_id[i];

		// loop over other models
		for (unsigned j = 0; j < model_id.size(); j++)
		{
			if (i == j) { continue; }

			//set model spg
			const ShapeParseGraph &model_spg = modelHierarchy.GetShapeParse(model_id[j], model_parse_id[j]);
			gmatch.modelViewIdx = model_id[j];
			gmatch.modelParseIdx = model_parse_id[j];


			// match
			double match_score = m_pShapeMatcher->Match(query_spg, model_spg);
			m_pShapeMatcher->GetF2SNodeMap(gmatch.nodeMap);

			// if this new query, at its given part id, matches a part already in the graph, add the edge. otherwise, ignore.
			NodeMatchMap mapping = gmatch.nodeMap;
			graph::node u, v;

			forall_nodes(u, query_spg)
			{
				if (mapping[u].srcNodeIdx == model_part_id[i])
				{
					ss << "model_" << model_id[j] << "_parse_" << model_parse_id[j] << "_part_" << mapping[u].tgtNodeIdx;

					std::pair<VertexID, bool> node_in_graph = getNodeByString2(g, ss.str());
					if (node_in_graph.second)
					{
						std::stringstream ss2;
						ss2 << "model_" << model_id[i] << "_parse_" << model_parse_id[i] << "_part_" << model_part_id[i];
						std::pair<VertexID, bool> node_in_graph2 = getNodeByString2(g, ss2.str());
						ss2.str("");


						EdgeID eid;
						bool ok;
						boost::tie(eid, ok) = boost::add_edge(node_in_graph2.first, node_in_graph.first, g);
						if (ok)
						{
							edges.push_back(eid);
							g[eid].score = mapping[u].nodeAttDist;
						}
					}
					else
					{
					//	std::cout << "didn't find the node." << std::endl;
					}
					ss.str("");
				}
			}
		}

		// now test this model 'i' against the original query (since it is not in the vector model_id)
		const ShapeParseGraph &model_spg = modelHierarchy.GetShapeParse(query_model_id, query_parse_id);
		gmatch.modelViewIdx = query_model_id;
		gmatch.modelParseIdx = query_parse_id;

		double match_score = m_pShapeMatcher->Match(query_spg, model_spg);
		m_pShapeMatcher->GetF2SNodeMap(gmatch.nodeMap);

		NodeMatchMap mapping = gmatch.nodeMap;
		graph::node u, v;

		forall_nodes(u, query_spg)
		{
			if (mapping[u].srcNodeIdx == model_part_id[i])
			{
				ss << "model_" << query_model_id << "_parse_" << query_parse_id << "_part_" << mapping[u].tgtNodeIdx; 
				std::pair<VertexID, bool> node_in_graph = getNodeByString2(g, ss.str());
				if (node_in_graph.second)
				{
					std::stringstream ss2;
					ss2 << "model_" << model_id[i] << "_parse_" << model_parse_id[i] << "_part_" << model_part_id[i];
					
					std::pair<VertexID, bool> node_in_graph2 = getNodeByString2(g, ss2.str());
					ss2.str("");

					EdgeID eid;
					bool ok;
					boost::tie(eid, ok) = boost::add_edge(node_in_graph2.first, node_in_graph.first, g);
					if (ok)
					{
						edges.push_back(eid);
						g[eid].score = mapping[u].nodeAttDist; 
					}
				}
				ss.str("");
			}
		}
		std::stringstream ssfile;
		ssfile << "model_" << i << ".dot";
		//printGraph(g, edges, ssfile.str());
		//std::cout << std::endl;
	}

	// print to graphviz
	////////////////////////////////////
	
	//printGraph(g, edges, "final.dot");

	// calculate max-clique based on this graph
	// must contain original query
	///////////////////////////////////////////
	std::cout << "starting clique stuff" << std::endl;
	std::cout << "Total models: " << model_id.size() << std::endl;
	std::cout << "Number of nodes: " << nodes.size() << std::endl;

	clique_size = cliqueHelper3(g, nodes, edges, score);
	std::cout << "The clique size was " << clique_size  << std::endl;
	//score = clique_size;
}

SPGPtr ObjectRecognizer::CreateSinglePartSPG(const ShapeInfoPtr &sip)
{
	// get shapeinfo by id...
	
	SPGPtr ptrSPG(new ShapeParseGraph(sip));
	graph::node part_node = ptrSPG->new_node(ShapePart());
	IntList &part_boundary = ptrSPG->attribute(part_node).boundarySegments;

	part_boundary.push_back(0);
	part_boundary.push_back(sip->BoundarySize() - 1);

	ptrSPG->FinalizeConstruction();
	return ptrSPG;
}

int ObjectRecognizer::getQueryId(std::string class_name, int obj_id, const ModelHierarchy &model_hierarchy)
{
	SPGMatch gmatch;
	for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < model_hierarchy.ModelViewCount(); ++gmatch.modelViewIdx)
	{
		const ModelHierarchy::ModelView &mv = model_hierarchy.GetModelView(gmatch.modelViewIdx);
		if (model_hierarchy.HasParent(mv, class_name, obj_id))
		{
			return gmatch.modelViewIdx;
		}
	}
	return -1;
}

void ObjectRecognizer::getAllClassesInDatabase(std::vector<std::string> &classes, const ModelHierarchy &modelHierarchy)
{
	SPGMatch gmatch;

	// For all model shapes in the model database		
	for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < modelHierarchy.ModelViewCount(); ++gmatch.modelViewIdx)
	{
		const ModelHierarchy::ModelView &model = modelHierarchy.GetModelView(gmatch.modelViewIdx);
		std::string class_name = modelHierarchy.getModelViewClass(model);

		// if we haven't already seen this class, add it to our object class vector
		if (std::find(classes.begin(), classes.end(), class_name) == classes.end())
		{
			classes.push_back(class_name);
		}
	}
}

void ObjectRecognizer::getAllModelIndicesOfGivenClass(std::vector<unsigned int> &models, std::string target_class, const ModelHierarchy &model_hierarchy)
{
	SPGMatch gmatch;

	for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < model_hierarchy.ModelViewCount(); ++gmatch.modelViewIdx)
	{
		const ModelHierarchy::ModelView &model = model_hierarchy.GetModelView(gmatch.modelViewIdx);
		if (model_hierarchy.getModelViewClass(model) == target_class)
		{
			models.push_back(gmatch.modelViewIdx);
		}
	}
}

void ObjectRecognizer::getClassToIndexMapping(std::map<std::string, unsigned int> &class_to_index, std::vector<std::string> classes)
{
	for (unsigned i = 0; i < classes.size(); ++i)
	{
		class_to_index[classes[i]] = i;
	}
}

void ObjectRecognizer::getModelToClassMapping(std::map<unsigned int, std::string> &model_to_class, const ModelHierarchy &model_hierarchy)
{
	SPGMatch gmatch;

	for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < model_hierarchy.ModelViewCount(); ++gmatch.modelViewIdx)
	{
		const ModelHierarchy::ModelView &model = model_hierarchy.GetModelView(gmatch.modelViewIdx);
		model_to_class[gmatch.modelViewIdx] = model_hierarchy.getModelViewClass(model);
	}
}

void generateAllPermutations(std::map<std::string, unsigned int> &unfinished_mapping, unsigned int index, 
	std::vector<unsigned int> &parameterizations, std::vector<std::string> &classes,
	std::vector<std::map<std::string, unsigned int> > &joint_parameterizations)
{
	for (auto param = parameterizations.begin(); param != parameterizations.end(); ++param)
	{
		std::map<std::string, unsigned int> new_map = unfinished_mapping;
		new_map[classes[index]] = *param;
		if (index == classes.size() - 1)
		{
			joint_parameterizations.push_back(new_map);
		}
		else
		{
			generateAllPermutations(new_map, index + 1, parameterizations, classes, joint_parameterizations);
		}
	}
}

/*unsigned int getParameterization(lbfgsfloatval_t val)
{
	if (val < 0.25)
	{
		return 1;
	}
	else if (val < 0.5)
	{
		return 2;
	}
	else if (val < 0.75)
	{
		return 3;
	}
	else
	{
		return 4;
	}
}*/

double ObjectRecognizer::evaluate(GAPhenotype &pheno)
{
	const ModelHierarchy &mh = m_pObjectLearner->GetModelHierarchy();

	// evaluate this parameterization.  fx is its score.
	unsigned int correct_matches = 0; 

	for (unsigned query = 0; query < mh.ModelViewCount(); ++query)
	{
		if (query % 20 == 0)
		{
			cout << "query " << query << "...";
		}

		unsigned int query_class_index = class_to_index[model_to_class[query]];
		double best_score = numeric_limits<double>::max();
		unsigned int best_model_id = 0;

		for (unsigned model = 0; model < mh.ModelViewCount(); ++model)
		{
			if (query == model) continue;
			unsigned int model_class_index = class_to_index[model_to_class[model]];

			// re-parse both by this parameterization.
			ShapeInfoPtr query_contour = mh.GetModelView(query).ptrShapeContour;
			std::list<ShapeParsingModel> shape_list = m_pShapeParser->getReparsedShapeList(query_contour, pheno.chromosome[model_class_index]);
			std::vector<SPGPtr> query_spgs = element_at(shape_list, 0).GetShapeParses();

			ShapeInfoPtr model_contour = mh.GetModelView(model).ptrShapeContour;
			shape_list = m_pShapeParser->getReparsedShapeList(model_contour, pheno.chromosome[model_class_index]);
			std::vector<SPGPtr> model_spgs = element_at(shape_list, 0).GetShapeParses();

			// match all parses of the query against all parses of the model.
			for (auto query_parse = query_spgs.begin(); query_parse != query_spgs.end(); ++query_parse)
			{
				for (auto model_parse = model_spgs.begin(); model_parse != model_spgs.end(); ++model_parse)
				{
					double score = m_pShapeMatcher->Match(*(*query_parse), *(*model_parse));
					if (score < best_score)
					{
						best_score = score;
						best_model_id = model;
					}
				}
			}
		} // end model loop.

		if (model_to_class[best_model_id] == model_to_class[query])
			correct_matches++;
	}

	return correct_matches;
}

void ObjectRecognizer::learnJointParsingModel()
{
	if (!m_params.learn_parsing_model)
		return;

	ofstream file;
	file.open("parsing_model.txt");
	srand(time(NULL));

	SPGMatch gmatch;
	const ModelHierarchy &model_hierarchy = m_pObjectLearner->GetModelHierarchy();

	////////////////////////

	// temporary.  Once we have our parameterizations set up a bit better,
	// we will have a better way to address this.
	unsigned int population_size = 10;
	double crossover_rate = 0.8;
	unsigned int iterations = 3;
	unsigned int number_of_classes = all_classes.size();
	std::cout << "number of classes: " << number_of_classes << endl;
	std::vector<unsigned int> parameterizations;
	for (unsigned i = 0; i < 4; ++i)
		parameterizations.push_back(i);
	
	// initialize population.
	std::vector<GAPhenotype> population;

	// first phenotypes are one parameterization for all classes.
	for (unsigned int i = 0; i < parameterizations.size(); ++i)
	{
		vector<unsigned int> chromosome;
		for (unsigned j = 0; j < number_of_classes; ++j)
		{
			chromosome.push_back(parameterizations[i]);
		}
		population.push_back(GAPhenotype(parameterizations, number_of_classes, chromosome));
	}

	// now random ones for the remainder.  the assumption here is that there are more
	// members in the population than parameterizations.
	for (unsigned i = 0; i < population_size - parameterizations.size(); ++i)
	{
		population.push_back(GAPhenotype(parameterizations, number_of_classes));
	}

	for (unsigned int iteration = 0; iteration < iterations; ++iteration)
	{
		cout << "iteration " << iteration + 1 << endl;
		// elitism (save max sltn at every step)
		GAPhenotype most_fit = population[population_size - 1];

		// generate new candidates.
		random_shuffle(population.begin(), population.end());
		for (unsigned i = 0; i < population_size; i += 2)
		{
			double potential_crossover = rand() / (RAND_MAX + 1);
			if (potential_crossover < crossover_rate)
			{
				pair<GAPhenotype, GAPhenotype> children = population[i].crossover(population[i + 1]);
				population.push_back(children.first);
				population.push_back(children.second);
			}
			population[i].mutate();
			population[i + 1].mutate();
		}

		cout << "eval" << endl;
		// evaluate all of our candidates.
		for (unsigned i = 0; i < population.size(); ++i)
		{
			population[i].fitness = evaluate(population[i]);
			cout << endl;
			for (unsigned g = 0; g < population[i].chromosome.size(); ++g)
			{
				cout << population[i].chromosome[g] << ", ";
			}
			cout << population[i].fitness << endl;
		}

		// tournament selection to trim the fat.
		while (population.size() > population_size)
		{
			random_shuffle(population.begin(), population.end());
			double selector = rand() / (RAND_MAX + 1);

			if (population[0].fitness > population[1].fitness)
			{
				if (selector < 0.95)
				{
					population.erase(population.begin() + 1);
				}
				else
				{
					population.erase(population.begin());
				}
			}
			else
			{
				if (selector < 0.95)
				{
					population.erase(population.begin());
				}
				else
				{
					population.erase(population.begin() + 1);
				}
			}
		}

		// sort by fitness at the end.
		sort(population.begin(), population.end());

		// elitism removes the least fit with the most fit from previous generation.
		population[0] = most_fit;
	}

	sort(population.begin(), population.end());
	cout << "best phenotype" << endl << "--------------------" << endl;
	for (unsigned g = 0; g < population[population_size - 1].chromosome.size(); ++g)
	{
		cout << population[population_size - 1].chromosome[g] << ", ";
	}
	cout << population[population_size - 1].fitness << endl;

	for (unsigned int i = 0; i < population[population_size - 1].chromosome.size(); ++i)
	{
		file << all_classes[i] << ", " << population[population_size - 1].chromosome[i] << "\n";
	}
	file.close();
	cout << "Parameterization written.  Exiting." << endl;
	exit(0);
	//////////////////////////
}


// Currently, this is learning the individual class-wise MAP parsing parameterization.
// Would prefer holistic joint MAP assignment.
void ObjectRecognizer::learnParsingModel()
{
	learnJointParsingModel();
	return;
	//////
	if (!m_params.learn_parsing_model)
		return;

	ofstream file;
	file.open("parsing_model.txt");

	SPGMatch gmatch;
	const ModelHierarchy &model_hierarchy = m_pObjectLearner->GetModelHierarchy();

	std::vector<std::string> classes;
	getAllClassesInDatabase(classes, model_hierarchy);

	std::map<unsigned int, std::string> model_to_class;
	getModelToClassMapping(model_to_class, model_hierarchy);

	std::vector<unsigned int> parameterizations;
	for (unsigned i = 0; i < 4; ++i)
		parameterizations.push_back(i);

	// for each class...
	for (auto c = classes.begin(); c != classes.end(); ++c)
	{
		std::vector<unsigned int> models_of_this_class;
		getAllModelIndicesOfGivenClass(models_of_this_class, *c, model_hierarchy);

		// we match against all parameterizations to determine the best one.
		unsigned int best_parameterization = 0;
		unsigned int best_parameterization_votes = 0;

		for (auto p = parameterizations.begin(); p != parameterizations.end(); ++p)
		{
			// this value counts the number of good matches for this class under this parameterization.
			unsigned int parameterization_votes = 0;

			// So, each model from this class can potentially contribute +1 to this parameterization.
			for (auto query = models_of_this_class.begin(); query != models_of_this_class.end(); ++query)
			{
				// re-parse the query
				ShapeInfoPtr query_contour = model_hierarchy.GetModelView(*query).ptrShapeContour;
				std::list<ShapeParsingModel> shape_list = m_pShapeParser->getReparsedShapeList(query_contour, *p);
				std::vector<SPGPtr> query_spgs = element_at(shape_list, 0).GetShapeParses(); 

				// values to store current MAP assignment.
				double best_score = numeric_limits<double>::max();
				unsigned int best_model_id = 0;
				
				// this loops over all models in the database.
				for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < model_hierarchy.ModelViewCount(); ++gmatch.modelViewIdx)
				{
					unsigned int model = gmatch.modelViewIdx;
					if (model == *query)
						continue;

					// re-parse the model.
					ShapeInfoPtr model_contour = model_hierarchy.GetModelView(model).ptrShapeContour;
					shape_list = m_pShapeParser->getReparsedShapeList(model_contour, *p);//model);
					std::vector<SPGPtr> model_spgs = element_at(shape_list, 0).GetShapeParses();

					// match all parses of the query against all parses of the model.
					for (auto query_parse = query_spgs.begin(); query_parse != query_spgs.end(); ++query_parse)
					{
						for (auto model_parse = model_spgs.begin(); model_parse != model_spgs.end(); ++model_parse)
						{
							double score = m_pShapeMatcher->Match(*(*query_parse), *(*model_parse));
							if (score < best_score)
							{
								best_score = score;
								best_model_id = gmatch.modelViewIdx;
							}
						}
					}
				}

				if (model_to_class[best_model_id] == model_to_class[*query])
					parameterization_votes++;
			}

			if (parameterization_votes > best_parameterization_votes)
			{
				std::cout << "overwriting " << best_parameterization_votes << " with " << parameterization_votes << std::endl;
				best_parameterization_votes = parameterization_votes;
				best_parameterization = *p;
			}
			else
			{
				std::cout << "not better.  Only got this many votes: " << parameterization_votes << " while the best is " << best_parameterization_votes << std::endl;
			}
		}

		// the best parameterization has been determined.  Write it to file.
		std::cout << "writing to file: " << *c << ", " << best_parameterization << " out of " << models_of_this_class.size() << std::endl;
		file << *c << "," << best_parameterization << "\n";
	}

	file.close();
	std::cout << "Parsing model learning completed." << std::endl;
	exit(1);
}

void ObjectRecognizer::learnWeights()
{
	if (m_params.learn_importance_weights)
	{
		SPGMatch gmatch;
		m_rankings.resize(m_pShapeParser->NumShapes());
		const ModelHierarchy &modelHierarchy = m_pObjectLearner->GetModelHierarchy();

		const unsigned numModelViews = modelHierarchy.ModelViewCount();

		QueryRanking &qr = m_rankings[0]; // assuming only one shape in each frame...

		// get all of the parses of the query shape.
		qr.queryParses = m_pShapeParser->GetShapeParses(0);
		std::cout << "number of parses = " << qr.queryParses.size() << std::endl;

		qr.matches.resize(numModelViews); // one match for all of the +1000 model views.
		std::cout << "number of model views = " << qr.matches.size() << std::endl;

		const unsigned numQueryParses = qr.queryParses.size();
		std::cout << "numqueryparses = " << numQueryParses << std::endl;


		// load the query info.
		std::vector<std::string> classes;
		getAllClassesInDatabase(classes, modelHierarchy);

		// open file for writing
		ofstream file;
		//file.open("weights.txt");
		for (unsigned class_num = 0; class_num < classes.size(); ++class_num) // on second machine, put this from classes.size()/4 to classes.size()/2 and then do from /2 to *3/4 and to classes.size()
		{
			std::vector<unsigned int> models_of_this_class;
			getAllModelIndicesOfGivenClass(models_of_this_class, classes[class_num], modelHierarchy);		

			unsigned model_count = models_of_this_class.size();
			// loop over all models in this class. 'i' is the query model.
			for (unsigned i = 0; i < model_count; i++)
			{
				// go over each parse of the query.
				unsigned i_parse_count = modelHierarchy.ShapeParseCount(models_of_this_class[i]);
				for (unsigned shape_parse = 0; shape_parse < i_parse_count; shape_parse++)
				{
					std::vector<int> model_id;
					std::vector<int> model_parse_id;
					std::vector<int> model_part_id;
					std::vector<double> matching_distance;

					const ShapeParseGraph &G_q = modelHierarchy.GetShapeParse(models_of_this_class[i], shape_parse);
					unsigned part_count = G_q.number_of_nodes();

					// for each part in the parse
					for (unsigned part_number = 0; part_number < part_count; part_number++)
					{
						// match that part against all parses of all other intra-class models
						// j is the model we are checking the query against.
						for (unsigned j = 0; j < model_count; j++)
						{
							// if i and j are the same model, skip.
							if (i == j) {continue;}

							// go over each shape parse of model 'j'
							unsigned j_parse_count = modelHierarchy.ShapeParseCount(models_of_this_class[j]);
							//if (j_parse_count > 2) {j_parse_count = 2;}
							for (unsigned j_shape_parse = 0; j_shape_parse < j_parse_count; j_shape_parse++)
							{
								gmatch.queryParseIdx = shape_parse;
								gmatch.modelViewIdx = models_of_this_class[j]; // should this be j, or models_of_this_class[j]?
								gmatch.modelParseIdx = j_shape_parse;

								// match query 'i' with shape parse 'shape_parse' to query 'j' with shape parse 'j_shape_parse'
								const ShapeParseGraph &G_m = modelHierarchy.GetShapeParse(models_of_this_class[j], j_shape_parse); 
								double score = m_pShapeMatcher->Match(G_q, G_m);
								m_pShapeMatcher->GetF2SNodeMap(gmatch.nodeMap);

								gmatch.value = score;
								NodeMatchMap mapping = gmatch.nodeMap;
								graph::node u,v;

								// loop over all nodes u in the query shape parse...
								forall_nodes(u, G_q)
								{
									if (mapping[u].srcNodeIdx == part_number)
									{
										model_id.push_back(models_of_this_class[j]);
										model_parse_id.push_back(j_shape_parse);
										model_part_id.push_back(mapping[u].tgtNodeIdx);
										matching_distance.push_back(mapping[u].nodeAttDist);
									}
								}
							}
					
						}
						int clique_size = 0;
						double clique_score = 0;
						findMaxClique(models_of_this_class[i], shape_parse, part_number, model_id, model_parse_id, model_part_id, matching_distance, clique_size, clique_score);
						std::cout << "Clique size: " << clique_size << std::endl;
						std::cout << "Score: " << clique_score << " for part " << models_of_this_class[i] << "_" << shape_parse << "_" << part_number << std::endl;
						file << models_of_this_class[i] << "_" << shape_parse << "_" << part_number << " - " << clique_size << " - " << clique_score << "\n";
					}
				}
			}
		}
		file.close();
		std::cout << "-------------------------" << std::endl;
		std::cout << "Weight learning completed." << std::endl;
		exit(1);
	}
}

void ObjectRecognizer::testShapeContext(SPGMatch &gmatch, const ModelHierarchy &modelHierarchy)
{
	if (m_params.test_against_shape_contexts)
	{
		// a matching tuple has structure <query, model, score>
		std::vector<boost::tuple<int, int, double> > matchings;
		boost::tuple<int, int, double> matching;
	
		for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < modelHierarchy.ModelViewCount(); ++gmatch.modelViewIdx)
		{
			if (gmatch.modelViewIdx != (unsigned int)m_pShapeParser->GetFrameNumber())
			{
				SPGPtr &model_shape_context_spg = CreateSinglePartSPG(modelHierarchy.GetModelView(gmatch.modelViewIdx).ptrShapeContour);
				SPGPtr &query_shape_context_spg = CreateSinglePartSPG(modelHierarchy.GetModelView((unsigned int)m_pShapeParser->GetFrameNumber()).ptrShapeContour);

				double score = m_pShapeMatcher->Match(*query_shape_context_spg, *model_shape_context_spg);
				
				//////////////////

				graph::node u = model_shape_context_spg->first_node();
				while (true)
				{
					if (u == NULL)
						break;

					// gather the size of the boundary in this shape part vs in the whole shape.
					// will be useful for evaluating the relative size of this part, 
					// and weighting accordingly.
					/*std::cout << "shape part size: " << model_shape_context_spg->inf(u).boundarySegments.size() << std::endl;
					std::cout << "num points: " << model_shape_context_spg->inf(u).ptrDescriptor->GetBoundaryLength() << std::endl;;
					std::cout << "total shape boundary size: " << model_shape_context_spg->getNumberOfBoundaryPoints() << std::endl;*/

					u = model_shape_context_spg->succ_node(u);
				}

				/////////////
				boost::tuple<int, int, double> match((int)m_pShapeParser->GetFrameNumber(), gmatch.modelViewIdx, score);
				matchings.push_back(match);
			}
		}

		// now get the best match.
		unsigned min_index = 0;
		double current_min_score = matchings[0].get<2>();
		for (unsigned i = 0; i < matchings.size(); ++i)
		{
			double current_index_score = matchings[i].get<2>();
			if (current_index_score < current_min_score)
			{
				current_min_score = current_index_score;
				min_index = i;
			}
		}

		std::cout << "Shape contexts says that " << matchings[min_index].get<0>() << " matches with " << matchings[min_index].get<1>() << " with a score of " << current_min_score << std::endl;
		std::string query_class = (modelHierarchy.getModelViewClass(modelHierarchy.GetModelView(matchings[min_index].get<0>())));
		std::string model_class = (modelHierarchy.getModelViewClass(modelHierarchy.GetModelView(matchings[min_index].get<1>())));
	
		bool is_match = (query_class == model_class);
		boost::tuple<int, int, double, int> MAP_match(matchings[min_index].get<0>(), matchings[min_index].get<1>(), current_min_score, (int)is_match);
		
		std::cout << "Is a match: " << is_match << std::endl;
		std::cout << query_class << std::endl;
		std::cout << model_class << std::endl;

		shape_context_MAP_match = MAP_match;
		shape_context_MAP_matches.push_back(MAP_match);
	}
}

void ObjectRecognizer::loadWeights(Lookup_Table &lt)
{
	if (lt.size() <= 0 && m_params.use_importance_weights)
	{
		std::ifstream stream;
		stream.open("weights.txt");
	
		int model, parse, part;
		char ignore_char;
		int value;
		while (stream >> model >> ignore_char >> parse >> ignore_char >> part >> ignore_char >> value)
		{
			lt[WeightKey(model, parse, part)] = value;
		}
		std::cout << "weights loaded." << std::endl;
	}
}

void ObjectRecognizer::loadParsingModels(std::map<std::string, int> &parsing_models)
{
	if (parsing_models.size() <= 0 && m_params.use_learned_parsing_model)
	{
		std::ifstream stream;
		stream.open("parsing_model.txt");

		char ignore_char;
		std::string model_class;
		int parsing_model;
		while (stream >> model_class >> ignore_char >> parsing_model)
		{
			parsing_models[model_class] = parsing_model;
		}
	}
}

void ObjectRecognizer::Run()
{
	if (!m_pObjectLearner)
	{
		ShowMissingDependencyError(ObjectLearner);
		return;
	}

	if (!m_pShapeParser)
	{
		ShowMissingDependencyError(ShapeParser);
		return;
	}

	if (!m_pShapeMatcher)
	{
		ShowMissingDependencyError(ShapeMatcher);
		return;
	}

	// Init the current ranking
	m_rankings.clear();

	// Only recognize object if we are performing the recognition task
	if (TaskName() != "Recognition")
		return;

	ShowStatus("Recognizing objects...");

	const ModelHierarchy& modelHierarchy = m_pObjectLearner->GetModelHierarchy();
	const unsigned numModelViews = modelHierarchy.ModelViewCount();

	// these are used elsewhere within the objectrecognizer module.  initialize early.
	getAllClassesInDatabase(all_classes, modelHierarchy);
	getModelToClassMapping(model_to_class, modelHierarchy);
	getClassToIndexMapping(class_to_index, all_classes);

	learnParsingModel();
	learnWeights();

	// Get the training info of the query shape if available only
	// if we have to exclude the query object from the model DB
	TrainingObjectData query_tod;
	bool hasQueryTOD = false;

	// hasQueryTOD is true only if we need to exclude something
	// from the database and the learner can provide the TOD
	if (m_params.excludeQueryObjectFromDB || m_params.excludeQueryViewFromDB)
	{
		hasQueryTOD = m_pObjectLearner->GetTrainingObjectData(&query_tod);

		StreamStatus("Matching query " << query_tod.ToString());
	}

	static Lookup_Table lookup_table;
	std::map<std::string, int> parsing_models;
	loadWeights(lookup_table);
	loadParsingModels(parsing_models);

	SPGMatch gmatch;
	unsigned dbg_match_counter = 0;

	const unsigned numQueryShapes = m_pShapeParser->NumShapes();

	unsigned query_id = 0;
	Timer matchingTimer;

	testShapeContext(gmatch, modelHierarchy);

	// It is cleared above, so resize is okay here
	m_rankings.resize(numQueryShapes);
	
	// For all query shapes in the ShapeParser.
	// usually one, since we only have one query at a time.
	for (unsigned s_q = 0; s_q < numQueryShapes; ++s_q)
	{
		QueryRanking& qr = m_rankings[s_q];

		// Get the parses of the query shape
		qr.queryParses = m_pShapeParser->GetShapeParses(s_q);
		
		qr.matchingTime = 0;
		matchingTimer.Reset();

		// There will be exactly one match info per model view
		// All matches are init to SPG_MATCH_NOT_SET (ie, -1)
		ASSERT(qr.matches.empty());
		qr.matches.resize(numModelViews);

		const unsigned numQueryParses = qr.queryParses.size();

		////////////////////////////////////////////////////// new matching code.
		/////////////////////////////////////////////////////////////////////////////////
		if (m_params.use_learned_parsing_model)
		{
			static std::map<unsigned int, std::string> model_to_class;
			if (model_to_class.size() <= 0)
				getModelToClassMapping(model_to_class, modelHierarchy);

			int query_id = getQueryId(query_tod.className, query_tod.objId, modelHierarchy);

			for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < numModelViews; ++gmatch.modelViewIdx)
			{
				if (query_id == gmatch.modelViewIdx)
					continue;

				// get parameterization.
				const ModelHierarchy::ModelView &model = modelHierarchy.GetModelView(gmatch.modelViewIdx);
				std::string class_name = modelHierarchy.getModelViewClass(model);
				unsigned int parameterization = parsing_models[class_name];

				// re-parse the model.
				ShapeInfoPtr model_contour = modelHierarchy.GetModelView(gmatch.modelViewIdx).ptrShapeContour;
				std::list<ShapeParsingModel> shape_list = m_pShapeParser->getReparsedShapeList(model_contour, parameterization); 
				std::vector<SPGPtr> model_spgs = element_at(shape_list, 0).GetShapeParses();

				// re-parse the query.
				ShapeInfoPtr query_contour = modelHierarchy.GetModelView(query_id).ptrShapeContour;
				shape_list = m_pShapeParser->getReparsedShapeList(query_contour, parameterization); 
				std::vector<SPGPtr> query_spgs = element_at(shape_list, 0).GetShapeParses();

				SPGMatch& bestMatch = qr.matches[gmatch.modelViewIdx];

				// over all model spgs...
				for (gmatch.modelParseIdx = 0; gmatch.modelParseIdx < model_spgs.size(); 
						++gmatch.modelParseIdx)
				{
					const ShapeParseGraph &model_spg = *model_spgs[gmatch.modelParseIdx];

					// and over all query spgs for each model spg...
					for (gmatch.queryParseIdx = 0; gmatch.queryParseIdx < query_spgs.size(); 
						++gmatch.queryParseIdx)
					{
						const ShapeParseGraph &query_spg = *query_spgs[gmatch.queryParseIdx];
						gmatch.value = m_pShapeMatcher->Match(query_spg, model_spg);

						if (m_params.onlySumModelNodeMatches)
						{
							gmatch.value = m_pShapeMatcher->GetGraphDistanceS2F();
						}

						// keep track of best matching.
						if (bestMatch.value == SPGMatch::ValueNotSet() 
							|| gmatch.value < bestMatch.value)
						{
							bestMatch.SetBasicInfo(gmatch);

							m_pShapeMatcher->GetF2SNodeMap(bestMatch.nodeMap);
						}
					}
				}
			}
			// sort the matches; we're done.
			std::sort(qr.matches.begin(), qr.matches.end(), std::less<SPGMatch>());
			qr.matchingTime = matchingTimer.ElapsedTime();
			StreamStatus("Matching done in " << qr.matchingTime << " seconds.");
		}
		else
		{
			////////////////////////////////////////////////////// cutoff to old match........
			//////////////////////////////////////////////////////////////////////////////////
			// For all parses of the query shape
			// Note: part of the objective is to find the best parse of the query, so
			// it is okay to have in the final ranking two different query parses that 
			// match to the same model view, and, possibly, even the same model view parse.
			for (gmatch.queryParseIdx = 0; gmatch.queryParseIdx < numQueryParses; 
				++gmatch.queryParseIdx)
			{
				const ShapeParseGraph& G_q = *qr.queryParses[gmatch.queryParseIdx];


				// For all model shapes in the model database
				for (gmatch.modelViewIdx = 0; gmatch.modelViewIdx < numModelViews; 
					++gmatch.modelViewIdx)
				{
					// See if we have to exclude the query object from the model DB
					if (hasQueryTOD)
					{
						const ModelHierarchy::ModelView& mv = 
							modelHierarchy.GetModelView(gmatch.modelViewIdx);

						//StreamStatus("Comparing to " 
						//			<< modelHierarchy.ToString(mv));

						// See if the current object view belongs to the query object
						// this is only the exact same instance (the same image).  Not the same 'class'
						if (modelHierarchy.HasParent(mv, query_tod.className, query_tod.objId))
						{
							query_id = gmatch.modelViewIdx;
							// The query object is the same than the model object
							if (m_params.excludeQueryObjectFromDB)
							{
								StreamStatus("Skipping model object " 
									<< modelHierarchy.ToString(mv));

								continue;
							}
							else if (m_params.excludeQueryViewFromDB && 
								query_tod.Compare(mv.viewInfo))
							{
								// The query view is the same than the model view	
								StreamStatus("Skipping model view " 
									<< modelHierarchy.ToString(mv));

								continue;
							}
						}
					}

					// Get the number of parses of the current model view
					const unsigned numModelParses = 
						modelHierarchy.ShapeParseCount(gmatch.modelViewIdx);

					// Get the slot where we saved the model view parse that best matches
					// the current query view parse (set to SPG_MATCH_NOT_SET initially)
					SPGMatch& bestMatch = qr.matches[gmatch.modelViewIdx];

					// Note: do not init the best model match value found here because
					// the "best" is across model parses. It is init when resizing
					// ranking of models per queries

					// For all parses of the model shapes
					// in this scenario, we've selected one view (one instance shape) and we are looking at all of its SPGs
					for (gmatch.modelParseIdx = 0; gmatch.modelParseIdx < numModelParses; 
						++gmatch.modelParseIdx)
					{
						const ShapeParseGraph& G_m = modelHierarchy.GetShapeParse(
							gmatch.modelViewIdx, gmatch.modelParseIdx);

					
						gmatch.value = m_pShapeMatcher->Match(G_q, G_m);
						//const ShapeParseGraph test = modelHierarchy.GetShapeParse(gmatch.modelViewIdx, gmatch.modelParseIdx);
						/*NodeMatchMap mapping = gmatch.nodeMap;
					
						graph::node u = G_m.first_node();
						while (true)
						{
							if (u == NULL)
								break;

							// do stuff.
							PointArray *pts = new PointArray();
							std::cout << "shape part size: " << G_m.inf(u).boundarySegments.size() << std::endl;
							G_m.inf(u).ptrDescriptor->GetPoints(pts);
							std::cout << "point array size: " << pts->size() << std::endl;
							std::cout << "total shape boundary size: " << G_m.getNumberOfBoundaryPoints() << std::endl;

							u = G_m.succ_node(u);
						}*/


									//PointArray *pts = new PointArray();
				//std::cout << "shape part size: " << m_pModelGraph->inf(u).boundarySegments.size() << std::endl;
				//m_pModelGraph->inf(u).ptrDescriptor->GetPoints(pts);
				//std::cout << "point array size: " << pts->size() << std::endl;
				//std::cout << "total shape boundary size: " << m_pModelGraph->getNumberOfBoundaryPoints() << std::endl;

						if (m_params.onlySumModelNodeMatches)
						{
							gmatch.value = m_pShapeMatcher->GetGraphDistanceS2F();
						}

						/*if (dbg_match_counter > 0 &&
							dbg_match_counter / 1000.0 == int(dbg_match_counter / 1000.0))
						{
							StreamStatus("(query id " << s_q << ", query parse " 
									  << gmatch.queryParseIdx << ") -> "
									  << "(model id " << gmatch.modelViewIdx 
									  << ", model parse " << gmatch.modelParseIdx << ") " 
									  << "[match id " << dbg_match_counter << "]");

							//DBG_PRINT_ELAPSED_TIME(timer, "Matched 1000 SPGs")
							//DBG_RESET_TIMER(timer)
						}*/

						dbg_match_counter++;

						// @todo mark comparison place
						if (bestMatch.value == SPGMatch::ValueNotSet() 
							|| gmatch.value < bestMatch.value)
						{
							bestMatch.SetBasicInfo(gmatch);

							m_pShapeMatcher->GetF2SNodeMap(bestMatch.nodeMap);
						}
					}
				}
			}

			// Sort the matches of the current query shape
			// @todo mark comparison place
			// but first, re-weight matches.
		
			unsigned model_id = 0;
			std::vector<SPGMatch>::iterator matching = qr.matches.begin();
			for (matching; matching != qr.matches.end(); ++matching)
			{
				// skip matching against self.
				if (query_id == model_id || (*matching).modelParseIdx > 1000)
				{
					model_id++;
					continue;
				}
			

				if (m_params.use_importance_weights)
				{
					double weight = 0.0;
					const ShapeParseGraph &model_graph = modelHierarchy.GetShapeParse((*matching).modelViewIdx, (*matching).modelParseIdx);
					const ShapeParseGraph &query_graph = *qr.queryParses[(*matching).queryParseIdx];

					double score = m_pShapeMatcher->Match(query_graph, model_graph);
					m_pShapeMatcher->GetF2SNodeMap(gmatch.nodeMap); 

					// check which parts (if any) went unmatched, and punish accordingly.
					// 1. gather list of model and query graph parts.
					const unsigned query_part_count = query_graph.number_of_nodes();
					std::vector<double> query_part_scores;
					for (unsigned i = 0; i < query_part_count; ++i)
					{
						query_part_scores.push_back(-1);
					}
			
					const int model_part_count = model_graph.number_of_nodes();
					(*matching).value += (0.5 * model_part_count);
					/*
					std::vector<double> model_part_scores;
					for (unsigned i = 0; i < model_part_count; ++i)
					{
						model_part_scores.push_back(-1);
					}

					// 2. gather the mean score
					// 3. punish unmatched parts proportional to the mean score for matched parts.
					double new_value = 0.0;
					unsigned num_vals = 0;
					for (unsigned i = 0; i < (*matching).nodeMap.size(); ++i)
					{
						new_value += (*matching).nodeMap.at(i).nodeAttDist;
					}

					for (unsigned i = 0; i < (*matching).nodeMap.size(); ++i)
					{
						Lookup_Table::const_iterator it2 = lookup_table.find(WeightKey((*matching).modelViewIdx, (*matching).modelParseIdx, (*matching).nodeMap.at(i).tgtNodeIdx));
						if (true)//it2 != lookup_table.end())
						{
							//if ((*it2).second > 5)
							{
								weight = ((*matching).nodeMap.at(i).nodeAttDist * 1/(*it2).second);
								//(*matching).value += weight;
								//new_value += (*matching).nodeMap.at(i).nodeAttDist;//weight;

								//new_value += ((*matching).nodeMap.at(i).nodeAttDist * 1/(*it2).second);
								model_part_scores[(*matching).nodeMap.at(i).tgtNodeIdx] = ((*matching).nodeMap.at(i).nodeAttDist * 1/(*it2).second);
								query_part_scores[(*matching).nodeMap.at(i).srcNodeIdx] = ((*matching).nodeMap.at(i).nodeAttDist * 1/(*it2).second);
								num_vals++;
								//(*it).value *= 0.85;
								//(*matching).value += ((1/(*it2).second) * 0.05);
							}
						}
						else
						{
							// unmatched.  won't find that in the weight-set.
							//new_value += (*matching).nodeMap.at(i).nodeAttDist;
						}
						//num_vals++;
					}
			
			
					double mean_weight = new_value/num_vals;
					// now add weights for all unmatched nodes.
					for (unsigned i = 0; i < model_part_scores.size(); ++i)
					{
						if (model_part_scores[i] == -1)
						{
							double part_clique_size = 0.0;
							Lookup_Table::const_iterator part_it = lookup_table.find(WeightKey((*matching).modelViewIdx, (*matching).modelParseIdx, i));
							part_clique_size = (*part_it).second;

							//(*matching).value += ((double)(1/part_clique_size) * (double)(1/part_clique_size));
							//new_value += ((double)(1/part_clique_size) * (double)(1/part_clique_size));
							model_part_scores[i] = 1/part_clique_size;
							//(*matching).value += part_clique_size * 0.85;
						}
					}
			
					for (unsigned i = 0; i < query_part_scores.size(); ++i)
					{
						if (query_part_scores[i] == -1)
						{
							//(*matching).value += mean_weight;
							//new_value += (*matching).nodeMap.at(i).nodeAttDist;
							//new_value += mean_weight;
							query_part_scores[i] = 1/mean_weight;
						}
					}
			
					//(*matching).value += 0.5 * model_part_count;
					//std::cout << (*matching).value << std::endl;
						/////////////////////////////////////*/


					/*
					NodeMatchMap mapping = gmatch.nodeMap;
					graph::node u,v;
					// loop over all nodes u in the query shape parse...
					forall_nodes(u, query_graph)
					{
						Lookup_Table::const_iterator it2 = lookup_table.find(WeightKey((*it).modelViewIdx, (*it).modelParseIdx, mapping[u].tgtNodeIdx));
						if (it2 != lookup_table.end())
						{
							//std::cout << "found, adding to the weight. " << std::endl;
							weight += (double)(*it2).second;
							// try something else...
							if ((*it2).second > 5)
							{
								(*it).value *= 0.85;
							}
						}
						else
						{
							// unmatched.  won't find that in the weight-set.
						}
					}
					// now average the weight...
					weight /= query_graph.number_of_nodes();
					//(*it).weightValue(weight);		
					*/   
			
					model_id++;
				}
			}

			std::sort(qr.matches.begin(), qr.matches.end(), std::less<SPGMatch>());

			qr.matchingTime = matchingTimer.ElapsedTime();

			StreamStatus("Matching done in " << qr.matchingTime << " seconds.");
		}
	}
}

void ObjectRecognizer::GetParameterInfo(int i, DoubleArray* pMinVals, 
	DoubleArray* pMaxVals, DoubleArray* pSteps) const
{
	InitArrays(2, pMinVals, pMaxVals, pSteps, 0, 0, 1);

	if (!m_rankings.empty())
		pMaxVals->at(0) = m_rankings.size() - 1;

	const ModelHierarchy& modelHierarchy = m_pObjectLearner->GetModelHierarchy();

	if (modelHierarchy.ModelViewCount() > 0)
		pMaxVals->at(1) = modelHierarchy.ModelViewCount() - 1;
}

/*!
	Draws the output of the component. This function is called from
	a "drawing" thread, which is different from the "processing" thread
	that calls Run(). In order to protect the shared resources between the
	threads, a mutex is used.
*/
void ObjectRecognizer::Draw(const DisplayInfoIn& dii) const
{
	unsigned q_idx = (unsigned)dii.params.at(0);
	unsigned m_idx = (unsigned)dii.params.at(1);

	if (q_idx >= m_rankings.size() || m_idx >= m_rankings[q_idx].matches.size())
	{ 
		return;
	}

	const SPGMatch& match = m_rankings[q_idx].matches[m_idx];
	// this is just me trying to figure out how to get that second slider running.
	/*std::cout << "q_idx: " << q_idx << std::endl;
	std::cout << "m_idx: " << m_idx << std::endl;
	std::cout << "---------------------" << std::endl << std::endl;*/
	
	// See if it's the query view window
	if (dii.displayId == 0)
	{
		if (TEST_DRAWING_CORNERS)
		{
			// draw the convex corners of the full shape...

			// get an SPG for the full shape.
			const ModelHierarchy& modelHierarchy = m_pObjectLearner->GetModelHierarchy();
			std::cout << "Frame number: " << m_pShapeParser->GetFrameNumber() << std::endl;

			std::cout << "View count: " << modelHierarchy.ModelViewCount() << std::endl;

			if (modelHierarchy.ModelViewCount() > m_pShapeParser->GetFrameNumber())
			{
				ShapeInfoPtr sip = modelHierarchy.GetModelView((unsigned int)m_pShapeParser->GetFrameNumber()).ptrShapeContour;
				std::cout << "Got contour." << std::endl;
				sip->Draw();
			}
		}
		else
		{
			// for slider value slider_index...
			//SPGPtr query_spg =
			// m_rankings[q_idx].queryParses[slider_index];
			SPGPtr query_spg = 
			m_rankings[q_idx].queryParses[match.queryParseIdx];
		
			query_spg->Draw(match.nodeMap);
		}
	}
	// See if it's the model view window
	else if (dii.displayId == 1 && match.value > -1) 
	{
		const ModelHierarchy& modelHierarchy = m_pObjectLearner->GetModelHierarchy();

		const ShapeParseGraph& model_spg = modelHierarchy.GetShapeParse(
			match.modelViewIdx, match.modelParseIdx);

		model_spg.Draw();

		// Superimpose the warped query if requested
		if (m_params.overlayWarpQuery)
		{
			SPGPtr query_spg = 
				m_rankings[q_idx].queryParses[match.queryParseIdx];

			PointTransform ptp;
			ShapePartComp spc;
			graph::node u, v;

			spc.SetGraphs(*query_spg, model_spg);

			forall_nodes(u, *query_spg)
			{
				v = match.nodeMap[u].mappedNode;

				// If u or v are nil, the ptp returned is empty
				spc.GetTransformationParams(u, v, &ptp);

				// Returns a NULL pointer if the transform isn't possible (eg, empty ptp)
				ShapeDescPtr ptrSD = query_spg->inf(u).ptrDescriptor->Transform(ptp);

				if (ptrSD != NULL)
				{
					LineList ll;
					PointArray ptsQ;
					double dist = 0;
					
					ptrSD->GetPoints(&ptsQ);

					for (unsigned i = 0 ; i < ptp.P.size(); i++)
					{
						ll.push_back(std::make_pair(ptsQ[i], ptp.P[i]));
						dist += ptsQ[i].dist(ptp.P[i]);
					}
				
					DBG_PRINT1(dist / ptp.P.size())

					DrawLines(ll, 1.0);

					ptrSD->Draw(NamedColor("Green"));
				}
			}
		}
	}
}

/*!	
	Returns the basic information specifying the output of this component.
	It must provide an image, its type, and a text message. All of this parameters 
	are optional. For example, if there is no output image, the image type
	can be set to VOID_IMAGE.
*/
void ObjectRecognizer::GetDisplayInfo(const DisplayInfoIn& dii, DisplayInfoOut& dio) const
{
	dio.imageType = VOID_IMAGE;
	dio.syncDisplayViews = true;

	unsigned q_idx = (unsigned)dii.params.at(0);
	unsigned m_idx = (unsigned)dii.params.at(1);

	if (q_idx >= m_rankings.size() || m_idx >= m_rankings[q_idx].matches.size())
		return;

	const SPGMatch& match = m_rankings[q_idx].matches[m_idx];
	std::ostringstream oss;

	// See if it's the query view window
	if (dii.displayId == 0)
	{
		oss << "Parse id: " << match.queryParseIdx 
			<< ", match value: " << match.value << ".";
	}
	// See if it's the model view window
	else if (dii.displayId == 1 && match.value > -1) 
	{
		oss << "Model id: " << match.modelViewIdx << ", parse id: "
			<< match.modelParseIdx << ". Node correspondences:\n";

		//const ShapeParseGraph& m_spg = modelHierarchy.GetShapeParse(
		//	match.modelViewIdx, match.modelParseIdx);

		for (unsigned i = 0; i < match.nodeMap.size(); ++i)
		{
			auto na = match.nodeMap.at(i);

			ASSERT(na.mappedNode == nil || i == na.srcNodeIdx);

			oss << i << " -> ";

			if (na.mappedNode == nil)
				oss << "U";
			else
				oss << na.tgtNodeIdx << " = " << na.nodeAttDist;
			
			if (i + 1 < match.nodeMap.size())
				oss << ", ";
		}

	}

	dio.message = oss.str();
}

