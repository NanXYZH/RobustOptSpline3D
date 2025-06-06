#include "optimization.h"
#include "cuda_runtime.h"
//#include "device_atomic_functions.hpp"
#include "lib.cuh"
#include "Grid.h"
#include "gpuVector.h"
#include <vector>
#include "templateMatrix.h"
//#include "gpuVector.h"
#include "mma_t.h"

extern  __constant__  double gTemplateMatrix[24][24];
extern  __constant__ int* gV2E[8];
extern  __constant__ int* gV2Vfine[27];
extern  __constant__ int* gV2Vcoarse[8];
extern  __constant__ int* gV2V[27];
extern  __constant__ int* gVfine2Vfine[27];
extern  __constant__ int* gV2VfineC[64];// vertex to fine grid element center 
extern  __constant__ int* gVfine2Efine[8];
extern  __constant__ int* gVfine2Effine[8];
extern  __constant__ float power_penalty[1];
extern  __constant__ double* gU[3];
extern  __constant__ double* gF[3];
extern  __constant__ double* gR[3];
extern  __constant__ double* gUworst[3];
extern  __constant__ double* gFworst[3];
extern  __constant__ double* gRfine[3];
extern  __constant__ double* gUcoarse[3];
extern  __constant__ int gGS_num[8];
extern  __constant__ int gmode[1];
extern  __constant__ int* gVflag[1];
extern  __constant__ int* gEflag[1];
extern  __constant__ int gLayerid[1];
extern  __constant__ int gDEBUG[1];
extern  __constant__ int gorder[1];
extern  __constant__ int gnpartition[3];
extern  __constant__ int gnbasis[3];
extern  __constant__ int gnknotspan[3];
extern  __constant__ float gnstep[3];
extern  __constant__ float gnBoundMin[3];
extern  __constant__ float gnBoundMax[3];

extern  __constant__ float* gpu_KnotSer[3];
extern  __constant__ float* gpu_cijk;
extern  __constant__ float* gpu_SurfacePoints[3];
extern  __constant__ float* gpu_surface_normal[3];
extern  __constant__ float* gpu_surface_hessian[9];
extern  __constant__ float* gpu_surface_normal_direction[1];
extern  __constant__ float* gpu_surface_normal_dc[3];         // derivative to coeffs
extern  __constant__ float* gpu_surface_normal_norm_dc[1];

extern  __constant__ float* gpu_bg_ele[3];
extern  __constant__ float* gpu_bg_ele_value[1];
extern  __constant__ float* gpu_bg_ele_normal[3];
extern  __constant__ float* gpu_bg_ele_hessian[9];
extern  __constant__ float* gpu_bg_ele_normal_direction[1];

extern  __constant__ int gssmode[1];
extern  __constant__ int gdripmode[1];
extern  __constant__ float gdefaultPrintAngle[1];
extern  __constant__ float goptPrintAngle[1];


extern __device__ void loadTemplateMatrix(volatile double KE[24][24]);

template<int N>
__device__ int gridPos2id(int x, int y, int z) {
	return x + y * N + z * N * N;
}

__global__ void para_minus(
	int nv, const float* input1, const float* input2, float* dst) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;
	if (tid >= nv) return;

	int eid = gV2E[7][tid];

	if (eid == -1) { return; }

	float a = input1[eid];
	float b = input2[eid];

	dst[eid] = a - b;
}

__global__ void para_minus_abs(
	int nv, const float* input1, const float* input2, float* dst) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;
	if (tid >= nv) return;

	int eid = gV2E[7][tid];

	if (eid == -1) { return; }

	float a = input1[eid];
	float b = input2[eid];

	dst[eid] = abs(a - b);
}

