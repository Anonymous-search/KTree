#include "segmentation.hpp"

#include <iostream>
#include "error.hpp"



namespace KTREE {

Segmentation::Segmentation() {}

Segmentation::Segmentation(const std::vector<size_t>& right_indices): right_indices(right_indices) {}

bool Segmentation::is_valid() const {
	if (right_indices.size() == 0) {
		return false;
	}
	if (right_indices.size() == 1) {
		return true;
	}
	size_t prev = right_indices[0];
	for (size_t i = 1; i < right_indices.size(); i++) {
		if (right_indices[i] <= prev) {
			return false;
		}
		prev = right_indices[i];
	}
	return true;
}

size_t Segmentation::size() const {
	return right_indices.size();
}

Segment Segmentation::operator[] (size_t index) const {
	size_t start, end;

	if (index >= right_indices.size()) {
		throw KTreeError("Invalid Segmentation index");
	}
	if (index == 0) {
		start = 0;
		end = right_indices[0];
	} else {
		start = right_indices[index - 1];
		end = right_indices[index];
	}
	return Segment(start, end);
}

void Segmentation::split_segment(size_t index) {
	Segment segment = (*this)[index];

	if (segment.size() <= 1) {
		throw KTreeError("Cannot split segment with size <= 1");
	}

	size_t mid = segment.get_start() + (segment.get_end() - segment.get_start()) / 2;
	right_indices.insert(right_indices.begin() + index, mid);
}

void Segmentation::get_segments_sizes(std::vector<size_t>& holder) const {
	holder.clear();
	for (size_t i = 0; i < this->size(); i++) {
		Segment segment = (*this)[i];
		holder.push_back(segment.size());
	}
}

void Segmentation::print() const {
	std::cout << "[";
	for (size_t i = 0; i < right_indices.size(); i++) {
		std::cout << right_indices[i] << ", ";
	}
	std::cout << "]" << std::endl;

	// print the segments
	for (size_t i = 0; i < right_indices.size(); i++) {
		Segment segment = (*this)[i];
		segment.print();
		std::cout << " ";
	}
	std::cout << std::endl;
}

void Segmentation::serialize(std::ofstream& out) const {
	KTREE::serialize(right_indices, out);
}

void Segmentation::deserialize(std::ifstream& in) {
	right_indices.clear();
	KTREE::deserialize(right_indices, in);
}

Segment::Segment(size_t start, size_t end): range(std::make_pair(start, end)) {}

size_t Segment::get_start() const {
	return range.first;
}

size_t Segment::get_end() const {
	return range.second;
}

std::vector<size_t> Segment::get_indices() const {
	std::vector<size_t> indices;
	for (size_t i = range.first; i < range.second; i++) {
		indices.push_back(i);
	}
	return indices;
}

size_t Segment::size() const {
	return range.second - range.first;
}

void Segment::print() const {
	std::cout << "(" << range.first << ", " << range.second << ")";
}

bool Segment::belongs(size_t index) const {
	return index >= range.first && index < range.second;
}

};