
/*
    Copyright(c) 2012-2013 Tzu-Mao Li
    All rights reserved.

    The code is based on PBRT: http://www.pbrt.org

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

#include "sbf.h"

#include "sampler.h"
#include "spectrum.h"
#include "intersection.h"
#include "imageio.h"
#include "montecarlo.h"
#include "progressreporter.h"
#include "CrossBilateralFilter.h"
#include "CrossNLMFilter.h"
#include "fmath.hpp"

#include <limits>
#include <algorithm>
#include <omp.h>
#include <cmath>

// Range sigma for bilateral filter, we found that with range term the result will be noisy,
// so we set the sigma to infinite to drop the range term(0 indicates infinite in our implementation)
const float c_SigmaC = 0.f;

SBF::SBF(int xs, int ys, int w, int h, 
         const Filter *filt, FilterType type,
         const vector<float> &_interParams,
         const vector<float> &_finalParams,
         float _sigmaN, float _sigmaR, float _sigmaD,
         float _interMseSigma, float _finalMseSigma) :
    fType(type), rFilter(filt),  
    interParams(_interParams), finalParams(_finalParams),
    sigmaN(_sigmaN), sigmaR(_sigmaR), sigmaD(_sigmaD),
    interMseSigma(_interMseSigma), finalMseSigma(_finalMseSigma) {
    xPixelStart = xs;
    yPixelStart = ys;
    xPixelCount = w;
    yPixelCount = h;
    pixelInfos = new BlockedArray<PixelInfo>(xPixelCount, yPixelCount);
    // TODO replace 8 with spp
    allSamples = vector<SampleData> (xPixelCount*yPixelCount*8);
    colImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    varImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    featureImg = TwoDArray<Feature>(xPixelCount, yPixelCount);
    featureVarImg = TwoDArray<Feature>(xPixelCount, yPixelCount);

    norImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    rhoImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    depthImg = TwoDArray<float>(xPixelCount, yPixelCount);
    rhoVarImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    norVarImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    depthVarImg = TwoDArray<float>(xPixelCount, yPixelCount);
    
    fltImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    minMseImg = TwoDArray<float>(xPixelCount, yPixelCount);
    adaptImg = TwoDArray<float>(xPixelCount, yPixelCount);
    sigmaImg = TwoDArray<Color>(xPixelCount, yPixelCount);

    //new:
    dirImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    lensImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    timeImg = TwoDArray<float>(xPixelCount, yPixelCount);
    sampleCount = 0;
}

void SBF::AddSample(const CameraSample &sample, const Spectrum &L, 
                    const Intersection &isect) {    
    int x = Floor2Int(sample.imageX)-xPixelStart;
    int y = Floor2Int(sample.imageY)-yPixelStart;
    // Check if the sample is in the image
    if (x < 0 || y < 0 || x >= xPixelCount || y >= yPixelCount) 
        return;    

    // Update PixelInfo structure
    PixelInfo &pixelInfo = (*pixelInfos)(x, y);

    // Convert to 3d color space from Spectrum
    float xyz[3];
    L.ToRGB(xyz);
    float rhoXYZ[3];
    isect.rho.ToRGB(rhoXYZ);


    // TODO: does AtomicAdd really needed?
    //TODO remove atomic Add
    for(int i = 0; i < 3; i++) {
        AtomicAdd(&(pixelInfo.Lxyz[i]), xyz[i]);        
        AtomicAdd(&(pixelInfo.sqLxyz[i]), xyz[i]*xyz[i]);
        AtomicAdd(&(pixelInfo.rho[i]), rhoXYZ[i]);
        AtomicAdd(&(pixelInfo.sqRho[i]), rhoXYZ[i]*rhoXYZ[i]);
        //new
        AtomicAdd(&(pixelInfo.dir[i]), isect.dir[i]);
        // Sometimes pbrt returns NaN normals, we simply ignore them here
        if(!isect.shadingN.HasNaNs()) {
            AtomicAdd(&(pixelInfo.normal[i]), isect.shadingN[i]);            
            AtomicAdd(&(pixelInfo.sqNormal[i]), isect.shadingN[i]*isect.shadingN[i]);
        }
    }
    AtomicAdd(&(pixelInfo.lensPos[0]), sample.lensU); //should this save a copy in the array?
    AtomicAdd(&(pixelInfo.lensPos[1]), sample.lensV);
    AtomicAdd(&(pixelInfo.time), sample.time);

    AtomicAdd(&(pixelInfo.depth), isect.depth);
    AtomicAdd(&(pixelInfo.sqDepth), isect.depth*isect.depth);
    AtomicAdd((AtomicInt32*)&(pixelInfo.sampleCount), (int32_t)1);
    //TODO: does this work for multiple threads??
    SampleData sd = allSamples[sampleCount++];
    for(int i = 0; i < 3; i++) {
    	sd.rgb[i] = xyz[i];
    	sd.rho[i] = rhoXYZ[i];
    	sd.normal[i] = isect.shadingN[i];
    	sd.secondOrigin[i] = isect.secondOrigin[i];
    	sd.thirdOrigin[i] = isect.thirdOrigin[i];
    }
    sd.imgPos[0] = sample.imageX;
    sd.imgPos[1] = sample.imageY;
    sd.lensPos[0] = sample.lensU;
    sd.lensPos[1] = sample.lensV;
	sd.time = sample.time;
}

void SBF::GetAdaptPixels(int spp, vector<vector<int> > &pixels) {
    Update(false);
    
    // We use long long here since int will overflow for very extreme case
    // (e.g. for a very big image with size 2560x1920, unsigned int can only afford 873 spp)
    long long totalSamples = (long long)xPixelCount*
                             (long long)yPixelCount*
                             (long long)spp;

    long double probSum = 0.0L;
    for(int y = 0; y < yPixelCount; y++)
        for(int x = 0; x < xPixelCount; x++) {
            probSum += (long double)adaptImg(x, y);
        }
    long double invProbSum = 1.0L/probSum;

    // Clear pixels
    vector<vector<int> >().swap(pixels);

    pixels.resize(yPixelCount);
    for(int y = 0; y < yPixelCount; y++) {
        pixels[y].resize(xPixelCount);
        for(int x = 0; x < xPixelCount; x++) {
            pixels[y][x] = 
                max(Ceil2Int((long double)totalSamples * 
                             (long double)adaptImg(x, y) * invProbSum), 1);
        }
    }
}

float SBF::CalculateAvgSpp() const {
    unsigned long long totalSamples = 0;
    for(int y = 0; y < yPixelCount; y++)
        for(int x = 0; x < xPixelCount; x++) {
            totalSamples += (unsigned long long)(*pixelInfos)(x, y).sampleCount;
        }
    long double avgSpp = (long double)totalSamples/(long double)(xPixelCount*yPixelCount);
    return (float)avgSpp;
}

void SBF::WriteImage(const string &filename, int xres, int yres, bool dump) {
    Update(true);

    string filenameBase = filename.substr(0, filename.rfind("."));
    string filenameExt  = filename.substr(filename.rfind("."));

    printf("Avg spp: %.2f\n", CalculateAvgSpp());

    WriteImage(filenameBase+"_sbf_img"+filenameExt, colImg, xres, yres);
    WriteImage(filenameBase+"_sbf_flt"+filenameExt, fltImg, xres, yres);
    TwoDArray<Color> sImg = TwoDArray<Color>(xPixelCount, yPixelCount);
    for(int y = 0; y < yPixelCount; y++)
        for(int x = 0; x < xPixelCount; x++) {
            float sc = (float)(*pixelInfos)(x, y).sampleCount;
            sImg(x, y) = Color(sc, sc, sc);
        }        
    WriteImage(filenameBase+"_sbf_smp"+filenameExt, sImg, xres, yres);
    WriteImage(filenameBase+"_sbf_param"+filenameExt, sigmaImg, xres, yres);

    if(dump) { // Write debug images
        WriteImage(filenameBase+"_sbf_var"+filenameExt, varImg, xres, yres);
        
        // Normals contain negative values, normalize them here
        for(int y = 0; y < norImg.GetRowNum(); y++)
            for(int x = 0; x < norImg.GetColNum(); x++) {
                norImg(x, y) += Color(1.f, 1.f, 1.f);
                norImg(x, y) /= 2.f;
            }
        WriteImage(filenameBase+"_sbf_nor"+filenameExt, norImg, xres, yres);

        WriteImage(filenameBase+"_sbf_nor_var"+filenameExt, norVarImg, xres, yres);
        WriteImage(filenameBase+"_sbf_rho"+filenameExt, rhoImg, xres, yres);
        WriteImage(filenameBase+"_sbf_rho_var"+filenameExt, rhoVarImg, xres, yres);

        TwoDArray<Color> depthColImg = FloatImageToColor(depthImg);
        TwoDArray<Color> dvColImg = FloatImageToColor(depthVarImg);
        WriteImage(filenameBase+"_sbf_dep"+filenameExt, depthColImg, xres, yres);
        WriteImage(filenameBase+"_sbf_dep_var"+filenameExt, dvColImg, xres, yres);

        //new:
        WriteImage(filenameBase+"_sbf_dir"+filenameExt, dirImg, xres, yres);
        WriteImage(filenameBase+"_sbf_lens"+filenameExt, lensImg, xres, yres);
        TwoDArray<Color> timeColImg = FloatImageToColor(timeImg);
        WriteImage(filenameBase+"_sbf_time"+filenameExt, timeColImg, xres, yres);
    }
}

void SBF::WriteImage(const string &filename, const TwoDArray<Color> &image, int xres, int yres) const {
    ::WriteImage(filename, (float*)image.GetRawPtr(), NULL, xPixelCount, yPixelCount,
                 xres, yres, xPixelStart, yPixelStart);
}

TwoDArray<Color> SBF::FloatImageToColor(const TwoDArray<float> &image) const {
    TwoDArray<Color> colorImg(image.GetColNum(), image.GetRowNum());
    for(int y = 0; y < yPixelCount; y++)
        for(int x = 0; x < xPixelCount; x++) {
            float val = image(x, y);
            colorImg(x, y) = Color(val, val, val);
        } 
    return colorImg;
}

void SBF::Update(bool final) {
    ProgressReporter reporter(1, "Updating");

#pragma omp parallel for num_threads(PbrtOptions.nCores)
    for(int y = 0; y < yPixelCount; y++)
        for(int x = 0;x < xPixelCount; x++) {
            PixelInfo &pixelInfo = (*pixelInfos)(x, y);
            float invSampleCount = 1.f/(float)pixelInfo.sampleCount;
            float invSampleCount_1 = 1.f/((float)pixelInfo.sampleCount-1.f);
            Color colSum = Color(pixelInfo.Lxyz); //color in RGB
            Color sqColSum = Color(pixelInfo.sqLxyz); //square of the color?
            Color colMean = colSum*invSampleCount;
            Color colVar = (sqColSum - colSum*colMean) * 
                           invSampleCount_1 * invSampleCount;

            Color norSum = Color(pixelInfo.normal);
            Color sqNorSum = Color(pixelInfo.sqNormal);
            Color norMean = norSum*invSampleCount;
            Color norVar = (sqNorSum - norSum*norMean) *
                           invSampleCount_1;
            
            Color rhoSum = Color(pixelInfo.rho);
            Color sqRhoSum = Color(pixelInfo.sqRho);
            Color rhoMean = rhoSum*invSampleCount;
            Color rhoVar = (sqRhoSum - rhoSum*rhoMean) *
                           invSampleCount_1;
            //new
            Color dirSum = Color(pixelInfo.dir);
            Color dirMean = dirSum*invSampleCount;
            
            Color lensSum = Color(pixelInfo.lensPos[0], pixelInfo.lensPos[1], 0.f);
            //Color lensSum = Color(0.f, 0.f, 0.f);
            Color lensMean = lensSum*invSampleCount;

            float depthSum = pixelInfo.depth;
            float sqDepthSum = pixelInfo.sqDepth;
            float depthMean = depthSum * invSampleCount;
            float depthVar = (sqDepthSum - depthSum*depthMean) *
                             invSampleCount_1;
            
            colImg(x, y) = colMean;
            varImg(x, y) = colVar;
            norImg(x, y) = norMean;
            norVarImg(x, y) = norVar;
            rhoImg(x, y) = rhoMean;            
            rhoVarImg(x, y) = rhoVar;
            depthImg(x, y) = depthMean;
            depthVarImg(x, y) = depthVar;
            //new
            dirImg(x, y) = dirMean;
            lensImg(x, y) = lensMean;

            Feature feature, featureVar;
            feature[0] = norMean[0];
            feature[1] = norMean[1];
            feature[2] = norMean[2];
            feature[3] = rhoMean[0];
            feature[4] = rhoMean[1];
            feature[5] = rhoMean[2];
            feature[6] = depthMean;
            featureVar[0] = norVar[0];
            featureVar[1] = norVar[1];
            featureVar[2] = norVar[2];
            featureVar[3] = rhoVar[0];
            featureVar[4] = rhoVar[1];
            featureVar[5] = rhoVar[2];
            featureVar[6] = depthVar;
            
            featureImg(x, y) = feature;
            featureVarImg(x, y) = featureVar;
        }

    TwoDArray<Color> rColImg = colImg;
    /**
     *  We use the image filtered by the reconstruction filter for MSE estimation,
     *  but apply filtering on the 1x1 box filtered image.
     *  We found that this gives sharper result and smoother filter selection
     */
    rFilter.Apply(rColImg);
    /**
     *  Theoratically, we should use squared kernel to filter variance,
     *  however we found that it will produce undersmoothed image(this is
     *  because we assumed Gaussian white noise when performing MSE 
     *  estimation, so we did not consider the covariances between pixels)
     *  Therefore we reconstruct the variance with the original filter.      
     */
    rFilter.Apply(varImg);

    // We reconstruct feature buffers with 1x1 box filter as it gives us sharper result
    // In the case that the feature buffers are very noisy like heavy DOF or very fast
    // motion, it might be a good idea to filter the feature buffer. But as the variance
    // will be very local, we will have to apply some adaptive filters.

    vector<float> sigma = final ? finalParams : interParams;
    Feature sigmaF;
    sigmaF[0] = sigmaF[1] = sigmaF[2] = sigmaN;
    sigmaF[3] = sigmaF[4] = sigmaF[5] = sigmaR;
    sigmaF[6] = sigmaD;

    vector<TwoDArray<Color> > fltArray;
    vector<TwoDArray<float> > mseArray;
    vector<TwoDArray<float> > fltMseArray;
    for(size_t i = 0; i < sigma.size(); i++) {
        fltArray.push_back(TwoDArray<Color>(xPixelCount, yPixelCount));
        mseArray.push_back(TwoDArray<float>(xPixelCount, yPixelCount));
        fltMseArray.push_back(TwoDArray<float>(xPixelCount, yPixelCount));
    }

    if(fType == CROSS_BILATERAL_FILTER) {
        for(size_t i = 0; i < sigma.size(); i++) {
            CrossBilateralFilter cbFilter(sigma[i], c_SigmaC, sigmaF, xPixelCount, yPixelCount); 
            TwoDArray<Color> flt(xPixelCount, yPixelCount);
            TwoDArray<float> mse(xPixelCount, yPixelCount);
            cbFilter.Apply(colImg, featureImg, featureVarImg, rColImg, varImg, flt, mse);
            mseArray[i] = mse;
            fltArray[i] = flt;
        }    

        CrossBilateralFilter mseFilter(final ? finalMseSigma : interMseSigma, 0.f, 
                                       sigmaF, xPixelCount, yPixelCount); 
        mseFilter.ApplyMSE(mseArray, featureImg, featureVarImg, fltMseArray);
    } else { //fType == CROSS_NLM_FILTER
        CrossNLMFilter nlmFilter(final ? 20 : 10, 2, sigma, sigmaF, 
                xPixelCount, yPixelCount);
        nlmFilter.Apply(colImg, featureImg, featureVarImg, 
                rColImg, varImg, fltArray, mseArray);
        // We use cross bilateral filter to filter MSE estimation even for NLM filters.
        CrossBilateralFilter mseFilter(final ? finalMseSigma : interMseSigma, 0.f, 
                                       sigmaF, xPixelCount, yPixelCount);   
        mseFilter.ApplyMSE(mseArray, featureImg, featureVarImg, fltMseArray);
        //filter.ApplyMSE(0.04f, mseArray, rColImg, featureImg, featureVarImg, fltMseArray);
    }

    minMseImg = numeric_limits<float>::infinity();   
    for(size_t i = 0; i < sigma.size(); i++) {
#pragma omp parallel for num_threads(PbrtOptions.nCores)
        for(int y = 0; y < yPixelCount; y++)
            for(int x = 0; x < xPixelCount; x++) {
                float error = fltMseArray[i](x, y);
                if(error < minMseImg(x, y)) {
                    Color c = fltArray[i](x, y);
                    adaptImg(x, y) = error/(c.Y()*c.Y()+1e-3f);
                    minMseImg(x, y) = error;
                    fltImg(x, y) = c;
                    sigmaImg(x, y) = Color((float)i/(float)sigma.size());
                }
            }
    }

    reporter.Update();
    reporter.Done();
}