//  suppose Uworst, Fworst is prepared in U, F
__global__ void computeSensitivity_kernel(int nv, float* rholist, double mu, float* sens) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;

	__shared__ double KE[24][24];

	loadTemplateMatrix(KE);

	if (tid >= nv) return;

	int vid = tid;

	int vneigh[27];
	for (int i = 0; i < 27; i++) {
		vneigh[i] = gV2V[i][vid];
	}

	// traverse incident elements of vertex
	for (int i = 0; i < 8; i++) {
		double partialSens = 0;

		int eid = gV2E[i][vid];
		if (eid == -1) continue;
		double Ui[3] = { gU[0][vid],gU[1][vid],gU[2][vid] };
		double penal = power_penalty[0] * powf(rholist[eid], power_penalty[0] - 1);

		// compute partial node force (element i's contribution) K_\rho * Uworst on vi
		double KrhoU[3] = { 0. };
		// vertex self id in neihbor element i
		int vi = 7 - i;
		// vertex neighbor id in element i, traverse them and compute the corresponding node force on self
		for (int vj = 0; vj < 8; vj++) {
			int vjlid = gridPos2id<3>(i % 2 + vj % 2, i % 4 / 2 + vj % 4 / 2, i / 4 + vj / 4);
			double Uj[3];
			for (int k = 0; k < 3; k++) Uj[k] = gU[k][vneigh[vjlid]];
			for (int krow = 0; krow < 3; krow++) {
				for (int kcol = 0; kcol < 3; kcol++) {
					KrhoU[krow] += KE[vi * 3 + krow][vj * 3 + kcol] * Uj[kcol];
				}
			}
		}

		for (int k = 0; k < 3; k++) KrhoU[k] *= penal;

#if 0
		// sensitivity  u_worst * dK/drho * u_worst
		for (int k = 0; k < 3; k++) partialSens += Ui[k] * KrhoU[k];

		// sensitivity  - 2 mu * u_worst * dK/drho * K * u_worst
		for (int k = 0; k < 3; k++) partialSens += -2 * mu * KrhoU[k] * gFworst[k][vid];

		// sensitivity  - lambda * N * dK/drho * u_worst
		for (int k = 0; k < 3; k++) {
			partialSens += -KrhoU[k] * gU[k][vid];
		}
#else
		// sensitivity  - u_worst * dK/drho * u_worst
		for (int k = 0; k < 3; k++) partialSens -= Ui[k] * KrhoU[k];

#endif

		atomicAdd(sens + eid, float(partialSens));
	}

}

__global__ void computeProjectionSensitivity_kernel(
	int nv, const float* initrholist, const float* sens, float* newsens, float beta) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;
	if (tid >= nv) return;

	int eid = gV2E[7][tid];

	if (eid == -1) { return; }

	//float beta = 8;
	float eta = 0.5;

	float sensold = sens[eid];

	float rho = initrholist[eid];

#if 0
	float diracrho = 1;
#else
	float diracrho = beta * (1 - tanhf(beta * (rho - eta)) * tanhf(beta * (rho - eta))) / (tanhf(beta * eta) + tanhf(beta * (1 - eta)));
#endif
	newsens[eid] = sensold * diracrho;
}

void computeSensitivity(void) {
	grids[0]->use_grid();
	// now, suppose Uworst, Fworst is prepared, N^T * Lambda is in U,
	// copy Fworst=KUworst to F
	//grids[0]->v3_copy(grids[0]->getWorstForce(), grids[0]->getForce());

	int para_beta = 8;

	// init sensitivity to zero
	init_array(grids[0]->getSens(), float{ 0 }, grids[0]->n_rho());

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_nodes(), 512);

	computeSensitivity_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getRho(), grids[0]->_keyvalues["mu"], grids[0]->getSens());
	cudaDeviceSynchronize();
	cuda_error_check;

	// DEBUG
	grids[0]->sens2matlab("sens");

