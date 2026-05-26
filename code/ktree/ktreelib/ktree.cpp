#include "ktree.hpp"

#include <iostream>
#include <algorithm>
#include <numeric>
#include <Eigen/Dense>


#include "error.hpp"
#include "utils.hpp"
#include "kpca.hpp"
#include "config.hpp"
#include "segmentation.hpp"
#include "data.hpp"
#include "query.hpp"
#include "timer.hpp"

#ifdef MULTITHREADED_ENABLED
#include "threadpool.hpp"
#endif




namespace KTREE {

KTree::KTree(): root(nullptr) {}

KTree::~KTree() {
	if (root != nullptr) {
		delete root;
	}
}

void KTree::index(const std::string& file_path, size_t num_points) {
	Timer t;

	t.start();
	
	if (root != nullptr) {
		delete root;
	}

	

	// initialize the segmentation
	std::vector<size_t> init_segmentation = {static_cast<size_t>(Config::get_instance()->dimensions)};
	Segmentation segmentation(init_segmentation);

	// create the root node
	root = new Node(file_path, segmentation, num_points);
	if (root == nullptr) {
		throw KTreeError("Failed to allocate memory for root node");
	}
	// non parallel version

#ifdef MULTITHREADED_ENABLED
	THREADS::ThreadPool pool;
	
	pool.add_task(root);
	pool.wait_for_completion();
#else
	std::vector<Node *> nodes;
	nodes.push_back(root);
	while(!nodes.empty()) {
		Node *node = nodes.back();
		nodes.pop_back();
		node->split(node->getNum_points());
		if (node->getLeft() != nullptr) {
			nodes.push_back(node->getLeft());
		}
		if (node->getRight() != nullptr) {
			nodes.push_back(node->getRight());
		}
	}
#endif
	t.stop();
	LOG("INDEXING TIME: " << t.to_string());

}


void KTree::serialize(std::ofstream& out) const {
	if (root != nullptr) {
		out << "Y";
		this->root->serialize(out);
	}
	else {
		out << "N";
	}
}

void KTree::deserialize(std::ifstream& in) {
	char c;

	in >> c;
	if (c == 'Y') {
		this->root = new KTREE::Node();
		this->root->deserialize(in);
	}	

}


Node::Node() {
	this->parent = nullptr;
	this->left = nullptr;
	this->right = nullptr;
	this->type = NodeType::LEAF;
	this->data = nullptr;
}

Node::Node(const std::string& file_path, const Segmentation& segmentation, size_t num_points): filename(file_path), segmentation(segmentation), num_points(num_points) {
	this->parent = nullptr;
	this->left = nullptr;
	this->right = nullptr;
	this->type = NodeType::LEAF;
	this->data = nullptr;
}

Node::~Node() {
	delete this->left;
	delete this->right;
	delete data;
}

void Node::print(int indent) const {
	// print the node in tree format
	std::string spaces(indent, ' ');
	std::cout << spaces;
	std::cout << "Node: " << this << " ";
	if (type == NodeType::LEAF) {
		std::cout << "LEAF";
		std::cout << " " << data->size();
	}
	else {
		std::cout << "INTERNAL";
	}
	std::cout << std::endl;
	if (type == NodeType::INTERNAL){
		if (left != nullptr) {
			left->print(indent + 2);
		}
		if (right != nullptr) {
			right->print(indent + 2);
		}
	}
}

const DataContainer& Node::getData() const {
	return *data;
}

void Node::quantize_segments_averages(const std::vector<float>& mins, const std::vector<float>& maxs) {
	this->segments_mins = mins;
	this->segments_maxs = maxs;
}

void Node::compute_summary(size_t num_points) {
	// computing the summary of the data
	std::ifstream file(this->filename, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
        throw std::runtime_error("Could not open the data file for reading");
    }

	size_t dimensions = KTREE::Config::get_instance()->dimensions;

	// Determine the number of points to read
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();

    size_t expected_size = num_points * dimensions * sizeof(float);
    if (file_size < expected_size) {
        file.close();
        throw std::runtime_error("Invalid number of points in data file");
    }

	size_t num_segments = this->segmentation.size();
	std::vector<size_t> segments_sizes;

	// get the sizes of the segments
	this->segmentation.get_segments_sizes(segments_sizes);
	// initialize the per_point_segment_averages
	std::vector<float> per_point_segment_averages(num_segments, 0.0);
	std::vector<float> segments_mins(num_segments, std::numeric_limits<float>::infinity());
    std::vector<float> segments_maxs(num_segments, -std::numeric_limits<float>::infinity());
	std::vector<float> means(dimensions, 0.0);
	std::vector<float> means_square(dimensions, 0.0);
    std::vector<float> variance(dimensions, 0.0);
	// compute the per point segment averages
	file.seekg(0, std::ios::beg);
	// Buffered reading
    const size_t batch_size = 1000;  // Read 1000 points at a time
    std::vector<float> buffer(batch_size * dimensions);
	size_t points_read = 0;
	while (points_read < num_points) {
		size_t to_read = std::min(batch_size, num_points - points_read);

        file.read(reinterpret_cast<char*>(buffer.data()), to_read * dimensions * sizeof(float));
		
		
		for (size_t i = 0; i < num_segments; i++) {
			Segment segment = this->segmentation[i];
			std::vector<size_t> indices = segment.get_indices();
			for (size_t j = 0; j < to_read; j++) {
				float sum = 0.0;
				for (size_t index : indices) {
                    sum += buffer[j * dimensions + index];
                }
				per_point_segment_averages[i] = sum / indices.size();
				if (per_point_segment_averages[i] < segments_mins[i])
					segments_mins[i] = per_point_segment_averages[i];
				if (per_point_segment_averages[i] > segments_maxs[i])
					segments_maxs[i] = per_point_segment_averages[i];
			}
		}

		for (size_t i = 0; i < to_read; i++) {
			for (size_t dim = 0; dim < dimensions; dim++) {
				means[dim] += buffer[i * dimensions + dim];
				means_square[dim] += buffer[i * dimensions + dim] * buffer[i * dimensions + dim];
			}
		}
		points_read += to_read;
	}


	this->quantize_segments_averages(segments_mins, segments_maxs);
	for (size_t i = 0; i < dimensions; i++) {
        means[i] /= num_points;
		variance[i] = means_square[i]/num_points - (means[i] * means[i]);
    }

	// select the top_k dimensions with the highest variance
	std::vector<size_t> top_k_dimensions(KTREE::Config::get_instance()->top_k, 0);

	// get the indices of the top k dimensions
	// descending order of variance
	KTREE::argsort(variance, top_k_dimensions);
	top_k_dimensions.resize(KTREE::Config::get_instance()->top_k);

	// segment selection
	std::vector<int> segments_count(segmentation.size(), 0);
	for (auto d: top_k_dimensions) {
		for (auto i = 0; i < num_segments; i++) {
			if (segmentation[i].belongs(d)) {
				segments_count[i]++;
			}
		}
	}

	std::vector<size_t> best_segments;
	size_t max = *KTREE::max_element(segments_count.begin(), segments_count.end());
	for (auto i = 0; i < num_segments; i++) {
		if (segments_count[i] == max) {
			best_segments.push_back(i);
		}
	}

	best_segment_index = best_segments[0];
	// if tie
	if (best_segments.size() > 1) {
		size_t top_variance_idx = top_k_dimensions[0];
		for (size_t i = 0; i < num_segments; i++) {
			if (segmentation[i].belongs(top_variance_idx)) {
				best_segment_index = i;
				break;
			}
		}
	}

	// get the dimensions in the best segment
	std::vector<size_t> segment_indices = segmentation[best_segment_index].get_indices();

	// get the dimensions in this segment that are among the top k dimensions
	for (auto d: top_k_dimensions) {
		if (std::find(segment_indices.begin(), segment_indices.end(), d) != segment_indices.end()) {
			best_segment_dimensions.push_back(d);
		}
	}

	// if no dimensions are found in the best segment
	// use all the dimensions in the best segment
	if (best_segment_dimensions.size() == 0) {
		best_segment_dimensions = segment_indices;
	}


	// extract the data for these best_segment_dimensions
	file.clear();
	file.seekg(0, std::ios::beg);
	points_read = 0;
	//batch_size = 1000;
	
	DataContainer *best_segment_data = new DataContainer(); //
	while (points_read < num_points) {
		size_t to_read = std::min(batch_size, num_points - points_read);
		file.read(reinterpret_cast<char*>(buffer.data()), to_read * dimensions * sizeof(float));
		for (size_t i = 0; i < to_read; i++) {
			std::vector<float> best_segment_point;
			for (size_t d : best_segment_dimensions) {
				best_segment_point.push_back(buffer[i * dimensions + d]);
			}
			DataPoint *best_segment_data_point = new DataPoint(best_segment_point);
			best_segment_data->append(best_segment_data_point);
		}

		points_read += to_read;
	}
	file.close();

	// apply pca to the best segment data

	Eigen::MatrixXf data;
	best_segment_data->toEigenMatrix(data);
	PCA::RandomFourierFeatures rff(data.cols() * 2, 1.f);
	rff.transform(data, Z, W, b);
	PCA::performPCA(Z, components, projected_data, 1); // why did we have 2 components again? we only need one right?


	// compute the median
	std::vector<float> medians;
	for (int i = 0; i < projected_data.rows(); i++) {
		for (int j = 0; j < projected_data.cols(); j++) {
			medians.push_back(projected_data(i, j));
		}
	}
	std::sort(medians.begin(), medians.end());
	if (medians.size() % 2 == 0) {
		median = (medians[medians.size() / 2 - 1] + medians[medians.size() / 2]) / 2;
	}
	else {
		median = medians[medians.size() / 2];
	}
}

void Node::split(size_t num_points) {
	if (type == NodeType::INTERNAL) {
		return;
	}
	if (this->parent && num_points <= Config::get_instance()->leaf_size) {
		this->type = NodeType::LEAF;
		std::string old_ = filename;
		this->choose_file_name();
		std::string index_dir = KTREE::Config::get_instance()->index_path;
		std::string new_ = index_dir + "/" + filename;
		if (std::rename(old_.c_str(), new_.c_str()) != 0) {
        	std::perror("Error renaming file");
    	}
		return;
	} // if it was a leaf it would already have it's file
	
	this->compute_summary(num_points);

	// check if spliting the segment is possible
	if (segmentation[best_segment_index].size() <= 1) {
		// we should set this node to LEAF
		this->type = NodeType::LEAF;
		std::string old_ = filename;
		this->choose_file_name();
		std::string index_dir = KTREE::Config::get_instance()->index_path;
		std::string new_ = index_dir + "/" + filename;
		if (std::rename(old_.c_str(), new_.c_str()) != 0) {
			std::perror("Error renaming file");
		}
		return;
	}

	// split the data into two parts
	std::vector<size_t> left_indices;
	std::vector<size_t> right_indices;

	for (int i = 0; i < projected_data.rows(); i++) {
		if (projected_data(i, 0) < median) {
			left_indices.push_back(i);
		}
		else {
			right_indices.push_back(i);
		}
	}

	Segmentation child_segmentation(segmentation);


	// split the segmentation
	child_segmentation.split_segment(best_segment_index);

	// create the left and right nodes
	
	std::string filename_left_data = choose_disposable_file_name(1);
	std::string filename_right_data = choose_disposable_file_name(2);
	
	std::string index_dir = KTREE::Config::get_instance()->index_path;
	std::string full_path_left_data = index_dir + "/" + filename_left_data;
	std::string full_path_right_data = index_dir + "/" + filename_right_data;

	std::ofstream file_left(full_path_left_data, std::ios::out | std::ios::binary);
	if (!file_left.is_open()) {
		throw std::runtime_error("Could not open the file for writing");
	}
	std::ofstream file_right(full_path_right_data, std::ios::out | std::ios::binary);
	if (!file_right.is_open()) {
		throw std::runtime_error("Could not open the file for writing");
	}
	
	std::ifstream file(filename, std::ios::in | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Could not open the data file for reading");
	}
	size_t dimensions = KTREE::Config::get_instance()->dimensions;
	file.seekg(0, std::ios::end);
	const size_t batch_size = 1000;
	
	size_t num_points_l = 0;
	size_t num_points_r = 0;
	std::vector<float> buffer(batch_size * dimensions);  
	size_t points_read = 0;
	while (points_read < num_points) {
		size_t to_read = std::min(batch_size, num_points - points_read);
		file.read(reinterpret_cast<char*>(buffer.data()), to_read * dimensions * sizeof(float));
		for (size_t i = 0; i < to_read; i++) {
			if (std::find(right_indices.begin(), right_indices.end(), points_read + i) != right_indices.end())
			{
				file_right.write(reinterpret_cast<char*>(&buffer[i * dimensions]), dimensions * sizeof(float));
				num_points_r++;
			}
			if (std::find(left_indices.begin(), left_indices.end(), points_read + i) != left_indices.end())
			{
				file_left.write(reinterpret_cast<char*>(&buffer[i * dimensions]), dimensions * sizeof(float));
				num_points_l++;
			}
		}
		points_read += to_read;
	}
	file.close();
	file_right.close();
	file_left.close();

	// clean up
	if (filename.find("disposable") != std::string::npos)
	{
		if (std::remove(filename.c_str()) != 0) {
        	std::perror("Error deleting file");
    	}
	}

	if (left_indices.size() != 0) {
		this->left = new Node(full_path_left_data, child_segmentation, num_points_l);
		this->left->setParent(this);
	}
	if (right_indices.size() != 0) {
		this->right = new Node(full_path_right_data, child_segmentation, num_points_r);
		this->right->setParent(this);
	}
	// set the type to internal
	this->type = NodeType::INTERNAL;
}

std::string Node::choose_disposable_file_name(size_t n) {
	// file name for the node
	// "node_address_disposable_randomnum.dat"
	std::ostringstream oss;
	int random_number = std::rand();
	oss << "node_" << this << "_disposable_" << random_number << "_n_" << n <<".dat";
	return oss.str();
}

void Node::choose_file_name() {
	// file name for the node
	// if it's a leaf node
	// a combination of the address of the node and the address of the data
	// "node_address_data_address.dat"
	if (type == NodeType::LEAF) {
		std::ostringstream oss;
		oss << "node_" << this << "_data_" << data << ".dat";
		this->filename = oss.str();
	}

}

NodeType Node::getType() const {
	return type;
}

size_t Node::getNum_points() const {
	return num_points;
}

void Node::setType(NodeType type) {
	this->type = type;
}

Node *Node::getParent() const {
	return parent;
}

void Node::setParent(Node *parent) {
	this->parent = parent;
}

Node* Node::getLeft() const {
	return left;
}

void Node::setLeft(Node *left) {
	if (this->left != nullptr) {
		delete this->left;
	}
	this->left = left;
}

Node* Node::getRight() const {
	return right;
}

void Node::setRight(Node *right) {
	if (this->right != nullptr) {
		delete this->right;
	}
	this->right = right;
}

size_t Node::size() const { // I don t think we need this check
	if (type == NodeType::LEAF) {
		return data->size();
	}
	return left->size() + right->size();
}

void Node::serialize(std::ofstream& out) const {
	switch (type) {
		case NodeType::LEAF:
			out << "L";
			break;
		case NodeType::INTERNAL:
			out << "I";
			break;
	}

	// serialize the segments_mins
	KTREE::serialize(segments_mins, out);

	// serialize the segments_maxs
	KTREE::serialize(segments_maxs, out);

	// serialize the segmentation
	segmentation.serialize(out);

	// serialize the filename
	KTREE::serialize(filename, out);

	// serialze the median
	KTREE::serialize(median, out);
	
	// serialize the best segment index
	KTREE::serialize(best_segment_index, out);
	KTREE::serialize(best_segment_dimensions, out);

	// serialize the kpca summary
	KTREE::serialize(W, out);
	KTREE::serialize(b, out);
	KTREE::serialize(Z, out);
	KTREE::serialize(projected_data, out);
	KTREE::serialize(components, out);

	// serialize the left and right nodes
	if (left != nullptr) {
		out << "Y";
		left->serialize(out);
	}
	else {
		out << "N";
	}

	if (right != nullptr) {
		out << "Y";
		right->serialize(out);
	}
	else {
		out << "N";
	}
}

void Node::deserialize(std::ifstream& in) {
	// deserialize the type
	char c;
	in >> c;
	if (c == 'L') {
		this->type = NodeType::LEAF;
	}
	else {
		this->type = NodeType::INTERNAL;
	}

	// deserialize the segments_mins
	KTREE::deserialize(segments_mins, in);
	// deserialize the segments_maxs	
	KTREE::deserialize(segments_maxs, in);

	// deserialize the segmentation
	segmentation.deserialize(in);

	// deserialize the filename
	KTREE::deserialize(filename, in);

	// deserialize the median
	KTREE::deserialize(median, in);

	// deserialize the best segment index
	KTREE::deserialize(best_segment_index, in);

	KTREE::deserialize(best_segment_dimensions, in);

	// deserialize the kpca summary
	KTREE::deserialize(W, in);
	KTREE::deserialize(b, in);
	KTREE::deserialize(Z, in);
	KTREE::deserialize(projected_data, in);
	KTREE::deserialize(components, in);

	in >> c;
	if (c == 'Y') {
		left = new Node();
		left->deserialize(in);
		left->setParent(this);
	}
	else {
		left = nullptr;
	}

	in >> c;
	if (c == 'Y') {
		right = new Node();
		right->deserialize(in);
		right->setParent(this);
	}
	else {
		right = nullptr;
	}

	// read the data if it's a leaf node
	if (type == NodeType::LEAF) {

		std::string full_path = Config::get_instance()->index_path + "/" + filename;
		data = DataContainer::load_from_file(full_path, true);
	}
}

};
