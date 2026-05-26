#include <iostream>

#include "KTreeConfig.h"

#include "ktreelib.hpp"


KTREE::Config *KTREE::Config::instance = nullptr;
std::mutex KTREE::Config::mtx;




int main(int argc, char **argv) {
	KTREE::Config *config = KTREE::Config::get_instance();
	KTREE::Args args(argc, argv);
	try {
		args.parse(config);
	} catch (std::exception &e) {
		std::cerr << e.what() << std::endl;

		exit(1);
	}


	KTREE::Index index;


	try {
		switch (config->mode) {
			case KTREE::Mode::INDEX:
				index.build();
				index.save();
				break;
			case KTREE::Mode::QUERY:
				index.load();
				index.search();
				break;
		}
	} catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
	}


	return 0;
}
