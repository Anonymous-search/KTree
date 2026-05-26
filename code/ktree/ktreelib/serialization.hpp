#ifndef __SERIALIZATION_HPP__
#define __SERIALIZATION_HPP__

#include <fstream>
#include <iostream>
#include <cstring>
#include <Eigen/Dense>

namespace KTREE {

class Serializable {
public:
	virtual ~Serializable() = default;

	virtual void serialize(std::ofstream& out) const = 0;
	virtual void deserialize(std::ifstream& in) = 0;
	
};

template <typename T>
void serialize(const T& data, std::ofstream& out) {
	out.write(reinterpret_cast<const char*>(&data), sizeof(T));
}

template <typename T>
void serialize(const std::vector<T>& data, std::ofstream& out) {
	size_t size = data.size();
	out.write(reinterpret_cast<const char*>(&size), sizeof(size_t));
	for (size_t i = 0; i < size; i++) {
		serialize(data[i], out);
	}
}

template <> inline
void serialize(const Eigen::MatrixXf& matrix, std::ofstream& out) {
	float value;

	size_t rows = matrix.rows();
	size_t cols = matrix.cols();
	serialize(rows, out);
	serialize(cols, out);
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			serialize(matrix(i, j), out);
		}
	}
}

template <> inline
void serialize(const std::string& data, std::ofstream& out) {
	size_t size = data.size();
	out.write(reinterpret_cast<const char*>(&size), sizeof(size_t));
	out.write(data.c_str(), size);
}

template <typename T>
void deserialize(T& data, std::ifstream& in) {
	in.read(reinterpret_cast<char*>(&data), sizeof(T));
}

template <> inline
void deserialize(Eigen::MatrixXf& matrix, std::ifstream& in) {
	float value;
	size_t rows, cols;

	deserialize(rows, in);
	deserialize(cols, in);
	matrix = Eigen::MatrixXf(rows, cols);
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			deserialize(value, in);
			matrix(i, j) = value;
		}
	}
}

template <typename T>
void deserialize(std::vector<T>& data, std::ifstream& in) {
	size_t size;
	in.read(reinterpret_cast<char*>(&size), sizeof(size_t));
	data = std::vector<T>(size);
	for (size_t i = 0; i < size; i++) {
		deserialize(data[i], in);
	}
}

template <> inline
void deserialize(std::string& data, std::ifstream& in) {
	size_t size;
	in.read(reinterpret_cast<char*>(&size), sizeof(size_t));
	
	char *buffer = new char[size + 1];
	memset(buffer, 0, size + 1);
	in.read(buffer, size);
	data = std::string(buffer);
}

};
#endif