//	// project sensitivity
//#ifdef ENABLE_HEAVISIDE
//	//MARK[TO CHANGE] sth wrong : use init rho or rho after filter, not rho after Heaviside
//	computeProjectionSensitivity_kernel << < grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getInitRho(), grids[0]->getSens(), grids[0]->getSens(), para_beta);
//	cudaDeviceSynchronize();
//	cuda_error_check;
//	grids[0]->initrho2matlab("initrho");
//	grids[0]->rho2matlab("rho");
//	grids[0]->sens2matlab("sensproj");
//#endif

	// filter sensitivity
	grids[0]->filterSensitivity(params.filter_radius);

	// DEBUG
	grids[0]->sens2matlab("sensfilt");

	size_t free_mem, total_mem;
	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

	// rho_diff 2 coeff_diff
	grids[0]->ddensity2dcoeff_update();

	// vol: rho_diff 2 coeff_diff
	// MARK[TODO]: need to fixed
	grids[0]->dvol2dcoeff();

	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

	// DEBUG
	grids[0]->csens2matlab("csensfilt2");
	grids[0]->Volcsens2matlab("volcsensfilt2");

}

void computeSensitivity2(float beta) {
	grids[0]->use_grid();
	// now, suppose Uworst, Fworst is prepared, N^T * Lambda is in U,
	// copy Fworst=KUworst to F
	//grids[0]->v3_copy(grids[0]->getWorstForce(), grids[0]->getForce());

	// init sensitivity to zero
	init_array(grids[0]->getSens(), float{ 0 }, grids[0]->n_rho());

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_nodes(), 512);

	computeSensitivity_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getRho(), grids[0]->_keyvalues["mu"], grids[0]->getSens());
	cudaDeviceSynchronize();
	cuda_error_check;

	// DEBUG
	grids[0]->sens2matlab("sens");

	// init dvol/dexPhys to 1/N
	float dvol2dexPhys = 1.0 / grids[0]->n_rho();
	init_array(grids[0]->getVolSens(), 1.0f, grids[0]->n_rho());
	grids[0]->Volsens2matlab("volsens");

	init_array(grids[0]->getVolSens(), dvol2dexPhys, grids[0]->n_rho());

	// project sensitivity
#ifdef ENABLE_HEAVISIDE
	grids[0]->initrho2matlab("initrho");
	grids[0]->rho2matlab("rho");

	computeProjectionSensitivity_kernel << < grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getInitRho(), grids[0]->getSens(), grids[0]->getSens(), beta);
	cudaDeviceSynchronize();
	cuda_error_check;
	grids[0]->sens2matlab("sensproj");

	computeProjectionSensitivity_kernel << < grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getInitRho(), grids[0]->getVolSens(), grids[0]->getVolSens(), beta);
	cudaDeviceSynchronize();
	cuda_error_check;
	grids[0]->Volsens2matlab("volsensproj");
#endif

	// filter sensitivity 
	grids[0]->filterSensitivity(params.filter_radius);

	// DEBUG
	grids[0]->sens2matlab("sensfilt");

	// filter vol sensitivity
	grids[0]->filterVolSensitivity(params.filter_radius);

	// DEBUG
	grids[0]->Volsens2matlab("volsensfilt");

	size_t free_mem, total_mem;
	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

	// energy: rho_diff 2 coeff_diff
	grids[0]->ddensity2dcoeff_update();

	// vol: vol_diff 2 coeff_diff
	grids[0]->dvol2dcoeff();

	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

#ifdef ENABLE_MATLAB
	// DEBUG
	grids[0]->csens2matlab("csensfilt2");
	grids[0]->Volcsens2matlab("volcsensfilt2");
#endif

}

__global__ void computeProjection_kernel(
	int nv, const float* rholist, float rhomin, float* newrho, float beta) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;
	if (tid >= nv) return;

	int eid = gV2E[7][tid];

	if (eid == -1) { return; }

	//float beta = 8;
	float eta = 0.5;

	float rhoold = rholist[eid];

#if 0
	float rhonew = rhoold;
#else
	float rhonew = (tanhf(beta * eta) + tanhf(beta * (rhoold - eta))) / (tanhf(beta * eta) + tanhf(beta * (1 - eta)));
#endif
	rhonew = clamp(rhonew, rhomin, 1.f);

	if (gEflag[0][eid] & grid::Grid::Bitmask::mask_shellelement) rhonew = 1;

	newrho[eid] = rhonew;
}

