#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>


#include "index.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "kpca.hpp"
#include "data.hpp"
#include "timer.hpp"
#include "ktree.hpp"


namespace KTREE {

Index::Index() {
	ktree = new KTree();
}

Index::~Index() {
	delete ktree;
}

void Index::build() {
	const Config& config = *(KTREE::Config::get_instance());
	DataContainer *data = nullptr;

	const std::string& index_path = config.index_path;
	LOG("Staring Building index at " << index_path);
	if (dir_exists(index_path)) {
		throw KTreeError("Index directory already exists");
	}

	if (!create_dir(index_path)) {
		throw KTreeError("Failed to create index directory");
	}

	//LOG("Reading data from " << config.dataset);
	//try {
	//	data = DataContainer::load_from_file(
	//		config.dataset, false, config.dataset_size
	//	);
	//} catch (std::exception &e) {
	//	throw KTreeError(e.what());
	//}
	//if (data == nullptr) {
	//	throw KTreeError("Failed to read data");
	//}

	LOG("Building KTree");

	ktree->index(config.dataset, config.dataset_size);
	// ktree->print();


	LOG("Index built successfully");
	unsigned int counter[2] = {0, 0};
	ktree->get_root()->count(counter);

	LOG("Leaf nodes: " << counter[0] << " Internal nodes: " << counter[1]);
}

void Index::load() {
	
	const Config& config = *(KTREE::Config::get_instance());
	const std::string& index_path = config.index_path;

	std::ifstream in(index_path + "/index.bin", std::ios::binary);
	if (!in.is_open()) {
		throw KTreeError("Failed to open index file for reading");
	}

	LOG("Starting to load index from: " << index_path);
	this->deserialize(in);
	// this->ktree->print();

	// print node count
	unsigned int counter[2] = {0, 0};
	ktree->get_root()->count(counter);

	LOG("Leaf nodes: " << counter[0] << " Internal nodes: " << counter[1]);
	LOG("Index loaded successfully");
	

	in.close();
}


void Index::save() {
	
	Timer t;

	t.start();
	const Config& config = *(KTREE::Config::get_instance());
	const std::string& index_path = config.index_path;

	LOG("Saving index to: " << index_path);
	std::ofstream out(index_path + "/index.bin", std::ios::binary);
	if (!out.is_open()) {
		throw KTreeError("Failed to open index file for writing");
	}


	this->serialize(out);
	out.close();
	t.stop();
	LOG("Index saved successfully: " << t.to_string());
}

void Index::serialize(std::ofstream& out) const {
	KTREE::Config::get_instance()->serialize(out);
	ktree->serialize(out);
}

void Index::deserialize(std::ifstream& in) {
	KTREE::Config::get_instance()->deserialize(in);
	ktree->deserialize(in);
}
void Index::search() {
	Timer t;
	DataContainer* queries = nullptr;
	const Config *config = KTREE::Config::get_instance();

	LOG("LOADING QUERIES DATA")
	try {
		bool all = config->queries_size == 0? true: false;
		size_t num_queries = all? 0: config->queries_size;

		queries = DataContainer::load_from_file(
			config->queries, all, num_queries
		);
	} catch (std::exception &e) {
		LOG("FAILED TO LOAD QUERIES DATA")
		throw KTreeError(e.what());
	}
	LOG("STARTING SEARCH:")
	std::cout << "------------------" << std::endl;
	std::cout << "Query ID, Query Time, Distance Computations, Visit Count" << std::endl;
	for (size_t i = 0; i < queries->size(); i++) {
		t.reset();
		t.start();
		Query query(queries->operator[](i));

		ktree->search(query);
		t.stop();

		std::cout << i << ", " << t.to_string() << ", " << query.get_distance_computation() << ", " << query.get_visit_count() << std::endl;
	}
	delete queries;

}

};