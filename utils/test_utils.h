#pragma once

#ifndef __TEST_UTILS_H
#define __TEST_UTILS_H

#include "optimization.h"
#include "glm/glm.hpp"

using namespace grid;

class TestSuit {
private:
	static double ModifPM(int max_vcycle, bool resetf = true, int presmooth = 1, int postsmooth = 1, bool silence = false, bool updateStencil = true);
public:
	static void testMain(const std::string& testname);

	static void writeSurfaceElements(const std::string& filename);

	static void writeWorstForce(const std::string& filename);

	static void writeWorstDisplacement(const std::string& filename);

	static void test1VcycleAndAccurateSolve(void);

	static void testinAccurateSolveTotalTime(void);

	static void testinAccurateSolveInterDensity(void);

	static void testStressComplianceDistribution(void);

	static void testAccuratePMTotalTime(void);

	static void testOrdinaryTopopt(void);

	static void testOrdinaryTopoptMMA(void);

	static void testOrdinarySplineTopopt(void);

	static void testOrdinarySplineTopoptMMA(void);   // spline + oldss(paper2) + certain

	static void testOrdinarySplineTopoptMMA_ss3(void);   // spline + newss(paper3) + certain

	static void testRobustMMA(void);

	static void testRobustSplineMMA(void);          // spline + oldss(paper2) + uncertain

	static void testDifferentEigenSolvers(void);

	static void testDifferentInitForce(void);

	static void testMemoryUsage(void);

	static void testReplayModiPM(void);

	static void testModifiedPMError(void);

	static void testDistributeForceOpt(void);

	static void extractMeshFromDensity(void);

	static void testModifiedPM(void);

	static void verifyModifiedPM(void);

	static void testFEM(void);

	static void testUnitLoad(void);

	static void testSymmDensity(void);

	static void testBESO(void);

	static void testSurfVoxels(void);

	static void testGStimes(void);

	static double gsPM(int n_vcycle, int presmooth, int postsmooth);

	static std::pair<double, Eigen::Matrix<double, -1, 1>> powerMethod(const Eigen::Matrix<double, -1, -1>& C);

	static void test2011(void);

	static void testtest2011(void);

	static void testKernels(void);

	static void setInitDensity(float* newrho);

	static void setDensity(float* newrho);

	static void setCoeff(float* newcijk);

	static void scaleVector(float* p_data, size_t len, float scale);

	static void test2019(void);

	static void stressAndComplianceOnVertex(const std::vector<glm::vec4>& p4, std::vector<double>& clist, std::vector<double>& vonlist);
};


#endif