void projectDensities(float beta)
{
	grids[0]->use_grid();
	init_array(grids[0]->_gbuf.init_rho_e, float{ 0 }, grids[0]->n_rho());

	cudaMemcpy(grids[0]->_gbuf.init_rho_e, grids[0]->_gbuf.rho_e, sizeof(float) * grids[0]->n_rho(), cudaMemcpyDeviceToDevice);

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_nodes(), 512);

	// update densities by Heaviside
	computeProjection_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getInitRho(), params.min_rho, grids[0]->getRho(), beta);
	cudaDeviceSynchronize();
	cuda_error_check;
}

__global__ void trySensMultiplier_kernel(
	int nv, const float* rholist, float* g_sens, float g_thres, float step, float damp, float rhomin, float* newrho) {
	int tid = blockDim.x*blockIdx.x + threadIdx.x;
	if (tid >= nv) return;

	int eid = gV2E[7][tid];

	if (eid == -1) { return; }

	float g = g_sens[eid];

	if (g > 0) g = 0;

	g = abs(g);

	float rhoold = rholist[eid];

	float rhonew = rhoold * powf(g / g_thres, damp);

	rhonew = clamp(rhonew, rhoold - step, rhoold + step);

	rhonew = clamp(rhonew, rhomin, 1.f);

	if (gEflag[0][eid] & grid::Grid::Bitmask::mask_shellelement) rhonew = 1;

	newrho[eid] = rhonew;
}

float updateDensities(float Vgoal) {
	grids[0]->use_grid();

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_nodes(), 512);

	float Vratio = 2;

	float g_thres_low = 0;
	float g_thres_upp = 1;

	float para_beta = 8;

	// compute old volume ratio
	double* sum = (double*)grid::Grid::getTempBuf(sizeof(double) * grids[0]->n_rho() / 100);
	double Vold = parallel_sum_d(grids[0]->getRho(), sum, grids[0]->n_rho()) / grids[0]->n_rho();

	// compute maximal sensitivity
	float* maxdump = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_rho() / 100);
	float g_max = parallel_maxabs(grids[0]->getSens(), maxdump, grids[0]->n_rho());


	g_thres_upp = g_max;

	printf("[sensitivity] max = %f\n", g_max);

	float g_thres = (g_thres_low + g_thres_upp) / 2;

	// iteration counter
	int itn = 0;

	// bisection search sensitivity multiplier
	do {
		// update sensitivity threshold
		g_thres = (g_thres_low + g_thres_upp) / 2;

		printf("-- searching multiplier g = %4.4e", g_thres);

		float* newrho = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_rho());

		// update new rho
		trySensMultiplier_kernel << <grid_size, block_size >> > (
			grids[0]->n_nodes(), grids[0]->getRho(), grids[0]->getSens(), g_thres, params.design_step, params.damp_ratio, params.min_rho, newrho);
		cudaDeviceSynchronize();
		cuda_error_check;

//#ifdef ENABLE_HEAVISIDE
//		computeProjection_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), newrho, params.min_rho, newrho, para_beta);
//		cudaDeviceSynchronize();
//		cuda_error_check;
//#endif // ENABLE_HEAVISIDE

		// compute new volume ratio
		Vratio = dump_array_sum(newrho, grids[0]->n_rho()) / grids[0]->n_rho();

		printf(", V = %f  goal %f\n", Vratio, Vgoal);

		if (Vratio > Vgoal) {
			g_thres_low = g_thres;
		}
		else if (Vratio < Vgoal) {
			g_thres_upp = g_thres;
		}
	} while (abs(Vratio - Vgoal) > 1e-4 && itn++ < 30);

	// update densities according to new sensitivity 
	// use current g_thres to update rho(like newrho before)
	trySensMultiplier_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getRho(), grids[0]->getSens(), g_thres, params.design_step, params.damp_ratio, params.min_rho, grids[0]->getRho());
	cudaDeviceSynchronize();
	cuda_error_check;

//	// update Heaviside densities
//#ifdef ENABLE_HEAVISIDE
//	projectDensities(para_beta);
//#endif // ENABLE_HEAVISIDE

	return g_thres;
}

