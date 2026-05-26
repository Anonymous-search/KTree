#include <getopt.h>
#include <iostream>
#include <algorithm>
#include <cctype>

#include "config.hpp"
#include "error.hpp"

#include "serialization.hpp"


static void print_usage() {
	std::cout << "Usage: ktree [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  --dataset <path>       Path to the dataset file" << std::endl;
	std::cout << "  --queries <path>       Path to the queries file" << std::endl;
	std::cout << "  --index <path>         Path to the index file" << std::endl;
	std::cout << "  --dataset_size <size>  Number of points in the dataset to index" << std::endl;
	std::cout << "  --queries_size <size>  Number of points in the queries to query" << std::endl;
	std::cout << "  --dimensions <size>    Number of dimensions" << std::endl;
	std::cout << "  --leaf_size <size>     Number of points in a leaf" << std::endl;
	std::cout << "  --mode <mode>          Mode (index, query)" << std::endl;
	std::cout << "  --help                 Display this information" << std::endl;
}

KTREE::Config::Config() {
	dataset = "";
	queries = "";
	index_path = "";
	top_k = 5;
	dataset_size = 0;
	queries_size = 0;
	dimensions = 0;
	leaf_size = 1;
	mode = INDEX;
}

KTREE::Config *KTREE::Config::get_instance() {
	if (instance == nullptr) {
		std::lock_guard<std::mutex> lock(mtx);
		if (instance == nullptr) {
			instance = new Config();
		}
	}
	return instance;
}

KTREE::Args::Args(int argc, char **argv)
{
	this->argc = argc;
	this->argv = argv;
}


void KTREE::Args::parse(Config *config)
{
	static struct option long_options[] = {
		{"dataset", required_argument, 0, 'd'},
		{"queries", required_argument, 0, 'q'},
		{"index", required_argument, 0, 'i'},
		{"dataset_size", required_argument, 0, 'n'},
		{"queries_size", required_argument, 0, 'm'},
		{"dimensions", required_argument, 0, 'D'},
		{"leaf_size", required_argument, 0, 'l'},
		{"mode", required_argument, 0, 'x'},
		{"top_k", required_argument, 0, 'k'},
		{"help", no_argument, 0, '?'}
	};

	int option_index = 0;
	int tmp = 0;
	std::string mode = "";

	while (true) {
		int c = getopt_long(this->argc, argv, "", long_options, &option_index);
		if (c == -1) break;

		switch (c) {
			case 'd':
				config->dataset = optarg;
				break;
			case 'q':
				config->queries = optarg;
				break;
			case 'i':
				config->index_path = optarg;
				break;
			case 'n':
				tmp = atoi(optarg);
				if (tmp <= 0) {
					throw KTREE::InvalidArguments<unsigned int>("dataset_size", config->dataset_size);
				}
				config->dataset_size = tmp;
				break;
			case 'm':
				tmp = atoi(optarg);
				if (tmp <= 0) {
					throw KTREE::InvalidArguments<int>("queries_size", tmp);
				}
				config->queries_size = tmp;
				break;
			case 'D':
				tmp = atoi(optarg);
				if (tmp <= 0) {
					throw KTREE::InvalidArguments<int>("dimensions", tmp);
				}
				config->dimensions = tmp;
				break;
			case 'l':
				tmp = atoi(optarg);
				if (tmp <= 0) {
					throw KTREE::InvalidArguments<int>("leaf_size", tmp);
				}
				config->leaf_size = tmp;
				break;
			case 'x':
				mode = optarg;
				std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c){ return std::tolower(c); });
				if (mode == "index") {
					config->mode = INDEX;
				} else if (mode == "query") {
					config->mode = QUERY;
				} else {
					throw KTREE::InvalidArguments<std::string>("mode", mode);
				}
				break;
			case 'k':
				tmp = atoi(optarg);
				if (tmp <= 0) {
					throw KTREE::InvalidArguments<int>("top_k", tmp);
				}
				config->top_k = tmp;
				break;

			case '?':
				print_usage();
				exit(0);
				break;
			default:
				throw std::runtime_error("Invalid argument");
				print_usage();
				exit(1);
				break;
		}
	}
}

void KTREE::Config::print()
{
	std::cout << "dataset: " << dataset << std::endl;
	std::cout << "queries: " << queries << std::endl;
	std::cout << "index_path: " << index_path << std::endl;
	std::cout << "dataset_size: " << dataset_size << std::endl;
	std::cout << "queries_size: " << queries_size << std::endl;
	std::cout << "dimensions: " << dimensions << std::endl;
	std::cout << "leaf_size: " << leaf_size << std::endl;
	std::cout << "top_k: " << top_k << std::endl;
	if (mode == INDEX) {
		std::cout << "mode: index" << std::endl;
	} else {
		std::cout << "mode: query" << std::endl;
	}
}

void KTREE::Config::serialize(std::ofstream& out) const
{
	KTREE::serialize(dataset_size, out);
	KTREE::serialize(dimensions, out);
	KTREE::serialize(leaf_size, out);
	KTREE::serialize(top_k, out);

}

void KTREE::Config::deserialize(std::ifstream& in)
{
	// deserialize the numeric values
	KTREE::deserialize(dataset_size, in);
	KTREE::deserialize(dimensions, in);
	KTREE::deserialize(leaf_size, in);
	KTREE::deserialize(top_k, in);
	
}