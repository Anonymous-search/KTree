# 1. Paper Information

## Title: Khaoula Abdenouri, Karima Echihabi. KTree: a Kernel-Based Index for Scalable Exact Similarity Search

## Abstract: 

We propose KTree, a tree-based index for exact vector similarity search. We present novel indexing and search algorithms that leverage a new tight lower-bounding distance, a rich class of kernel-based splitting strategies, and a data-adaptive variance-guided segmentation that
finds the best dimensions to join into segments regardless of their original order. We prove theoretically the correctness of the lower-bounding distance and demonstrate empirically KTree’s superiority with exhaustive experiments against popular baselines, using query workloads of varying difficulty and four real datasets, including two out-of-core datasets containing 1 billion dense vector embeddings. The results show that KTree outperforms the second-best competitor (which is not always the same) by up to 2.07x in query efficiency, 2.75x in pruning and 42% in the tightness of the lower bound. A thorough ablation study shows that each design choice contributes meaningfully to KTree’s overall performance.

# 2.1. Reproducibilty
Details about reproducibility of each baseline including KTree are available in scripst/config.json

## 2.2. Archive
This archive contains detailed information required to reproduce the experimental results of the above paper.

The archive contains the following 4 folders:

### code
This folder contains the code of the KTress index and all the baselines compared against.

### data-queries
The data-queries subdirectory contains links to the datasets and examples of the workloads used in the above paper.

### paper
This folder contains the pdf of the KTree manuscript and it's extended version. The extended version includes suplemental material. 

### scripts
This folder contains parameterization for KTree and the baselines to reproduce experiments of this paper

## 2.3. Hardware Requirements
No particular requirement other than a machine equipped with an HDD having at least 75GB of RAM. The experiments in the paper were run using GCC 6.2.0 under Ubuntu Linux 16.04.2 with default compilation flags. Experiments were conducted on a server equipped with two Intel(R) Xeon(R) Gold 6234 CPUs running at 3.30GHz, 125GB of RAM, and 135GB of swap space. The RAM was limited to 80GB3. Storage was pro- vided by a 3.2TB (2 × 1.6TB) SATA2 SSD array configured in RAID0, delivering a measured throughput of 330 MB/sec.
We used GRUB to limit the amount of RAM, restrict its size using the grub as follows:

sudo gedit  /etc/default/grub  \
add or update the GRUB_CMDLINE_LINUX_DEFAULT variable as GRUB_CMDLINE_LINUX_DEFAULT="quiet splash mem=75G" \
sudo update-grub \
sudo reboot 

 
