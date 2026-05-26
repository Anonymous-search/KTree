#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__

#include <string>
#include <map>
#include <mutex>

#include "serialization.hpp"

namespace KTREE {

enum Mode {
	INDEX = 0,
	QUERY = 1,
};



class Config: public Serializable {
public:
	std::string dataset;
	std::string queries;
	std::string index_path;
	unsigned int dataset_size;
	unsigned int queries_size;
	unsigned int dimensions;
	unsigned int leaf_size;
	size_t top_k;
	Mode mode;

	Config(const Config&) = delete;
	static Config *get_instance();

	void print();

	void serialize(std::ofstream& out) const override;
	void deserialize(std::ifstream& in) override;
	

private:
	~Config() = default;
	Config();

	static Config *instance;
	static std::mutex mtx;

};

class Args
{
public:
	int argc;
	char **argv;
	Args(int argc, char **argv);
	~Args() = default;
	void parse(Config *config);
};

};

#endif // __CONFIG_HPP__