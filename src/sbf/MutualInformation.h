/*
 * MutualInfo.h
 *
 *  Created on: Jul 20, 2013
 *      Author: moser
 */

#ifndef MUTUALINFO_H_
#define MUTUALINFO_H_
#include "SampleData.h"
#include <vector>
/**
 * Again heavily inspired by jklethinens code.
 * You must make an instance of this (instead of static), so memory for histograms only needs to be assigned once.
 */
#define NR_BUCKETS 5
class MutualInformation {
public:
	float mutualinfo(const vector<SampleData> &neighbourhood, const int firstChannel, const int secondChannel) {
		clearHistograms();
		for (int i=0; i < neighbourhood.size(); i++) {
			const SampleData &s = neighbourhood[i];
			int a = quantize(s[firstChannel]);
			int b = quantize(s[secondChannel]);
			hist_a[a]++;
			hist_b[b]++;
			hist_ab[a*NR_BUCKETS+b]++;
		}
		//compute entropies
		float ent_a = 0.f;
		float ent_b = 0.f;
		for (int i = 0; i < NR_BUCKETS; i++) {
			if(hist_a[i]) {
				float prob_a = hist_a[i]/neighbourhood.size();
				ent_a += -prob_a*log2f(prob_a);
			}
			if(hist_b[i]) {
				float prob_b = hist_b[i]/neighbourhood.size();
				ent_b += -prob_b*log2f(prob_b);
			}
		}
		float ent_ab = 0.f;
		for (int i = 0; i < NR_BUCKETS*NR_BUCKETS; i++) {
			if(hist_ab[i]) {
				float prob_ab = hist_ab[i]/neighbourhood.size();
				ent_ab += -prob_ab*log2f(prob_ab);
			}
		}
		return ent_a + ent_b - ent_ab;
	}

private:
	void clearHistograms() {
		for (int i = 0; i < NR_BUCKETS; i++) {
			hist_a[i] = hist_b[i] = 0.f;
		}
		for (int i = 0; i < NR_BUCKETS*NR_BUCKETS; i++) {
			hist_ab[i] = 0.f;
		}
	}
	float hist_a[NR_BUCKETS];
	float hist_b[NR_BUCKETS];
	float hist_ab[NR_BUCKETS*NR_BUCKETS];

	inline int quantize(float v) {
		v = (v+2)/4;
		v *= NR_BUCKETS-1;
		int bucket = (int)(v + 0.5f);
		return min(max(bucket, 0), NR_BUCKETS - 1);
	}
};

#endif /* MUTUALINFO_H_ */
