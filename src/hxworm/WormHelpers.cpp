////////////////////////////////////////////////////////////////
//
// Do not use CVS keywords because they cause merge conflicts and make branching unnecessarily hard.
//
// // Trims vectors in a SurfaceVectorField so that all surfaces generated by Displaces along the vectors are intersection-free
//
//
/////////////////////////////////////////////////////////////////
#define _USE_MATH_DEFINES
#include <math.h>

#include <hxcore/HxData.h>
#include <hxcore/HxWorkArea.h>
#include <hxcore/HxMessage.h>
#include <hxcore/HxObjectPool.h>
#include <hxcore/HxController.h>
#include <mclib/McDMatrix.h>
#include <hxcore/HxInterpreter.h>
#include <mclib/McProgressInterface.h>
#include <mclib/McMat2d.h>

#include <hxsurftools/HxScanConvertSurface.h>
#include <hxfield/HxUniformLabelField3.h>
#include <hxfield/HxUniformVectorField3.h>
#include <hxarith/HxArithmetic.h>
#include <hxarith/HxGradient.h>
#include <hxarith/HxResample.h>
#include <hxactiveseg/HxAdjustDeformableSurface.h>
#include <hximproc/ImGaussFilter3D.h>
#include <empackage/Watershed.h>							
#include <hxght/HxGeneralizedHoughTransform.h>
#include <hxsurftools/PrincipalAxesAligner.h>
#include <hxlines/HxLineSet.h>

#include <hxworm/WormHelpers.h>

McHandle<HxUniformLabelField3> WormHelpers::scanConvertSmall(HxSurface* surf, HxUniformLabelField3* field, int &volumeOverlap)
{
	//scan convert currentSurf to cropped mergeSurfLabelsEroded: 
	McVec3f bboxOrigin =  surf->getBoundingBoxOrigin();
	McVec3f bboxSize =  surf->getBoundingBoxSize();

	float bigBbox[6];
	const int* bigDims = field->lattice.dims();
	field->getBoundingBox(bigBbox);
	McVec3f vs = field->getVoxelSize();

	int minGridPos[3]; 
	getGridPosFromPosition(bigBbox, bigDims, vs, bboxOrigin, minGridPos);
	int maxGridPos[3]; 
	getGridPosFromPosition(bigBbox, bigDims, vs, bboxOrigin+bboxSize, maxGridPos);

	int tol=3;
	for (int i=0; i<3; i++){
		minGridPos[i]-=tol;
		maxGridPos[i]+=tol;
	}

	McHandle<HxUniformLabelField3> fieldCropped = field->duplicate();
	fieldCropped->lattice.crop(minGridPos,maxGridPos);

	float smallBbox[6];
	const int* sD = fieldCropped->lattice.dims();
	int smallDims[3]={sD[0],sD[1],sD[2]};
	fieldCropped->getBoundingBox(smallBbox);

	ScanConvertSurface* scanConvert = new ScanConvertSurface();
	scanConvert->interactive=false;
	scanConvert->material=0;
    McHandle<HxUniformLabelField3> surfLabelsCropped = 	dynamic_cast<HxUniformLabelField3*>(scanConvert->scanConvertSurface(surf, fieldCropped->coords(),
                       volumeOverlap,
                       fieldCropped));

	return surfLabelsCropped;
}

int WormHelpers::getVoxelFromGridPos(const int* dims, const int* gridPos)
{
	return (gridPos[2]*dims[1] + gridPos[1])*dims[0] + gridPos[0];
}

bool WormHelpers::getGridPosFromPosition(const float* bbox, const int* dims, McVec3f voxelSize, McVec3f translation, int* gridPos)
{
	McVec3f origin(bbox[0],bbox[2],bbox[4]);
	McVec3f offset = translation-origin;
	McVec3f voxelID = offset.compquot(voxelSize);

	bool isInGrid=true;
	//int* gridPos = new int[3];
	for (int i=0; i<3; i++){
		int dummyGridPos =(int)(voxelID[i]+0.5);
		gridPos[i] = std::max(0, std::min( dummyGridPos, dims[i]-1) );
		if (gridPos[i]!=dummyGridPos)
			isInGrid=false;
	}
	return isInGrid;
}

