#ifndef __INDEX__HPP__
#define __INDEX__HPP__

#include <vector>
#include <utility>
#include <Eigen/Dense>

#include "config.hpp"
#include "ktree.hpp"
#include "data.hpp"

#include "serialization.hpp"


namespace KTREE {

class Index: public Serializable {
private:
	KTree *ktree;

public:
	Index();
	~Index();

	void build();
	void save();
	void load(); 
	void search();
	void serialize(std::ofstream& out) const override;
	void deserialize(std::ifstream& in) override;
};

}

#endif // __INDEX__HPP__