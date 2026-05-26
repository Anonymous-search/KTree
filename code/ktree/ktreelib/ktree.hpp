#ifndef __KTREE_HPP__
#define __KTREE_HPP__

#include <vector>
#include <Eigen/Dense>
#include <stack>
#include <set>


#include "data.hpp"
#include "segmentation.hpp"
#include "serialization.hpp"
#include "query.hpp"
#include "kpca.hpp"




namespace KTREE {

enum class NodeType {
	LEAF,
	INTERNAL
};


class Node: public Serializable {
private:
	Node *parent;
	Node *left, *right;
	NodeType type;
	DataContainer *data;
	std::vector<float> segments_mins;
	std::vector<float> segments_maxs;
	Segmentation segmentation;

	std::string filename;
	size_t num_points;

	float median;
	size_t best_segment_index;
	std::vector<size_t> best_segment_dimensions;
	
	// KPCA
	Eigen::MatrixXf W;
	Eigen::MatrixXf b;
	Eigen::MatrixXf Z;
	Eigen::MatrixXf projected_data; // don t need to keep this
	Eigen::MatrixXf components;

private:
	void compute_summary(size_t num_points);
	std::string choose_disposable_file_name(size_t n);
	void choose_file_name();

public:
	Node();
	Node(const std::string& file_path, const Segmentation& segmentation, size_t num_points);
	~Node();

	Node& operator=(const Node &node) = delete;
	void split(size_t num_points);
	NodeType getType() const;
	size_t getNum_points() const;
	void setType(NodeType type);
	const DataContainer& getData() const;

	Node *getParent() const;
	void setParent(Node *parent);
	Node* getLeft() const;
	void setLeft(Node *left);
	Node* getRight() const;
	void setRight(Node *right);
	size_t size() const;
	void quantize_segments_averages(const std::vector<float>& mins, const std::vector<float>& maxs);

	const std::vector<float>& get_segments_mins() const {
		return segments_mins;
	}
	const std::vector<float>& get_segments_maxs() const {
		return segments_maxs;
	}

	// serialization
	void serialize(std::ofstream& out) const override;
	void deserialize(std::ifstream& in) override;

	const Segmentation& get_segmentation() const {
		return segmentation;
	}

	void print(int indent = 0) const;

	void count(unsigned int* counter) const {
		if (type == NodeType::LEAF) {
			(*counter)++;
		}
		else {
			counter[1] += 1;
			if (left != nullptr) {
				left->count(counter);
			}
			if (right != nullptr) {
				right->count(counter);
			}
		}
	}

	template<typename T>
	void search(Query<T>& query, std::stack<Node *>& stack) {
		query.increment_visit_count();
		// SEARCHING DOWN THE TREE
		// TILL WE REACH THE FIRST LEAF NODE

		stack.push(this);
		if (this->type == NodeType::LEAF) {
			for (size_t i = 0; i < data->size(); i++) {
				query.add_result((*data)[i]);
			}
			return;
		}
		else { // internal node
			std::vector<float> selected_query_data;
			for (auto d: best_segment_dimensions) {
				selected_query_data.push_back(query.get_query()[d]);
			}
			// project the query data
			Eigen::MatrixXf query_data(1, selected_query_data.size());
			for (size_t i = 0; i < selected_query_data.size(); i++) {
				query_data(0, i) = selected_query_data[i];
			}
			float projected_value;
			PCA::project(query_data, W, b, components, projected_value);
			if (projected_value <= median) {
				if (left != nullptr) {
					left->search(query, stack);
				}
				else {
					right->search(query, stack);
				}
			}
			else {
				if (right != nullptr) {
					right->search(query, stack);
				}
				else {
					left->search(query, stack);
				}
			}
		}

			// if (opposite_node != nullptr) {
			// 	DataPoint *best_result = query.best_result();
			// 	float distance_to_plane = (projected_value - median) * (projected_value - median);
				
			// 	if (query.get_results()->get_comparator().get_metric()(*best_result, query.get_query()) > distance_to_plane) {
			// 		opposite_node->search(query);
			// 	}
			// }
}


};


class KTree: public Serializable {
private:
	Node *root;
public:
	KTree();
	~KTree();

