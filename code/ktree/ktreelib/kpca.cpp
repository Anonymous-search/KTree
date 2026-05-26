#include "kpca.hpp"


#include <iostream>

namespace PCA {

RandomFourierFeatures::RandomFourierFeatures(int n_features, float gamma) : n_features(n_features), gamma(gamma) {
	std::random_device rd;
	gen_ = std::mt19937(rd());
	normal_dist_ = std::normal_distribution<float>(0.0, 1.0);
	uniform_dist_ = std::uniform_real_distribution<float>(0.0, 2 * M_PI);
}

void RandomFourierFeatures::transform(
	const Eigen::MatrixXf &data,
	Eigen::MatrixXf &Z,
	Eigen::MatrixXf &W,
	Eigen::MatrixXf &b
) {
	int n_samples = data.rows();
	int n_original_features = data.cols();

	// initialize random weights and bias
	W = Eigen::MatrixXf(n_original_features, n_features);
	b = Eigen::MatrixXf(1, n_features);

	// fill W with random normal values and scale by sqrt(2 * gamma)
	for (int i = 0; i < n_original_features; i++) {
		for (int j = 0; j < n_features; j++) {
			W(i, j) = normal_dist_(gen_) * std::sqrt(2.0f * gamma);
		}
	}

	// fill b with random values from uniform distribution in [0, 2 * pi]
	for (int i = 0; i < n_features; i++) {
		b(0, i) = uniform_dist_(gen_);
	}

	// compute the random features matrix Z
	Z = Eigen::MatrixXf(n_samples, n_features);

	for (int i = 0; i < n_samples; i++) {
		Eigen::VectorXf x = data.row(i);
		for (int j = 0; j < n_features; j++) {
			Z(i, j) = std::sqrt(2.0f / n_features) * std::cos(W.col(j).transpose() * x + b(0, j));
		}
	}
}

void performPCA(
	const Eigen::MatrixXf &transformed_data,
	Eigen::MatrixXf &components,
	Eigen::MatrixXf &projected_data,
	int n_components
) {//BDCSVD here not Jacobi
	Eigen::JacobiSVD<Eigen::MatrixXf> svd(transformed_data, Eigen::ComputeThinU | Eigen::ComputeThinV);

	Eigen::MatrixXf U_reduced = svd.matrixU().leftCols(n_components);

	components = svd.matrixV().leftCols(n_components);
	components.transposeInPlace();

	projected_data = U_reduced * svd.singularValues().head(n_components).asDiagonal();
}

void project(
	const Eigen::MatrixXf &data,
	const Eigen::MatrixXf &W,
	const Eigen::MatrixXf &b,
	const Eigen::MatrixXf &components,
	float &projected_value
) {
	int n_samples = data.rows();
	int n_features = W.cols();

	Eigen::MatrixXf Z(n_samples, n_features);

	for (int i = 0; i < n_samples; i++) {
		Eigen::VectorXf x = data.row(i);
		for (int j = 0; j < n_features; j++) {
			Z(i, j) = std::sqrt(2.0f / n_features) * std::cos(W.col(j).transpose() * x + b(0, j));
		}
	}

	Eigen::MatrixXf projected_data = Z * components.transpose();

	projected_value = projected_data(0, 0);
}

};

