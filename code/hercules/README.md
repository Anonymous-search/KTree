# 1. Paper Information

## Title: Karima Echihabi, Panagiota Fatourou, Kostas Zoumpatianos, Themis Palpanas, and Houda Benbrahim. Hercules Against Data Series Similarity Search. PVLDB, 15(10), 2022.

## Abstract: 

In this paper, we propose Hercules, a parallel tree-based technique for exact similarity search on massive disk-based data series collections. We present novel index construction and query answering algorithms that leverage different summarization techniques, carefully schedule costly operations, optimize memory and disk accesses, and exploit the multi-threading and SIMD capabilities of modern hardware to perform CPU-intensive calculations. We demonstrate the superiority and robustness of Hercules with an extensive experimental evaluation against the state-of-the-art techniques, using a variety of synthetic and real datasets, and query workloads of varying difficulty. The results show that Hercules performs up to one order of magnitude faster than the best competitor (which is not always the same). Moreover, Hercules is the only index that outperforms the optimized sequential scan on all scenarios, including the hard query workloads on disk-based datasets. 

## Paper Link: https://github.com/karimaechihabi/hercules/blob/main/paper/p1064-echihabi.pdf

### experiments
This folder contains detailed instructions on how to reproduce the experiments in the published paper.

   <u>bin</u>: executables required to run the experiments (can be used as a back-box). \
   <u>config</u>: configurations to add to the .bashrc file. \
   <u>scripts</u>: scripts used to automate experiments. \
   <u>workloads</u>: contains scripts to schedule experiments.

### paper
This folder contains the pdf of the Hercules manuscript. 



# Parametrization for the KTree experiments 
`project.config` hercules/experiments/config/ includs all the parameters used in the KTree paper