HxSurface* WormHelpers::adaptFromOutside(HxSurface* surf, HxUniformScalarField3* image, HxUniformLabelField3* mask, float profLength, int numSamples, float thresh, float tol, float gradThresh, bool doLinearTrafo, float & fit)
{
	McString adjustAlgorithmSettings;
	McHandle<HxSurface> extractedSurface = surf;

	if (doLinearTrafo) {
		McHandle<HxAdjustDeformableSurface> adjust = new HxAdjustDeformableSurface();
		adjust->portData.connect(image);
		adjust->portSurface.connect(surf);
		adjust->fire();
		if (mask!=NULL) {
			adjust->portMaskData.connect(mask);
		}
		adjust->fire();
		adjust->setMaxIterations(10);
		adjust->portAlgorithm.setValue("PelvicBoneCTCostFunction2");
		adjust->fire();
		adjust->portOptions.setValue(0,0);
		adjust->portOptions.setValue(1,0);
		adjust->portOptions.setValue(2,0);
		adjust->portOptions.setValue(3,0);
		adjust->portOptions.setValue(4,1);
		adjust->portOptions.setValue(5,0);
		adjust->portAdjust.setValue(1);
		adjust->portMode.setValue(1);
		adjust->portTrafoType.setValue(2);
		adjust->portShow.setValue(0);
		McString activeString = adjust->getLabel();
		const char* activeName = activeString.dataPtr();
		//
		adjustAlgorithmSettings.printf("%s {profiles} setValue 0 %d ; %s {profiles} setValue 1 %f ;	%s {params} setValue 0 %f ; %s {params} setValue 1 %f ; %s {params} setValue 2 %f ; %s {prefFactor} setValue 0 1 ; %s {ScaleOptions} setValue 0 0 ; %s {NearestPatches} setState { } ; %s {InmostPatches} setState {0 } ; %s {IgnorePatches} setState { } ; %s {StayTherePatches} setState { } "	,activeName, numSamples, activeName, profLength, activeName, thresh, activeName, tol, activeName, gradThresh, activeName, activeName, activeName, activeName, activeName, activeName);
		theInterpreter->eval(adjustAlgorithmSettings);
		adjust->fire();
		adjustAlgorithmSettings.printf("%s {OptimizeMethod} setIndex 0 1 ",activeName); 
		theInterpreter->eval(adjustAlgorithmSettings);
		adjust->fire();
		adjustAlgorithmSettings.printf("%s {constraints} setValue 0 1 ",activeName); 
		theInterpreter->eval(adjustAlgorithmSettings);
		adjust->fire();

		adjust->portAction.hit();
		adjust->fire();
		McHandle<HxSurface > activeResult = dynamic_cast<HxSurface*>(adjust->getResult());

		extractedSurface = activeResult;
	}

	McHandle<HxAdjustDeformableSurface> deform = new HxAdjustDeformableSurface();
	deform->portData.connect(image);
	deform->portSurface.connect(extractedSurface);
	deform->fire();
	if (mask!=NULL) {
		deform->portMaskData.connect(mask);
	}
	deform->portOptions.setValue(0,0);
	deform->portOptions.setValue(1,1);
	deform->portOptions.setValue(2,1);
	deform->portOptions.setValue(3,1);
	deform->portOptions.setValue(4,1);
	deform->portOptions.setValue(5,1);
	deform->portAdjust.setValue(1);
	deform->fire();
	deform->portAlgorithm.setValue("PelvicBoneCTCostFunction2");
	deform->fire();
	McString deformString = deform->getLabel();
	const char* deformName = deformString.dataPtr();

	adjustAlgorithmSettings.printf("%s setMaxIterations 3 ", deformName);
	theInterpreter->eval(adjustAlgorithmSettings);
	deform->fire();
	adjustAlgorithmSettings.printf("%s {profiles} setValue 0 %d ;	%s {profiles} setValue 1 %f ;", deformName, numSamples, deformName, profLength);
	theInterpreter->eval(adjustAlgorithmSettings);
	deform->fire();
	adjustAlgorithmSettings.printf(" %s {OptimizeMethod} setIndex 0 1 ; %s fire ; %s {params} setValue 0 %f ;	%s {params} setValue 1 %f ;	%s {params} setValue 2 %f ;	", deformName, deformName, deformName, thresh, deformName, tol, deformName, gradThresh); 
	theInterpreter->eval(adjustAlgorithmSettings);
	deform->fire();
	adjustAlgorithmSettings.printf(" %s {prefFactor} setValue 0 0.1 ; %s {NearestPatches} setState { } ;	%s {InmostPatches} setState {0 } ;	%s {IgnorePatches} setState { } ;	%s {StayTherePatches} setState { }", deformName, deformName, deformName, deformName, deformName);
	theInterpreter->eval(adjustAlgorithmSettings);
	deform->fire();

	deform->portAction.hit();
	deform->fire();

	adjustAlgorithmSettings.printf("%s costInfo getValue 0 ", deformName);
	McString fitValString;
	theInterpreter->eval(adjustAlgorithmSettings,fitValString);
	fit = atof(fitValString.getString());

//	if (adjust) 	theObjectPool->addObject(adjust);
//	theObjectPool->addObject(deform);

	HxSurface* deformResult = dynamic_cast<HxSurface*>(deform->getResult());

	return deformResult;
}