float updateDensities2(float Vgoal, float beta, float change[2]) {
	grids[0]->use_grid();

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_nodes(), 512);

	float Vratio = 2;

	float g_thres_low = 0;
	float g_thres_upp = 1;

	// compute old volume ratio
	double* sum = (double*)grid::Grid::getTempBuf(sizeof(double) * grids[0]->n_rho() / 100);
	double Vold = parallel_sum_d(grids[0]->getRho(), sum, grids[0]->n_rho()) / grids[0]->n_rho();

	// compute maximal sensitivity
	float* maxdump = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_rho() / 100);
	float g_max = parallel_maxabs(grids[0]->getSens(), maxdump, grids[0]->n_rho());

	// record old rho
	float* rhoold;
	cudaMalloc(&rhoold, sizeof(float) * grids[0]->n_rho());
	init_array(rhoold, float{ 0 }, grids[0]->n_rho());
	cuda_error_check;
	cudaMemcpy(rhoold, grids[0]->_gbuf.init_rho_e, sizeof(float) * grids[0]->n_rho(), cudaMemcpyDeviceToDevice);

	g_thres_upp = g_max;

	printf("[sensitivity] max = %4.4e\n", g_max);

	float g_thres = (g_thres_low + g_thres_upp) / 2;

	// iteration counter
	int itn = 0;

	// bisection search sensitivity multiplier
	do {
		// update sensitivity threshold
		g_thres = (g_thres_low + g_thres_upp) / 2;

		printf("-- searching multiplier g = %4.4e", g_thres);

		float* newrho = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_rho());

		// update new rho
		trySensMultiplier_kernel << <grid_size, block_size >> > (
			grids[0]->n_nodes(), grids[0]->getInitRho(), grids[0]->getSens(), g_thres, params.design_step, params.damp_ratio, params.min_rho, newrho);
		cudaDeviceSynchronize();
		cuda_error_check;

#ifdef ENABLE_HEAVISIDE
		computeProjection_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), newrho, params.min_rho, newrho, beta);
		cudaDeviceSynchronize();
		cuda_error_check;
#endif // ENABLE_HEAVISIDE

		// compute new volume ratio
		Vratio = dump_array_sum(newrho, grids[0]->n_rho()) / grids[0]->n_rho();

		printf(", V = %f  goal %f\n", Vratio, Vgoal);

		if (Vratio > Vgoal) {
			g_thres_low = g_thres;
		}
		else if (Vratio < Vgoal) {
			g_thres_upp = g_thres;
		}
	} while (abs(Vratio - Vgoal) > 1e-4 && (g_thres_upp - g_thres_low) / (g_thres_low + g_thres_upp) > 1e-3 && itn++ < 50);

	// update densities according to new sensitivity 
	// use current g_thres to update rho(like newrho before)
	trySensMultiplier_kernel << <grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getInitRho(), grids[0]->getSens(), g_thres, params.design_step, params.damp_ratio, params.min_rho, grids[0]->getRho());
	cudaDeviceSynchronize();
	cuda_error_check;

	// update Heaviside densities
#ifdef ENABLE_HEAVISIDE
	projectDensities(beta);
#endif // ENABLE_HEAVISIDE

	para_minus_abs << <grid_size, block_size >> > (grids[0]->n_nodes(), rhoold, grids[0]->getInitRho(), rhoold);
	//para_minus << <grid_size, block_size >> > (grids[0]->n_nodes(), grids[0]->getInitRho(), grids[0]->getRho(), rhoold);
	cudaDeviceSynchronize();
	cuda_error_check;
	float* maxabs = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_rho() / 100);
	change[0] = parallel_maxabs(rhoold, maxabs, grids[0]->n_rho());
	change[1] = parallel_sum(rhoold, maxabs, grids[0]->n_rho()) / grids[0]->n_rho();

	std::cout << "test: " << change[0] << std::endl;
	std::cout << "test: " << change[1] << std::endl;

	cudaFree(rhoold);
	rhoold = nullptr;
	
	return g_thres;
}

