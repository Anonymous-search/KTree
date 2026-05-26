#ifndef __QUERY_HPP__
#define __QUERY_HPP__

#include "data.hpp"

#include <Eigen/Dense>

namespace KTREE {

class DistanceMetric {
public:
	virtual float operator() (const DataPoint& a, const DataPoint& b) const = 0;
	virtual ~DistanceMetric() {}
};

class EuclideanDistance: public DistanceMetric {
public:
	float operator() (const DataPoint& a, const DataPoint& b) const override {

		// float sum = 0;
		// for (size_t i = 0; i < a.size(); i++) { 
		// 	sum += (a[i] - b[i]) * (a[i] - b[i]); 
		// } return sum;

		Eigen::Map<const Eigen::VectorXf> va(a.data(), a.size());
		Eigen::Map<const Eigen::VectorXf> vb(b.data(), b.size());

		return (va - vb).squaredNorm();
	}
};

template <typename T>
class ResultContainer;

template <typename T>
class DataPointComparator;


template<typename T = EuclideanDistance>
class Query {
private:
	DataPoint *query;
	ResultContainer<T> *results;
	size_t distance_computation;
	size_t visit_count;

public:
	Query(DataPoint *query): query(query), results(new ResultContainer<T>(*this)), distance_computation(0), visit_count(0) {}
	
	~Query() {
		delete results;
	}

	void set_query(DataPoint *query) {
		this->clear();
		this->query = query;
		results->set_query(*this);
	}
	
	DataPoint& get_query() const {
		return *query;
	}

	void add_result(DataPoint *point) {
		results->insert(point);
	}
	
	size_t get_distance_computation() const {
		return distance_computation;
	}
	
	size_t get_visit_count() const {
		return visit_count;
	}

	DataPoint* best_result() const {
		return results->best_result();
	}
	
	void increment_distance_computation() {
		distance_computation++;
	}
	void increment_visit_count() {
		visit_count++;
	}

	const ResultContainer <T>* get_results() const {
		return results;
	}

	void clear() {
		delete query;
		query = nullptr;
		results->clear();
		distance_computation = 0;
		visit_count = 0;
	}
};



template <typename T = EuclideanDistance>
class DataPointComparator {
private:
	T metric;
	Query<T> &query;
public:
	DataPointComparator(Query<T>& query): query(query) {}
	DataPointComparator(const DataPointComparator& other): metric(other.metric), query(other.query) {}
	DataPointComparator& operator=(const DataPointComparator& other) {
		if (this != &other) {
			this->metric = other.metric;
			this->query = other.query;
		}
		return *this;
	}
	bool operator() (const DataPoint *a, const DataPoint *b) {
		query.increment_distance_computation();
		return metric(*a, query.get_query()) < metric(*b, query.get_query());
	}

	const T& get_metric() const {
		return metric;
	}
};

template <typename T = EuclideanDistance>
class ResultContainer {
private:
	DataPointComparator<T> comparator;
	std::vector<DataPoint *> results;
	Query<T>& query;
	size_t k;
public:
	ResultContainer(Query<T>& query, size_t k = 1): comparator(query), query(query), k(k) {}

	void insert(DataPoint *point) {
		if (point == nullptr) {
			return;
		}
		results.push_back(point);
		std::sort(results.begin(), results.end(), comparator);
		if (results.size() > k) {
			results.pop_back();	
		}
	}
	DataPoint* best_result() const {
		if (results.empty()) {
			return nullptr;
		}
		return results[0];
	}

	void clear() {
		results.clear();
	}

	void set_query(Query<T>& query) {
		this->query = query;
		this->comparator = DataPointComparator<T>(query);
		this->clear();

	}
	size_t size() const {
		return results.size();
	}

	const DataPointComparator<T>& get_comparator() const {
		return comparator;
	}
};



};
#endif