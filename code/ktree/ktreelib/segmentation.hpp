#ifndef __SEGMENTATION_HPP__
#define __SEGMENTATION_HPP__

#include <vector>

#include "serialization.hpp"

namespace KTREE {
	class Segment {
private:
	std::pair<size_t, size_t> range;
public:
	Segment(size_t start, size_t end);
	size_t get_start() const;
	size_t get_end() const;
	std::vector<size_t> get_indices() const;
	size_t size() const;

	bool belongs(size_t index) const;

	void print() const;

};

class Segmentation: public Serializable {
private:
	std::vector<size_t> right_indices;
public:
	Segmentation();
	Segmentation(const std::vector<size_t>& right_indices);
	bool is_valid() const;
	size_t size() const;
	Segment operator[] (size_t index) const;
	void print() const;
	void get_segments_sizes(std::vector<size_t>& holder) const;
	void split_segment(size_t index);

	// serialization
	void serialize(std::ofstream& out) const override;
	void deserialize(std::ifstream& in) override;

};

};

#endif