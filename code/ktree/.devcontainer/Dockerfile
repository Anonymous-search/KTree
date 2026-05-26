FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
	autoconf \
	automake \
	build-essential \
	ca-certificates \
	cmake \
	doxygen \
	gdb \
	gfortran \
	git \
	libboost-all-dev \
	libeigen3-dev \
	libfftw3-dev \
	libgsl-dev \
	libjemalloc-dev \
	libreadline-dev \
	libtool \
	m4 \
	ninja-build \
	pkg-config \
	python3 \
	python3-dev \
	python3-matplotlib \
	python3-pip \
	python3-venv \
	&& rm -rf /var/lib/apt/lists/*

RUN mkdir -p /usr/local/include \
	&& ln -sfn /usr/include/eigen3/Eigen /usr/local/include/Eigen

USER vscode
WORKDIR /workspaces/Ktree
