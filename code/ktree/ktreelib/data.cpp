#include "data.hpp"

#include "utils.hpp"
#include "config.hpp"
#include "segmentation.hpp"
#include <iostream>
#include <fstream>


namespace KTREE {

DataContainer::DataContainer() {
}

DataPoint *DataContainer::remove(size_t index) {
	if (index >= data.size()) {
		return nullptr;
	}
	DataPoint *point = data[index];
	data[index] = nullptr;
	return point;
}

DataContainer::~DataContainer() {
	this->clear();
}

void DataContainer::clear() {
	for (auto point : data) {
		delete point;
	}
	data.clear();
}



void DataContainer::append(DataPoint *point) {
	data.push_back(point);
}

size_t DataContainer::size() const {
	return data.size();
}

void DataContainer::print() const {
	for (auto point : data) {
		if (point != nullptr)
			point->print();
	}
}

void DataContainer::variance(std::vector<float>& variance) const {
	KTREE::compute_variance(data, variance);
}

void DataContainer::toEigenMatrix(Eigen::MatrixXf& matrix) const {
	matrix = Eigen::MatrixXf(data.size(), data[0]->size());
	for (size_t i = 0; i < data.size(); i++) {
		for (size_t j = 0; j < data[0]->size(); j++) {
			matrix(i, j) = (*(data[i]))[j];
		}
	}
}

DataPoint* DataContainer::operator[] (size_t index) const {
	if (index >= data.size()) {
		return nullptr;
	}
	return data[index];
}


DataPoint::DataPoint(std::vector<float> &data) {
	for (auto d : data) {
		push_back(d);
	}
}

DataPoint::~DataPoint() {}

void DataPoint::print() const {
	for (auto d : *this) {
		std::cout << d << " ";
	}
	std::cout << std::endl;
}

std::vector<float> DataPoint::get_representation(const Segmentation& segmentation) const {
	// get the representation of the data point
	// based on the segmentation
	// the representation is the average of the data point
	// in each segment
	std::vector<size_t> segments_sizes;
	segmentation.get_segments_sizes(segments_sizes);
	size_t num_segments = segments_sizes.size();
	std::vector<float> representation(num_segments, 0.0);

	for (size_t i = 0; i < num_segments; i++) {
		Segment segment = segmentation[i];
		std::vector<size_t> indices = segment.get_indices();
		float sum = std::accumulate(indices.begin(), indices.end(), 0.0, [this](float sum, size_t index) {
			return sum + (*this)[index];
		});
		representation[i] = sum / indices.size();
	}
	return representation;
}

DataContainer* DataContainer::load_from_file(
	const std::string& file_path,
	bool all,
	size_t num_points
) {

	std::ifstream file(file_path, std::ios::in | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Could not open the data file for reading");
	}

	DataContainer *data = new DataContainer();

	// if all is true, read all the data points
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	if (all) {
		num_points = size / (KTREE::Config::get_instance()->dimensions * sizeof(float));
	} else {
		// check if the number of points is valid
		size_t expected_size = num_points * KTREE::Config::get_instance()->dimensions * sizeof(float);
		if (size < expected_size) {
			delete data;
			file.close();
			throw std::runtime_error("Invalid number of points in data file");
		}
	}

	file.seekg(0, std::ios::beg);
	for (size_t i = 0; i < num_points; i++) {
		std::vector<float> point;
		for (size_t j = 0; j < KTREE::Config::get_instance()->dimensions; j++) {
			float value;
			file.read(reinterpret_cast<char*>(&value), sizeof(float));
			point.push_back(value);
		}
		DataPoint *data_point = new DataPoint(point);
		data->append(data_point);
	}

	file.close();
	return data;
}

void DataContainer::save_to_file(const std::string& filename) const {
	// save the data to the file
	// inside the index directory
	std::string index_dir = KTREE::Config::get_instance()->index_path;

	std::string full_path = index_dir + "/" + filename;

	std::ofstream file(full_path, std::ios::out | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Could not open the file for writing");
	}

	for (auto point : data) {
		for (auto d : *point) {
			file.write(reinterpret_cast<const char*>(&d), sizeof(float));
		}
	}

}



};