void WormHelpers::doGht(HxSurface* templateSurf, HxUniformScalarField3* image, HxUniformLabelField3* maskImage, const float* scaleRange, int* nGhtSteps, int maxNumTrafos, int numThreads, const float* wsParams, bool smoothAndResampleForWs, HxUniformScalarField3* &accuImage, HxTransforms* &trafos, HxUniformScalarField3* &wsImage )
{
    McHandle<HxGradient> grad = new HxGradient();
    grad->portData.connect(image);
    grad->fire();
    grad->portRestype.setValue(1);
    grad->fire();
    grad->portDoIt.hit();
    grad->fire();
	McHandle<HxUniformVectorField3> gradientImage = dynamic_cast<HxUniformVectorField3*>(grad->getResult());

	McHandle<HxGeneralizedHoughTransform> ght = new HxGeneralizedHoughTransform();
    ght->portData.connect(templateSurf);
    ght->portGradientField.connect(gradientImage);
    ght->portMask.connect(maskImage);
    ght->fire();

	ght->portInvert.setValue(1);
    ght->fire();

	float scale0=scaleRange[0];
	float scale1=scaleRange[1];
    ght->portScale.setValue(0, 1);
    ght->portScale.setValue(2, scaleRange[0]);
    ght->portScale.setValue(4, scaleRange[1]);
    ght->portScale.setValue(6, nGhtSteps[0]);
    ght->portYScale.setValue(0, 1);
    ght->portYScale.setValue(2, scaleRange[2]);
    ght->portYScale.setValue(4, scaleRange[3]);
    ght->portYScale.setValue(6, nGhtSteps[1]);
    ght->portZScale.setValue(0, 1);
    ght->portZScale.setValue(2, scaleRange[4]);
    ght->portZScale.setValue(4, scaleRange[5]);
    ght->portZScale.setValue(6, nGhtSteps[2]);

	int rotGhtSteps = std::max(std::max(nGhtSteps[0],nGhtSteps[1]),nGhtSteps[2]);
	float degrees = 90-90./rotGhtSteps;

	ght->portXRotate.setValue(0, (int)(rotGhtSteps>1));
	ght->portXRotate.setValue(2, degrees);
	ght->portXRotate.setValue(4, rotGhtSteps);
	ght->portYRotate.setValue(0, (int)(rotGhtSteps>1));
	ght->portYRotate.setValue(2, degrees);
	ght->portYRotate.setValue(4, rotGhtSteps);
	ght->portZRotate.setValue(0, (int)(rotGhtSteps>1));
	ght->portZRotate.setValue(2, degrees);
	ght->portZRotate.setValue(4, rotGhtSteps);

	ght->portBins.setValue(6);
	ght->portInvert.setValue(1);
	ght->portOptions.setValue(0,1);
	ght->portOptions.setValue(1,0);
	ght->portOptions.setValue(2,1);

	ght->portSortedTransforms.setValue(0);
	ght->portNumThreads.setValue(numThreads);

	ght->fire();
	ght->portApply.hit();
	ght->fire();

	accuImage = dynamic_cast<HxUniformScalarField3*> ( ght->getResult() );  

	float minAccu, maxAccu;
	accuImage->getRange(minAccu, maxAccu);

    McHandle<HxArithmetic> arith = new HxArithmetic();
    arith->portData.connect(accuImage);
    arith->fire();
	QString expression;
	expression.sprintf("-A+%f",maxAccu);
    arith->_portExpr[0]->setValue(expression);
    arith->_portAction.hit();
    arith->fire();
    McHandle<HxUniformScalarField3> invAccu = dynamic_cast<HxUniformScalarField3*> (arith->getResult());
//    invAccu->portMaster.disconnect();

	if (smoothAndResampleForWs) {
		McTypedPointer srcPtr(invAccu->lattice.dataPtr(),invAccu->primType());
		McTypedData3D src(invAccu->lattice.dims(),srcPtr);

		ImGaussFilter3D gauss;
		gauss.setParams(5,0.4);
		gauss.apply3D(&src);

		McVec3f vs = invAccu->getVoxelSize();
		int resampleBy = 3;
        McHandle<HxResample> resample = new HxResample();
        resample->portData.connect(invAccu);
        resample->fire();
		resample->portMode.setValue(1);
        resample->fire();
		resample->portVoxSize.setValue(0,vs[0]*resampleBy); 
		resample->portVoxSize.setValue(1,vs[1]*resampleBy); 
		resample->portVoxSize.setValue(2,vs[2]*resampleBy); 
        resample->fire();
        resample->portFilter.setValue(6);//"Minimum"
        resample->fire();
	    resample->portAction.hit(0);
	    resample->fire();
        McHandle<HxUniformScalarField3> invAccuResampled = dynamic_cast<HxUniformScalarField3*>(resample->getResult());

//		theObjectPool->addObject(resample);
//		theObjectPool->addObject(invAccu);

		invAccu = invAccuResampled;
	}

	// ws parameters:
	float minusSome = maxAccu/10.;
	float maxAccuMinusSome = maxAccu-minusSome;
	float numSteps = maxAccuMinusSome/5.;

	McHandle<Watershed> ws = new Watershed();
	ws->portData.connect(invAccu);
	ws->fire();
	ws->portThreshold.setValue(0,0);
	ws->portThreshold.setValue(1,maxAccuMinusSome);
	ws->portSteps.setValue(0, numSteps);
	ws->portMaxNewThreshold.setValue(0,maxAccuMinusSome);
	ws->fire();
	ws->portAction.hit();
	ws->fire();

    McHandle<HxUniformScalarField3> watershedImage = dynamic_cast<HxUniformScalarField3*> (ws->getResult());

	if (smoothAndResampleForWs) {
		const int* dims = image->lattice.dims();
        McHandle<HxResample> resample2 = new HxResample();
        resample2->portData.connect(watershedImage);
        resample2->fire();
		resample2->portMode.setValue(0);
        resample2->fire();
		resample2->portRes.setValue(0,dims[0]);
		resample2->portRes.setValue(1,dims[1]);
		resample2->portRes.setValue(2,dims[2]);
		resample2->fire();
        resample2->portFilter.setValue(0);//"Box"
        resample2->fire();
	    resample2->portAction.hit(0);
	    resample2->fire();
        McHandle<HxUniformScalarField3> watershedImageResampled = dynamic_cast<HxUniformScalarField3*>(resample2->getResult());
	
//		theObjectPool->addObject(watershedImage);
//		theObjectPool->addObject(resample2);

		watershedImage = watershedImageResampled;
	}

	ght->portNumberTransforms.setValue(maxNumTrafos);
	ght->fire();
	ght->portCreateTransforms.hit(0);
	ght->fire();
	trafos = ((HxTransforms*)ght->getResult(1));//->getTransforms();

	wsImage = watershedImage;
}

