#ifndef __DATA_HPP__
#define __DATA_HPP__

#include <vector>
#include <Eigen/Dense>

#include "segmentation.hpp"

namespace KTREE {



class DataPoint: public std::vector<float> {
public:
	DataPoint(std::vector<float> &data);
	~DataPoint();
	void print() const;
	std::vector<float> get_representation(const Segmentation& segmentation) const;
};


class DataContainer {
private:
	std::vector<DataPoint *> data;
public:
	DataContainer();
	~DataContainer();

	void append(DataPoint *point);
	// DataPoint* pop(size_t index);
	size_t size() const;
	void print() const;

	DataPoint* operator[] (size_t index) const;

	void variance(std::vector<float>& variance) const;

	void toEigenMatrix(Eigen::MatrixXf& matrix) const;
	DataPoint *remove(size_t index);

	void save_to_file(const std::string& filename) const;

	static DataContainer* load_from_file(
		const std::string& file_path,
		bool all = true,
		size_t num_points = 0
	);

	void clear();

};

};

#endif