__global__ void tryCSensMultiplier_kernel(
	int nc, const float* cijklist, float* c_sens, float c_thres, float step, float damp, float cijkmin, float cijkmax, float* newcijk) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;
	if (tid >= nc) return;

	//int eid = gV2E[7][tid];

	//if (eid == -1) { return; }

	float c = c_sens[tid];

	if (c > 0) c = 0;

	c = abs(c);

	float cijkold = cijklist[tid];

	float cijknew = cijkold * powf(c / c_thres, damp);

	cijknew = clamp(cijknew, cijkold - step, cijkold + step);

	cijknew = clamp(cijknew, cijkmin, cijkmax);

	// MARK[TODO]: move this to computation of rho
	// rhoknew = clamp(rhoknew, rhomin, 1.f);
	// if (gEflag[0][eid] & grid::Grid::Bitmask::mask_shellelement) rhonew = 1;

	newcijk[tid] = cijknew;
}

float updateCoeff(float Vgoal) {
	grids[0]->use_grid();

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_nodes(), 512);

	float Vratio = 2;

	float g_thres_low = 0;
	float g_thres_upp = 1;

	// compute old volume ratio
	double* sum = (double*)grid::Grid::getTempBuf(sizeof(double) * grids[0]->n_rho() / 100);
	double Vold = parallel_sum_d(grids[0]->getRho(), sum, grids[0]->n_rho()) / grids[0]->n_rho();

	std::cout << " test vold : " << Vold << std::endl;

	// compute maximal sensitivity
	float* maxdump = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_cijk() / 100);
	float g_max = parallel_maxabs(grids[0]->getCSens(), maxdump, grids[0]->n_cijk());
	std::cout << " test g_max : " << g_max << std::endl;

	g_thres_upp = g_max;

	printf("[sensitivity] max = %f\n", g_max);

	float g_thres = (g_thres_low + g_thres_upp) / 2;

	// iteration counter
	int itn = 0;

	// MARK[TODO] newcijk need to update to _gbuf.coeffs !!!
	float* coeff_copy = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_cijk());
	cudaMemcpy(coeff_copy, grids[0]->_gbuf.coeffs, sizeof(float) * grids[0]->n_cijk(), cudaMemcpyDeviceToDevice);
	init_array(grids[0]->_gbuf.coeffs, float{ 0 }, grids[0]->n_cijk());

	size_t free_mem, total_mem;
	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

	// bisection search sensitivity multiplier
	do {
		// update sensitivity threshold
		g_thres = (g_thres_low + g_thres_upp) / 2;

		printf("-- searching multiplier g = %4.4e", g_thres);

		//// MARK[TODO] newcijk need to update to _gbuf.coeffs !!!
		//float* coeff_copy = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_cijk());
		//cudaMemcpy(coeff_copy, grids[0]->_gbuf.coeffs, sizeof(float) * grids[0]->n_cijk(), cudaMemcpyDeviceToDevice);
		//init_array(grids[0]->_gbuf.coeffs, float{ 0 }, grids[0]->n_cijk());

		// update new coeff
		tryCSensMultiplier_kernel << <grid_size, block_size >> > (
			grids[0]->n_cijk(), coeff_copy, grids[0]->getCSens(), g_thres, params.design_step, params.damp_ratio, params.min_cijk, params.max_cijk, grids[0]->getCoeff());
		cudaDeviceSynchronize();
		cuda_error_check;

		//float* newrho = (float*)grid::Grid::getTempBuf(sizeof(float) * grids[0]->n_rho());

		//// update new rho
		//trySensMultiplier_kernel << <grid_size, block_size >> > (
		//	grids[0]->n_nodes(), grids[0]->getRho(), grids[0]->getSens(), g_thres, params.design_step, params.damp_ratio, params.min_rho, newrho);
		//cudaDeviceSynchronize();
		//cuda_error_check;

		// MARK[TODO] newcijk need to update to _gbuf.coeffs !!!
		
		// coeff 2 rho
		grids[0]->coeff2density();
		float* newrho = grids[0]->getRho();

		// compute new volume ratio
		Vratio = dump_array_sum(newrho, grids[0]->n_rho()) / grids[0]->n_rho();

		printf(", V = %f  goal %f\n", Vratio, Vgoal);

		if (Vratio > Vgoal) {
			g_thres_low = g_thres;
		}
		else if (Vratio < Vgoal) {
			g_thres_upp = g_thres;
		}
	} while (abs(Vratio - Vgoal) > 1e-4 && itn++ < 30);

	// update densities according to new sensitivity
	tryCSensMultiplier_kernel << <grid_size, block_size >> > (
		grids[0]->n_cijk(), grids[0]->getCoeff(), grids[0]->getCSens(), g_thres, params.design_step, params.damp_ratio, params.min_cijk, params.max_cijk, grids[0]->getCoeff());
	cudaDeviceSynchronize();
	cuda_error_check;

	grids[0]->coeff2density();
	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

	grid::Grid::clearBuf();
	cudaMemGetInfo(&free_mem, &total_mem);
	std::cout << "Free Memory: " << free_mem / (1024 * 1024) << " MB  |  Total Memory: " << total_mem / (1024 * 1024) << " MB" << std::endl;

	//// update densities according to new sensitivity
	//tryCSensMultiplier_kernel << <grid_size, block_size >> > (
	// grids[0]->n_nodes(), grids[0]->getRho(), grids[0]->getSens(), g_thres, params.design_step, params.damp_ratio, params.min_cijk, params.max_cijk, grids[0]->getRho());
	//cudaDeviceSynchronize();
	//cuda_error_check;

	return g_thres;
}