void WormHelpers::getPrincipalAxes(HxSurface* surf, McDArray<McVec3f>& eVecs, McVec3d& eVals, McVec3d& radiiMin, McVec3d& radiiMax)
{
	McDArray<McVec3f> pts = surf->points;
	McDMatrix<float> pointMatrix(3,pts.size());
	for (int v=0; v<pts.size(); v++) {
		McVec3f pt = pts[v];
			for (int j=0; j<3; j++) {
				pointMatrix[j][v]=pt[j];
			}
	}

	McDArray<float> center;
	McDArray<McDArray<float> > eVecsTmp;
    McDArray<float> eValsTmp;
	McDArray<McDArray<float> > dummyWeights;
	pointMatrix.pca(center, eValsTmp, eVecsTmp, dummyWeights);

	eVecs.resize(3);
	for (int i=0; i<3; i++) {
		eVals[i]=eValsTmp[i];
		for (int j=0; j<3; j++) {
			eVecs[i][j] = eVecsTmp[i][j];
		}
	}
	eVecs[2]=eVecs[0].cross(eVecs[1]);
	eVecs[2].normalize();

	radiiMin.setValue(0,0,0);
	for (int i=0; i<3; i++) {
		for (int v=0;v<pts.size();v++) {
			radiiMin[i]=std::min((float)radiiMin[i],dummyWeights[v][i]);
		}
	}
	radiiMax.setValue(0,0,0);
	for (int i=0; i<3; i++) {
		for (int v=0;v<pts.size();v++) {
			radiiMax[i]=std::max((float)radiiMax[i],dummyWeights[v][i]);
		}
	}
}