	void index(const std::string& file_path, size_t num_points);
	
	template<typename T>
	void search(Query<T>& query) {
		if (root != nullptr) {
			std::vector<float> query_representation;
			float distance = std::numeric_limits<float>::max();

			std::stack<Node *> stack;
			root->search(query, stack);

#ifndef TOP_DOWN_SEARCH_PRUNING
			while (!stack.empty()) {
				Node *node = stack.top();
				stack.pop();
				Node *parent = node->getParent();
				if (parent != nullptr) {
					// let's check the opposite node
					Node *opposite_node = nullptr;
					if (parent->getLeft() == node) {
						opposite_node = parent->getRight();					
					}
					else {
						opposite_node = parent->getLeft();
					}
					if (opposite_node != nullptr) {
						// we need to check if we want to visit this opposite node
						std::stack<Node *> tmp_stack;
						if (opposite_node->getType() == NodeType::LEAF) {
							opposite_node->search(query, tmp_stack);
						}
						else {
							query_representation.clear();
							query_representation = query.get_query().get_representation(opposite_node->get_segmentation());
							const std::vector<float>& opposite_mins = opposite_node->get_segments_mins();
							const std::vector<float>& opposite_maxs = opposite_node->get_segments_maxs();
							distance = 0.0f;
							for (size_t i = 0; i < query_representation.size(); i++) {
								if (query_representation[i] > opposite_maxs[i]) {
									distance += std::abs(query_representation[i] - opposite_maxs[i]);
								} else if (query_representation[i] < opposite_mins[i]) {
									distance += std::abs(query_representation[i] - opposite_mins[i]);
								}
								else {
									distance += 0.0f;
								}
							}
							DataPoint *best_result = query.best_result();
							float distance_to_bsf = query.get_results()->get_comparator().get_metric()(*best_result, query.get_query());
							if (distance < distance_to_bsf) {
								opposite_node->search(query, tmp_stack);
							}
						}
					}
				}
			}
#else
			Node *current_node = root;
			std::stack<Node *> tmp_stack;
			while (current_node) {
				Node *children[] = {
					current_node->getLeft(), 
					current_node->getRight()
				};

				// check for leaf nodes
				bool leaf_node_reached = false;

				for (size_t i = 0; i < sizeof(children) / sizeof(Node *); i++) {
					Node *child = children[i];
					if (child != nullptr) {
						if (child->getType() == NodeType::LEAF) {
							leaf_node_reached = true;
							child->search(query, tmp_stack);
						}
					}
				}
				if (leaf_node_reached) {
					break;
				}


				float distance_to_children[2] = {
					std::numeric_limits<float>::max(),
					std::numeric_limits<float>::max()
				};

				for (size_t i = 0; i < sizeof(children) / sizeof(Node *); i++) {
					Node *child = children[i];
					if (child == nullptr) {
						continue;
					}
					float distance = 0.0f;
					query_representation.clear();
					query_representation = query.get_query().get_representation(child->get_segmentation());
					const std::vector<float>& child_mins = child->get_segments_mins();
					const std::vector<float>& child_maxs = child->get_segments_maxs();
					distance = 0.0f;
					for (size_t i = 0; i < query_representation.size(); i++) {
						if (query_representation[i] > child_maxs[i]) {
							distance += std::abs(query_representation[i] - child_maxs[i]);
						} else if (query_representation[i] < child_mins[i]) {
							distance += std::abs(query_representation[i] - child_mins[i]);
						}
						else {
							distance += 0.0f;
						}
					}
					distance_to_children[i] = distance;
				}

				if (children[0] == nullptr) {
					current_node = children[1];
				}
				else if (children[1] == nullptr) {
					current_node = children[0];
				}
				else if (distance_to_children[0] < distance_to_children[1]) {
					current_node = current_node->getLeft();
				}
				else {
					current_node = current_node->getRight();
				}
			}
#endif
		}

	}


	Node* get_root() const {
		return root;
	}
	void serialize(std::ofstream& out) const override;
	void deserialize(std::ifstream& in) override;

	void print() const {
		if (root != nullptr) {
			root->print();
		}
	}
};


};
#endif
