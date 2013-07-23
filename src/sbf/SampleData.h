#ifndef SAMPLE_DATA_H
#define SAMPLE_DATA_H

#ifdef __CUDACC__
#define CUDA_PREFIX __host__ __device__
#else
#define CUDA_PREFIX
#endif
struct SampleData {

	CUDA_PREFIX void reset() {
		for (int i = 0; i < 3; i++) {
			rgb[i] = normal[i] = secondNormal[i] = rho[i] = secondOrigin[i] =
					thirdOrigin[i] = inputColors[i] = outputColors[i] =
					firstReflectionDir[i] = 0.f;
		}
		for (int i = 0; i < 2; i++) {
			lensPos[i] = imgPos[i]= 0.f;
		}
		time = 0.f;
		x = y = 0;
	}

	// position features (the first 6 values)
	float secondOrigin[3];  //0, 1, 2
	float thirdOrigin[3];	//3, 4, 5
	//features
	float normal[3];		//6, 7, 8
	float secondNormal[3];	//9, 10, 11
	float rho[3];			//12, 13,14

	float rgb[3];			//15, 16, 17
	float imgPos[2];		//18, 19

	//random parameters
	float firstReflectionDir[3]; //	20, 21, 22
	float lensPos[2];		//		23, 24
	float time;				//		25

	// input/output colors:
	float inputColors[3];	//whatev
	float outputColors[3];
	int x, y;
	// Some handy accessor methods, thx @jklethinen
	// asserts that float and int are the same length
	CUDA_PREFIX static int getSize()					{ return sizeof(SampleData)/sizeof(float); }
	CUDA_PREFIX static int getFeaturesOffset() 			{ return 0; }
	CUDA_PREFIX static int getFeaturesSize()			{ return 15; }
	CUDA_PREFIX static int getColorOffset()				{ return 15; }
	CUDA_PREFIX static int getColorSize()				{ return 3; }
	CUDA_PREFIX static int getImgPosOffset()			{ return 18; }
	CUDA_PREFIX static int getImgPosSize()				{ return 2; }
	CUDA_PREFIX static int getRandomParamsOffset()		{ return 20; }
	CUDA_PREFIX static int getRandomParametersSize()	{ return 5; }
	CUDA_PREFIX static int getLastNormalizedOffset()	{ return 25; } //this should mark the last used parameter
	CUDA_PREFIX void   operator+=(const SampleData& s)	{ for(int i=0;i<getSize();i++) (*this)[i] += s[i]; }
	CUDA_PREFIX void   divide(int s)					{ for(int i=0;i<getSize();i++) (*this)[i] /= float(s); }
	CUDA_PREFIX float& operator[](int i)				{ return ((float*)this)[i]; }
	CUDA_PREFIX const float& operator[](int i) const	{ return ((float*)this)[i]; }
	CUDA_PREFIX float sum() const						{ float s=0; for(int i=0;i<getSize();i++) s+=(*this)[i]; return s; }
	CUDA_PREFIX float avg() const						{ return sum()/getSize(); }

	/**
	 * For ordering in sbf.h
	 */
	CUDA_PREFIX bool operator <(const SampleData& other) const {
		if (y == other.y)
			return x < other.x;
		else return y < other.y;
	}

	CUDA_PREFIX SampleData operator +(const SampleData& other) const {
			SampleData s;
			for (int f=0; f < getLastNormalizedOffset(); f++)
				s[f] = (*this)[f] + other[f];
			return s;
		}
	CUDA_PREFIX SampleData operator *(const SampleData& other) const {
				SampleData s;
				for (int f=0; f < getLastNormalizedOffset(); f++)
					s[f] = (*this)[f] * other[f];
				return s;
			}

};

#endif //SAMPLE_DATA_H