McDArray<McVec3f> WormHelpers::extractPatchCenterPoints(HxSurface* surface)
{
	int nPoints = surface->points.size();

	McDArray<int> used(0);
	surface->findUsedPoints(used);
	if (used.size()<nPoints) {
		return surface->points;
	}

	int nPatches = surface->patches.size();
	McDArray<McVec3f> centers(nPatches);
	centers.fill(McVec3f(0));
	McDArray<int> numPointsPerPatch(nPatches);
	numPointsPerPatch.fill(0);

	surface->computeTrianglesPerPoint();
	for (int i=0; i<nPoints; i++)
	{
		int triIdx = surface->trianglesPerPoint[i][0];
		int patch = surface->triangles[triIdx].patch;
		if (patch > -1) {
			centers[patch]+=surface->points[i];
			numPointsPerPatch[patch]++;
		}
	}
    
	for (int p=0; p<nPatches; p++)
	{
		centers[p]/=numPointsPerPatch[p];
	}

	//HxSurface* centerSurf = new HxSurface(nPatches, 0, 0, 1);
	//centerSurf->points = centers;
	
	return centers;

}

HxSurface* WormHelpers::extractPatchCenterVertexSurf(HxSurface* surf)
{
	McDArray<McVec3f> centers = extractPatchCenterPoints(surf);
	HxSurface* vertexSurf = new HxSurface(centers.size(),0,0,1);
	vertexSurf->points = centers; 
	return vertexSurf;
}

void WormHelpers::getWormCoordSystem(HxSurface* surf, McVec3f& center, McVec3f& apAxis, McVec3f& dvAxis, McVec3f& lrAxis, McVec3d& extMin, McVec3d& extMax){
	McDArray<McVec3f> eVecsTT;
	McVec3d eValsTT; 
	WormHelpers::getPrincipalAxes(surf, eVecsTT, eValsTT, extMin, extMax);
	float cTT[3];
	surf->getCenter(cTT);
	center.setValue(cTT[0],cTT[1],cTT[2]);
	apAxis = eVecsTT[0];
	apAxis.normalize();
	int sign = std::abs(extMin[0]) > std::abs(extMax[0]) ? -1 : 1;
	apAxis*=sign;
	if (sign<0) {
		McVec3d dummy = extMin;
		extMin = -extMax;
		extMax = -dummy;
	}

	// crop out 2/5..4/5 along apAxisTT: 
	McHandle<HxSurface> croppedSurfTT = cropToBWM(surf, apAxis); 
	McVec3d extMinCropped,extMaxCropped; 
	WormHelpers::getPrincipalAxes(croppedSurfTT, eVecsTT, eValsTT, extMinCropped, extMaxCropped);
	croppedSurfTT->getCenter(cTT);
	McVec3f centerTTCropped = McVec3f(cTT);
	// which one is the dorso-ventral axis? 
	McVec3f axis1 = eVecsTT[1];
	McVec3f axis2 = eVecsTT[2];
	axis1.normalize();
	axis2.normalize();
	//instead: which axis is more symmetric?
	// avg above, axis1:
//	double distAbove=0;
//	double distBelow=0;
	McDArray<double> x1(0);
	McDArray<double> x2(0);
	McDArray<double> y(0);
	McDArray<double> distAbove(0);
	McDArray<double> distBelow(0);
	for (double angle=0; angle<2*M_PI; angle+=2*M_PI/360) {
		McRotation rot(apAxis,angle);
		McVec3f axis;
		rot.multVec(axis1,axis);
		int numAbove1=0;
		int numBelow1=0;
		double dA=0;
		double dB=0;
		for (int p=0; p<croppedSurfTT->getNumPoints(); p++) {
			double diff = (croppedSurfTT->points[p]-centerTTCropped).dot(axis);
			if (diff<0 ) {
				numAbove1++;
				dA+=diff*diff;
			} else {
				numBelow1++;
				dB+=diff*diff;
			}
		}
		double distAbove1=std::sqrt(dA/numAbove1);
		double distBelow1=std::sqrt(dB/numBelow1);
		double stdev = std::sqrt((dA+dB)/(numAbove1+numBelow1));
		double diff1 = (std::abs(distAbove1)-std::abs(distBelow1))/stdev;
		//(float)(std::max(numAbove1,numBelow1))/std::min(numAbove1,numBelow1);//std::abs(std::abs(extMin[1])-std::abs(extMax[1]))/(std::abs(extMin[1])+std::abs(extMax[1]));//
#ifdef _DEBUG
		// debug output:
		theMsg->printf("angle %f diff %f",angle,diff1);
#endif
		x1.append(std::sin(angle));
		x2.append(-std::cos(angle));
		y.append(diff1);
		distAbove.append(distAbove1);
		distBelow.append(distBelow1);
	}
	int numA=x1.size();
	double m[4]={0,0,0,0};
	McVec2d xty(0);
	for (int a=0; a<numA; a++){
		m[0]+=x1[a]*x1[a];
		m[3]+=x2[a]*x2[a];
		m[1]+=x1[a]*x2[a];
		m[2]+=x1[a]*x2[a];
		xty[0]+=x1[a]*y[a];
		xty[1]+=x2[a]*y[a];
	}
	McMat2d xtx(m);
	McVec2d sinParams = xtx.inverse()*xty;
	double offset = std::atan(sinParams[1]/sinParams[0]);
	double amp = sinParams[0]/std::cos(offset);
	McRotation rot(apAxis,offset+M_PI_2);
	rot.multVec(axis1,dvAxis);
	sign = amp<0 ? -1 : 1;
	dvAxis *= sign;

	lrAxis = apAxis.cross(dvAxis);

#ifdef _DEBUG_BLUBB
	// debug output:
	McHandle<HxLineSet> debugAxes =new HxLineSet();
	McDArray<McVec3f> points(0);
	points.append(centerTTCropped);
	points.append(centerTTCropped-apAxis*100);
	points.append(centerTTCropped+apAxis*100);
	points.append(centerTTCropped-axis1*100);
	points.append(centerTTCropped+axis1*100);
	points.append(centerTTCropped-axis2*100);
	points.append(centerTTCropped+axis2*100);
	debugAxes->addPoints(points,points.size());
	for (int i=1; i<7; i++) {
			int line[2] = {0, i};
			debugAxes->addLine(2,line);
	}
	theObjectPool->addObject(croppedSurfTT);
	theObjectPool->addObject(debugAxes);
#endif


}