extern void matlab_utils_test(void);

void selfTest(void)
{
	printf("-- Self testing...\n");
	using namespace grid;

	std::vector<int> arr(10000, 0xaaaaaaaa);

	BitSAT<int> bits(arr);

	printf("-- host bits total = %d\n", bits.total());

	gBitSAT<int> gbits(bits._bitArray, bits._chunkSat);

	{
		std::vector<int> bitscheck(bits._bitArray.size());
		cudaMemcpy(bitscheck.data(), gbits._bitarray, sizeof(int) * bitscheck.size(), cudaMemcpyDeviceToHost);
		//printf("%p ->\n", gbits._bitarray);
		//for (int i = 0; i < bitscheck.size(); i++) printf("%d ", bitscheck[i]);
		//printf("\n");
	}

	devArray_t<int*, 1> gcount;
	cudaMalloc(&gcount[0], bits._bitArray.size() * BitCount<int>::value * sizeof(int));

	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, bits._bitArray.size() * BitCount<int>::value, 128);

	auto kernel = [=] __device__(int tid) {
		if (blockIdx.x == 0 && threadIdx.x == 0) {
			//printf("_bitarray = %p ; _chunksat = %p\n", gbits._bitarray, gbits._chunksat);
			//printf("firstOne = %d\n", ::firstOne<sizeof(int) * 8>::value);
		}
		gcount[0][tid] = gbits[tid];
	};
	traverse_noret << <grid_size, block_size >> > (bits._bitArray.size() * BitCount<int>::value, kernel);
	cudaDeviceSynchronize();
	cuda_error_check;

	std::vector<int> counts(bits._bitArray.size() * BitCount<int>::value);
	cudaMemcpy(counts.data(), gcount[0], sizeof(int) * bits._bitArray.size() * BitCount<int>::value, cudaMemcpyDeviceToHost);

	bool pass_test = true;
	for (int i = 0; i < counts.size(); i++) {
		//printf("[%d] : %d\n", i, counts[i]);
		if (counts[i] != bits[i]) {
			pass_test = false;
			break;
		}
	}

	gbits.destroy();
	gcount.destroy();

	cuda_error_check;

	matlab_utils_test();

	// test GraftArray
	{
		std::vector<int> hostbuf(10000);
		int* _bufdev;
		cudaMalloc(&_bufdev, sizeof(int)*hostbuf.size());
		int baselen = hostbuf.size();
		auto kern = [=] __device__(int tid) {
			int rl = baselen / 125;
			GraftArray<int, 25, 5> p(_bufdev, rl);
			int id[3] = { tid % rl, tid / rl % 5,tid / rl / 5 };
			p[id[2]][id[1]][id[0]] = tid;
		};
		size_t grid_size, block_size;
		make_kernel_param(&grid_size, &block_size, hostbuf.size(), 512);
		traverse_noret << <grid_size, block_size >> > (hostbuf.size(), kern);
		cudaDeviceSynchronize();
		cuda_error_check;
		cudaMemcpy(hostbuf.data(), _bufdev, sizeof(int) * hostbuf.size(), cudaMemcpyDeviceToHost);

		bool fail = false;
		for (int i = 0; i < hostbuf.size(); i++) {
			if (hostbuf[i] != i) {
				fail = true; break;
			}
		}
		if (fail) {
			printf("-- GraftArray test failed\n");
		}
		pass_test &= !fail;

		cudaFree(_bufdev);
	}

	//
	//MMA::test_mma();

	if (pass_test) {
		printf("-- Pass test\n");
	}
	else {
		printf("-- Test failed\n");
	}

}

