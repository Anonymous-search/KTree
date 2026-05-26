#ifndef __KPCA_HPP__
#define __KPCA_HPP__

#include <Eigen/Dense>
#include <vector>
#include <random>
#include <cmath>

namespace PCA {

class RandomFourierFeatures {
private:
	int n_features;
	float gamma;
	std::mt19937 gen_;
	std::normal_distribution<float> normal_dist_;
	std::uniform_real_distribution<float> uniform_dist_;

public:
	RandomFourierFeatures(int n_features, float gamma);
	void transform(
		const Eigen::MatrixXf &data,
		Eigen::MatrixXf &transformed_data,
		Eigen::MatrixXf &Wr,
		Eigen::MatrixXf &br
	);
};


void performPCA(
	const Eigen::MatrixXf &transformed_data,
	Eigen::MatrixXf &components,
	Eigen::MatrixXf &projected_data,
	int n_components
);

void project(
	const Eigen::MatrixXf &data,
	const Eigen::MatrixXf &W,
	const Eigen::MatrixXf &b,
	const Eigen::MatrixXf &components,
	float &projected_value
);

};
#endif // __KPCA_HPP__