HxSurface* WormHelpers::cropToBWM(HxSurface* surf, McVec3f apAxis)
{
	HxSurface* croppedSurf = dynamic_cast<HxSurface *>(surf->duplicate());
	McVec3f point;
	float minPosAlongAxis = apAxis.dot(croppedSurf->points[0]);
	int minIdx = 0;
	float maxPosAlongAxis = apAxis.dot(croppedSurf->points[croppedSurf->getNumPoints()-1]);
	int maxIdx = croppedSurf->getNumPoints()-1;
	for (int p=1; p<croppedSurf->getNumPoints(); p++) {
		float posAlongAxis = apAxis.dot(croppedSurf->points[p]);
		minIdx = (posAlongAxis<minPosAlongAxis) ? p : minIdx;
		minPosAlongAxis = (posAlongAxis<minPosAlongAxis) ? posAlongAxis : minPosAlongAxis;
		maxIdx = (posAlongAxis>maxPosAlongAxis) ? p : maxIdx;
		maxPosAlongAxis = (posAlongAxis>maxPosAlongAxis) ? posAlongAxis : maxPosAlongAxis;
	}

	McVec3f origin = croppedSurf->points[minIdx];
	float length = maxPosAlongAxis-minPosAlongAxis;
	int keptPointIdx = 0;
	for (int p=0; p<croppedSurf->getNumPoints(); p++) {
		point = croppedSurf->points[p]-origin;
		if (apAxis.dot(point)>0.4*length && apAxis.dot(point)<0.8*length ) {
			keptPointIdx=p;
			break;
		}
	}

	for (int p=0; p<croppedSurf->getNumPoints(); p++) {
		point = croppedSurf->points[p]-origin;
		if (apAxis.dot(point)<0.4*length || apAxis.dot(point)>0.8*length ) {
			croppedSurf->points[p]=croppedSurf->points[keptPointIdx];
		}
	}
	float eps=1E-5;
	croppedSurf->removeDuplicatePoints(eps);
	return croppedSurf;
}