// upload template matrix and power penalty coefficient
void uploadTemplateMatrix(void)
{
	double element_len = grids.elementLength();
	initTemplateMatrix(element_len, gpu_manager, params.youngs_modulu, params.poisson_ratio);
	const double* ke = getTemplateMatrixElements();
	cudaMemcpyToSymbol(gTemplateMatrix, ke, sizeof(gTemplateMatrix));
	cuda_error_check;

	// upload power penalty
	float power = params.power_penalty;
	cudaMemcpyToSymbol(power_penalty, &power, sizeof(power_penalty));
	cuda_error_check;
}

void setDEBUG(bool debug)
{
	int a = 0;
	if (debug) a = 1;
	cudaMemcpyToSymbol(gDEBUG, &a, sizeof(int));
}

__global__ void checkAjointKernel(int n_gsvertices, double mu, devArray_t<double*, 3> vdst) {
	int tid = blockDim.x * blockIdx.x + threadIdx.x;

	if (tid >= n_gsvertices) return;

	for (int i = 0; i < 3; i++) {
		double kku = gF[i][tid];
		double f = vdst[i][tid];
		double fworst = gFworst[i][tid];
		vdst[i][tid] = 2 * fworst - f - 2 * mu * kku;
	}
}

// supporst N^T lambda is in U,  uworst is in Uworst, Kuworst is in Fworst
bool checkAdjointVariable(void) {
	grids[0]->use_grid();
	devArray_t<double*, 3> vdst, uback;
	for (int i = 0; i < 3; i++) {
		cudaMalloc(&vdst[i], sizeof(double) * grids[0]->n_gsvertices);
		cudaMalloc(&uback[i], sizeof(double) * grids[0]->n_gsvertices);
	}
	// backup adjoint displacement
	grids[0]->v3_copy(grids[0]->getDisplacement(), uback._data);
	// KNlam in vdst
	grids[0]->applyK(grids[0]->getDisplacement(), grids[0]->getForce());
	grids[0]->v3_copy(grids[0]->getForce(), vdst._data);
	grids[0]->resetDirchlet(vdst._data);

	// KKu is in F
	grids[0]->v3_copy(grids[0]->getWorstForce(), grids[0]->getDisplacement());
	grids[0]->applyK(grids[0]->getDisplacement(), grids[0]->getForce());
	grids[0]->resetDirchlet(grids[0]->getForce());
	double mu = grids[0]->_keyvalues["mu"];
	
	size_t grid_size, block_size;
	make_kernel_param(&grid_size, &block_size, grids[0]->n_gsvertices, 512);
	checkAjointKernel<<<grid_size,block_size>>>(grids[0]->n_gsvertices, mu, vdst);
	cudaDeviceSynchronize();
	cuda_error_check;
	grids[0]->v3_toMatlab("vadj", vdst._data);
	bool vnorm = grids[0]->v3_norm(vdst._data);
	bool passCheck = vnorm < 1e-6;

	grids[0]->v3_copy(uback._data, grids[0]->getDisplacement());

	vdst.destroy();
	uback.destroy();

	return passCheck;
}


