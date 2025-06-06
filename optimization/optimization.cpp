#include "optimization.h"
#include "projection.h"
#include "matlab_utils.h"
#include "binaryIO.h"
#include "tictoc.h"
#include <cstdlib>
#include "mma_t.h"


gpu_manager_t gpu_manager;

grid::HierarchyGrid grids(gpu_manager);

Parameter params;

void buildGrids(const std::vector<float>& coords, const std::vector<int>& trifaces, Mesh& inputmesh) 
{
	//grids.lambdatest();

	grids.set_prefer_reso(params.gridreso);
	grids.set_skip_layer(true);
	grids.genFromMesh(coords, trifaces, inputmesh);
}

void logParams(std::string file, std::string version_str, int argc, char** argv)
{
	std::ofstream ofs(grids.getPath(file));
	ofs << "[version] " << version_str << std::endl;
	for (int i = 0; i < argc; i++) {
		ofs << argv[i] << " ";
	}
	ofs << std::endl;
	ofs.close();
}

void setParameters(
	float volRatio, float volDecrease, float designStep, float filterRadi, float dampRatio, float powerPenal,
	float min_density, int gridreso, float youngs_modulu, float poisson_ratio, float shell_width,
	bool logdensity, bool logcompliance, int partitionx, int partitiony, int partitionz, int spline_order, float min_coeff, float max_coeff, float default_print_angle, float opt_print_angle
) {
	params.volume_ratio = volRatio;
	params.volume_decrease = volDecrease;
	params.design_step = designStep;
	params.filter_radius = filterRadi;
	params.damp_ratio = dampRatio;
	params.power_penalty = powerPenal;
	params.min_rho = min_density;
	params.gridreso = gridreso;
	params.youngs_modulu = youngs_modulu;
	params.poisson_ratio = poisson_ratio;
	params.partitionx = partitionx;
	params.partitiony = partitiony;
	params.partitionz = partitionz;
	params.spline_order = spline_order;
	params.min_cijk = min_coeff;
	params.max_cijk = max_coeff;
	params.isosurface_value = (min_coeff + max_coeff) / 2;
	grids.set_min_density(min_density);
	grids.set_spline_coeff_bound(min_coeff, max_coeff);
	grids.set_spline_order(spline_order);
	grids.set_spline_partition(partitionx, partitiony, partitionz, spline_order);
	grids.set_shell_width(shell_width);
	grids.enable_logdensity(logdensity);
	grids.enable_logcompliance(logcompliance);
	grids.setPrintAngle(default_print_angle, opt_print_angle);
}

void setOutpurDir(const std::string& dirname)
{
	std::string outdir = dirname;

	char lastChar = *outdir.rbegin();
	if (lastChar != '\\' && lastChar != '/') {
		outdir.push_back('\\');
	}
	std::cout << "\033[32m-- Output path \033[0m \n  " << outdir << "\033[0m" << std::endl;

	if (std::filesystem::exists(outdir)) {
		std::cout << "Folder already exists." << std::endl;
	}
	else {
		if (std::filesystem::create_directory(outdir)) {
			std::cout << "Folder created successfully." << std::endl;
		}
		else {
			std::cout << "Failed to create folder." << std::endl;
		}
	}

	grids.setOutPath(outdir);
}

void setInputMesh(const std::string& inputmesh)
{
	std::string meshfile = inputmesh;
	grids.setMeshFile(meshfile);
}

void setWorkMode(const std::string& modestr)
{
	if (modestr == "nscf") {
		grids.setMode(no_support_constrain_force_direction);
	}
	else if (modestr == "nsff") {
		grids.setMode(no_support_free_force);
	}
	else if (modestr == "wscf") {
		grids.setMode(with_support_constrain_force_direction);
	}
	else if (modestr == "wsff") {
		grids.setMode(with_support_free_force);
	}
	else {
		printf("-- unsupported mode\n");
		exit(-1);
	}
}

void setSSMode(const std::string& modestr)
{
	// the first of the category is only the negative axis-z
	if (modestr == "p") {
		grids.setSSMode(grid::GlobalSSMode::p_norm_ss);
	} 
	else if (modestr == "p2") {
		grids.setSSMode(grid::GlobalSSMode::p_norm2_ss);
	}
	else if (modestr == "h")	{
		grids.setSSMode(grid::GlobalSSMode::h_function_ss);
	}
	else if (modestr == "h2") {
		grids.setSSMode(grid::GlobalSSMode::h_function2_ss);
	}
	else if (modestr == "oh") {
		grids.setSSMode(grid::GlobalSSMode::overhang_ss);
	}
	else if (modestr == "oh2") {
		grids.setSSMode(grid::GlobalSSMode::overhang2_ss);
	}
	else {
		printf("-- unsupported mode\n");
		exit(-1);
	}
}

void setDripMode(const std::string& modestr)
{
	if (modestr == "p") {
		grids.setDripMode(grid::GlobalDripMode::p_norm_drip);
	}
	else if (modestr == "p2") {
		grids.setDripMode(grid::GlobalDripMode::p_norm2_drip);
	}
	else if (modestr == "h") {
		grids.setDripMode(grid::GlobalDripMode::h_function_drip);
	}
	else if (modestr == "h2") {
		grids.setDripMode(grid::GlobalDripMode::h_function2_drip);
	}
	else if (modestr == "oh") {
		grids.setDripMode(grid::GlobalDripMode::overhang_drip);
	}
	else if (modestr == "oh2") {
		grids.setDripMode(grid::GlobalDripMode::overhang2_drip);
	}
	else if (modestr == "exp") {
		grids.setDripMode(grid::GlobalDripMode::exp_drip);
	}
	else if (modestr == "exp2") {
		grids.setDripMode(grid::GlobalDripMode::exp2_drip);
	}
	else {
		printf("-- unsupported mode\n");
		exit(-1);
	}
}

void solveFEM(void)
{
	double rel_res = 1;
	while (rel_res > 1e-4) {
		rel_res = grids.v_cycle();
	}
}

void matlab_utils_test(void) {
#ifdef ENABLE_MATLAB
	std::vector<Eigen::Triplet<double>>  triplist;
	for (int i = 0; i < 300; i++) {
		triplist.emplace_back(i * 2, i * 10, i);
	}

	Eigen::SparseMatrix<double> sp;

	sp.resize(4000, 4000);

	sp.setFromTriplets(triplist.begin(), triplist.end());

	eigen2ConnectedMatlab("sp", sp);

	getMatEngine().eval("a=nnz(sp);");

	Eigen::Matrix<double, -1, -1> spnnz;
	matlab2eigen("a", spnnz);

	if (spnnz(0, 0) > 90) {
		printf("-- matlab utils test passed\n");
	}
	else {
		printf("-- matlab utils test failed\n");
	}
#endif
}

double modifiedPM(void)
{
	// DEBUG
	//test_rigid_displacement();
	//exit(-1);

#if 1
	// generate random force
	grids[0]->randForce();
#else
	// pertubate force on last force
	grids[0]->pertubForce(0.8);
#endif

	// project force to balanced load on load region
	forceProject(grids[0]->getForce());
	
	// normalize force
	grids[0]->unitizeForce();

	// reset displacement
	grids[0]->reset_displacement();

	// DEBUG
	grids[0]->force2matlab("finit");

	getForceSupport(grids[0]->getForce(), grids[0]->getSupportForce());

	double fch = 1;

	int max_itn = 500;

	int itn = 0;

	double rel_res = 1;

	printf("\033[32m[ModiPM]\n\033[0m");

	//grids.test_vcycle();

	snippet::SerialChar<double> fchserial;

	bool failed = false;

	// 1e-5
	while (itn++ < max_itn && (fch > 1e-4 || rel_res > 1e-2)) {
#if 1
		// do one v_cycle
		rel_res = grids.v_cycle(1, 1);
#else
		if (fchserial.arising() && itn > 30) {
			rel_res = grids.v_halfcycle(1, 1, 1);
		}
		else {
			rel_res = grids.v_cycle(1, 1);
		}
#endif

		// if fch is arising, init itn
		//if (itn > 30 && fchserial.arising()) {
		//	itn -= 30;
		//	// clear arising serial
		//	fchserial.add(-std::numeric_limits<double>::infinity());
		//	printf("-- fch arising, itn ->%d\n", itn);
		//}

		// remove rigid displacement
		//grids[0]->displacement2matlab("u1");
#if 0
		if (!grids.hasSupport() && rel_res > 2) displacementProject(grids[0]->getDisplacement());
#else
		if (!grids.hasSupport()) displacementProject(grids[0]->getDisplacement());
#endif
		//grids[0]->displacement2matlab("u2");

		failed = rel_res > 1e4;

		if (failed) break;

		// project to balanced load on load region
		grids[0]->v3_copy(grids[0]->getDisplacement(), grids[0]->getForce());
		forceProject(grids[0]->getForce());

		// normalize projected force
		grids[0]->unitizeForce();

		// compute change of force
		fch = grids[0]->supportForceCh() / grids[0]->supportForceNorm();

		//grids[0]->force2matlab("f1");

		// check fch serial
		fchserial.add(fch);

		// update support force
		getForceSupport(grids[0]->getForce(), grids[0]->getSupportForce());

		// output residual information
		//printf("-- r_rel %6.2lf%%, fch %2.2lf%%  %s\n", rel_res * 100, fch * 100, fchserial.arising() ? "( + )" : "");
		printf("--[%d] r_rel %6.2lf%%, fch %2.2lf%%  \n", itn, rel_res * 100, fch * 100);

		// DEBUG
		//if (itn % 20 == 0) {
		//	grids.writeSupportForce(grids.getPath("fs"));
		//}
	}

	if (failed) {
		printf("-- modified PM failed\n");
		grids.writeSupportForce(grids.getPath("fserr"));
		grids.writeDisplacement(grids.getPath("uerr"));
		exit(-1);
	}

	// reduce relative residual 
	itn = 0;
#if 0
	while (rel_res > 1e-5 && itn++ < 50) { rel_res = grids.v_cycle(); printf("-- r_rel %6.2lf%%\n", rel_res * 100); }
#endif

	// DEBUG
	grids[0]->force2matlab("fworst");
	grids[0]->displacement2matlab("uworst");
	//grids.writeSupportForce(grids.getPath("fs"));

	//grids[0]->v3_copy(grids[0]->getForce(), grids[0]->getWorstForce());

	//grids[0]->v3_copy(grids[0]->getDisplacement(), grids[0]->getWorstDisplacement());

	double worstCompliance = grids[0]->compliance();

	if (isnan(worstCompliance)) { printf("\033[31m-- NaN occurred !\033[0m\n"); exit(-1); }

	grids[0]->_keyvalues["mu"] = worstCompliance;

	printf("-- Worst Compliance %6.3e\n", worstCompliance);

	return worstCompliance;
}

#if 0
double eigenCG(void)
{
	// f ( u ) = u ^T P u / u ^ T K u;
	double * p[3];
	double * g[3];
	double * g1[3];
	double * xa[3];
	double * ga[3];
	grids[0]->v3_create(p);
	grids[0]->v3_create(g);
	grids[0]->v3_create(g1);
	grids[0]->v3_create(xa);
	grids[0]->v3_create(ga);

	double** x = grids[0]->getDisplacement();

	// generate random x0
	grids[0]->v3_rand(x, -1, 1);

	auto pknorm = RayleighGradient(x, g);
	double pnorm = pknorm.first;
	double knorm = pknorm.second;
	
	grids[0]->v3_copy(g, p);

	int itn = 0;
	double res = 1;
	double gk2 = grids[0]->v3_dot(g, g);
	double newgk2 = gk2;

	while (res > 1e-6 & itn++ < 200) {

		double al = 0, au = 10;
		double a = 1;
		// line search
		auto h = [&](double t) {
			grids[0]->v3_copy(x, xa);
			grids[0]->v3_add(xa, t, p);
			double pn = Pnorm(xa);
			double kn = Knorm(xa);
			return pn / kn;
		};
		auto dh = [&](double t, double* gnew[3], double& pn, double& kn) {
			grids[0]->v3_copy(x, xa);
			grids[0]->v3_add(xa, t, p);
			auto pk = RayleighGradient(xa, gnew);
			pn = pk.first; kn = pk.second;
			return grids[0]->v3_dot(gnew, p);
		};

		double amax = 100;
		double c1 = 1e-1, c2 = 1 / 3;
		snippet::circle_array_t<double, 20> alist, hlist;
		alist[0] = 0; alist[1] = 1;
		double pn0, kn0;
		double dh0 = dh(alist[0], ga, pn0, kn0);
		double h0 = pn0 / kn0;
		hlist[0] = h0;
		for (int itn = 1; itn < 100; itn++) {
			double pn, kn;
			double dha = dh(alist[itn], ga, pn, kn);
			double ha = pn / kn;
			hlist[itn] = ha;

			auto zoom = [&](double aL, double aU) {
				double a;
				for (int itnn = 0; itnn < 100; itnn++) {
					// interpolate  a
					a = (aL + aU) / 2;

					// narrow interval
					double pn, kn;
					double dha = dh(a, ga, pn, kn);
					double ha = pn / kn;
					double hal = h(aL);
					if (ha > h0 + c1 * a * dh0 || ha >= hal) {
						aU = a;
					}
					else {
						if (abs(dha) <= -c2 * dh0) {
							return a;
						}
						if (dha*(aU - aL) > 0) {
							aU = aL;
						}
						aL = a;
					}
				}
				return a;
			};

			if (ha > h0 + c1 * alist[itn] * dh0 || (hlist[itn] >= hlist[itn - 1] && itn > 1)) {
				a = zoom(alist[itn - 1], alist[itn]);
				break;
			}
			if (abs(dha) <= -c2 * dh0) {
				a = alist[itn];
				break;
			}
			if (dha > 0) {
				a = zoom(alist[itn], alist[itn - 1]);
			}
			alist[itn + 1] = (a + amax) / 2;
		}
		
		
		// update x
		grids[0]->v3_add(x, a, p);

		// update direction
		auto pknorm = RayleighGradient(x, g1);
		newgk2 = grids[0]->v3_dot(g1, g1);
		grids[0]->v3_minus(g1, 1, g);
		double gk12 = grids[0]->v3_dot(g1, g);
		//newgk2 = grids[0]->v3_dot(g, g);
		double beta = gk12 / gk2;
		gk2 = newgk2;
	}
}
#endif

std::pair<double, double> RayleighGradient(double* u[3], double* g[3])
{
	grids[0]->v3_copy(u, g);
	forceProject(g);
	double pnorm = grids[0]->v3_dot(g, g);
	grids[0]->applyK(u, grids[0]->getForce());
	double knorm = grids[0]->v3_dot(u, grids[0]->getForce());
#if 0
	grids[0]->v3_add(2.0 / knorm, g, -2.0 * pnorm / (knorm*knorm), grids[0]->getForce());
#else
	grids[0]->v3_add(-2.0 / knorm, g, +2.0 * pnorm / (knorm*knorm), grids[0]->getForce());
#endif
	return { pnorm,knorm };
}

double Pnorm(double* u[3])
{
	getForceSupport(u, grids[0]->getSupportForce());
	std::vector<double> fs[3];
	for (int i = 0; i < 3; i++) {
		gpu_manager_t::download_buf(fs[i].data(), grids[0]->getSupportForce()[i], sizeof(double)*n_loadnodes());
	}

	double* fsp[3] = { fs[0].data(),fs[1].data(),fs[2].data() };
	forceProject(fsp);

	double s = 0;
#pragma omp parallel for reduction(+:s)
	for (int i = 0; i < n_loadnodes(); i++) {
		s += pow(fsp[0][i], 2) + pow(fsp[1][i], 2) + pow(fsp[2][i], 2);
	}

	return s;
}

double Knorm(double* u[3])
{
	grids[0]->applyK(u, grids[0]->getForce());
	double s = grids[0]->v3_dot(u, grids[0]->getForce());
	return s;
}

double inCompleteModiPM(double fch_thres /*= 1e-2*/)
{
	// generate random force
	grids[0]->randForce();
	// project force to balanced load on load region
	forceProject(grids[0]->getForce());
	// normalize force
	grids[0]->unitizeForce();
	// reset displacement
	grids[0]->reset_displacement();
	getForceSupport(grids[0]->getForce(), grids[0]->getSupportForce());

	double fch = 1;
	int max_itn = 50;
	int itn = 0;
	double rel_res = 1;
	printf("\033[32m[ModiPM]\n\033[0m");

	// 1e-5
	while (itn++<max_itn && fch>fch_thres) {
		// do one v_cycle
		rel_res = grids.v_cycle(1, 1);

		// project to balanced load on load region
		grids[0]->v3_copy(grids[0]->getDisplacement(), grids[0]->getForce());
		forceProject(grids[0]->getForce());

		// normalize projected force
		grids[0]->unitizeForce();

		// compute change of force
		fch = grids[0]->supportForceCh() / grids[0]->supportForceNorm();

		// update support force
		getForceSupport(grids[0]->getForce(), grids[0]->getSupportForce());

		// output residual information
		printf("-- r_rel %6.2lf%%, fch %2.2lf%%\n", rel_res * 100, fch * 100);
	}

	double worstCompliance = grids[0]->compliance();
	if (isnan(worstCompliance)) { printf("\033[31m-- NaN occurred !\033[0m\n"); exit(-1); }
	grids[0]->_keyvalues["mu"] = worstCompliance;
	printf("-- Worst Compliance %6.3e\n", worstCompliance);
	return worstCompliance;
}

double MGPSOR(void)
{
	// do some modipm cycle
	double c = inCompleteModiPM();
	
	double lambda = c;
	double fch = 1;
	int itn = 0;
	double fsnorm = 1;
	double* xk[3];
	grids[0]->v3_create(xk);

	while (itn++ < 200 && fch > 1e-4 || itn < 3) {
		// backup old xk
		grids[0]->v3_copy(grids[0]->getDisplacement(), xk);

		// f = k * x
		grids[0]->applyK(grids[0]->getDisplacement(), grids[0]->getForce());

		// c = x * K * x
		c = grids[0]->v3_dot(grids[0]->getDisplacement(), grids[0]->getForce());

		// compute Px
		forceProject(grids[0]->getDisplacement());

		// compute support force norm
		fsnorm = grids[0]->v3_norm(grids[0]->getDisplacement());
		lambda = pow(fsnorm, 2) / c;

		// compute Eigen residual
		grids[0]->v3_add(-lambda, grids[0]->getForce(), 1, grids[0]->getDisplacement());
		grids[0]->force2matlab("feig");

		// compute force change
		grids[0]->v3_normalize(grids[0]->getDisplacement());
		fch = grids[0]->supportForceCh(grids[0]->getDisplacement());
		getForceSupport(grids[0]->getDisplacement(), grids[0]->getSupportForce());

		// reset displacement
		grids[0]->reset_displacement();

		// Preconditioned SOR
		double rel_res = 1;
		int vit = 0;
		while (rel_res > 1e-1 && vit++ < 3) {
			rel_res = grids.v_cycle();
		}
		grids[0]->displacement2matlab("u");
		grids[0]->residual2matlab("r");
		
		grids[0]->v3_add(grids[0]->getDisplacement(), 1, xk);
		printf("-- lam = %6.4e, fch = %6.2lf%%, r_rel = %6.2lf%%\n", lambda, fch * 100, rel_res * 100);
	}

	grids[0]->v3_destroy(xk);

	grids[0]->v3_scale(grids[0]->getDisplacement(), 1.0 / fsnorm);
	grids[0]->applyK(grids[0]->getDisplacement(), grids[0]->getForce());

	double c_worst = grids[0]->v3_dot(grids[0]->getDisplacement(), grids[0]->getForce());
	return c_worst;
}

double project_v_cycle(HierarchyGrid& grds) {
	//forceProject(grds[0]->getForce());

	for (int i = 0; i < grds.n_grid(); i++) {
		if (grds[i]->is_dummy()) continue;
		if (i > 0) {
			grds[i]->fineGrid->update_residual();
			//if (grds[i]->fineGrid->_layer == 0) forceProjectComplementary(grds[i]->fineGrid->getResidual());
			grds[i]->restrict_residual();
			grds[i]->reset_displacement();
		}
		if (i < grds.n_grid() - 1) {
			grds[i]->gs_relax();
		}
		else {
			grds[i]->solve_fem_host();
		}
	}

	for (int i = grds.n_grid() - 2; i >= 0; i--) {
		if (grds[i]->is_dummy()) continue;
		grds[i]->prolongate_correction();
		grds[i]->gs_relax();
	}

	grds[0]->force2matlab("f");
	grds[0]->update_residual();
	grds[0]->residual2matlab("r");
	grds[0]->displacement2matlab("u");

	forceProjectComplementary(grds[0]->getDisplacement());

	grds[0]->displacement2matlab("up");
	grds[0]->update_residual();

	grds[0]->residual2matlab("rp");
	forceProjectComplementary(grds[0]->getResidual());
	grds[0]->residual2matlab("rpp");

	return grds[0]->relative_residual();
}

void findAdjointVariabls(void) {
	//grids[0]->v3_add(2, grids[0]->getDisplacement(), -2 * grids[0]->_keyvalues["mu"], grids[0]->getWorstForce());
}

void optimization(void) {
	// allocated total size
	printf("[GPU] Total Mem :  %4.2lfGB\n", double(gpu_manager.size()) / 1024 / 1024 / 1024);

	grids.testShell();

	initDensities(params.volume_ratio);

	// DEBUG
	setDEBUG(false);
	grids[0]->eidmap2matlab("eidmap");
	grids[0]->vidmap2matlab("vidmap");

	grids[0]->randForce();

	//float Vgoal = 1;
	float Vgoal = params.volume_ratio;

	int itn = 0;

	snippet::converge_criteria stop_check(1, 2, 5e-3);

	std::vector<double> cRecord, volRecord;

	std::vector<double> tRecord;

	double Vc = Vgoal - params.volume_ratio;

	while (itn++ < 100) {
		printf("\n* \033[32mITER %d \033[0m*\n", itn);

		Vgoal *= (1 - params.volume_decrease);

		Vc = Vgoal - params.volume_ratio;

		if (Vgoal < params.volume_ratio) Vgoal = params.volume_ratio;

		// update numeric stencil after density changed
		update_stencil();

		// solve worst displacement by modified power method
		auto t0 = tictoc::getTag();
#if 1
		double c_worst = modifiedPM();
#else
		double c_worst = MGPSOR();
#endif
		auto t1 = tictoc::getTag();
		tRecord.emplace_back(tictoc::Duration<tictoc::ms>(t0, t1));

		grids.writeSupportForce(grids.getPath(snippet::formated("iter%d_fs", itn)));

		cRecord.emplace_back(c_worst); volRecord.emplace_back(Vgoal);

		if (stop_check.update(c_worst, &Vc) && Vgoal <= params.volume_ratio + 1e-3) break;

		grids.log(itn);
		// compute adjoint variables
		//findAdjointVariabls();

		// compute sensitivity
		computeSensitivity();

		// update density
		updateDensities(Vgoal);

		bio::write_vector(grids.getPath("crec_iter"), cRecord);
		bio::write_vector(grids.getPath("vrec_iter"), volRecord);
		bio::write_vector(grids.getPath("trec_iter"), tRecord);

		// DEBUG
		if (itn % 5 == 0) {
			grids.writeDensity(grids.getPath("out.vdb"));
			grids.writeSensitivity(grids.getPath("sens.vdb"));
		}
	}

	printf("\n=   finished   =\n");

	// write result density field
	grids.writeDensity(grids.getPath("out.vdb"));

	// write worst compliance record during optimization
	bio::write_vector(grids.getPath("cworst"), cRecord);

	// write volume record during optimization
	bio::write_vector(grids.getPath("vrec"), volRecord);

	// write time cost record during optimization
	bio::write_vector(grids.getPath("trec"), tRecord);

	// write last worst f and u
	grids.writeSupportForce(grids.getPath("flast"));
	grids.writeDisplacement(grids.getPath("ulast"));
}

// spline + newss(paper3) + uncertain
void optimization_ss(void) {
	printf("\033[33mOptimization with new self-supporting constraint... \n\033[0m");
	//printf("%s Optimization with new self-supporting constraint...  %s\n", GREEN, RESET);
	
	// allocated total size
	printf("[GPU] Total Mem :  %4.2lfGB\n", double(gpu_manager.size()) / 1024 / 1024 / 1024);

	grids.testShell();
	// 
	grids.writeNodePos(grids.getPath("nodepos"), *grids[0]);
	// generate element nodes (which the spline backgrund nodes)
	grids.writeElementPos(grids.getPath("elepos"), *grids[0]);
	grids[0]->uploadBgEle();
	grids[0]->uploadBgEleSymbol();

	grids[0]->randForce();

#if 1
	// MARK: ADD user-defined input	
	//std::cout << "--TEST volume ratio: " << params.volume_ratio  << "..."<< std::endl;
	initCoeffs(params.volume_ratio);
	grids[0]->set_spline_knot_series();
	grids.writeCoeff(grids.getPath("coeff"));
	grids[0]->set_spline_knot_infoSymbol();
	grids[0]->uploadCoeffsSymbol();
	grids[0]->coeff2density();
#elif 0	
	initDensities(params.volume_ratio);
#else
	initDensities(1);
	float Vgoal = 1;
#endif

#ifdef ENABLE_HEAVISIDE
	float para_beta = 4;
	float change_tol = 1e-3;
	float change[2] = { 0.f };
	//grids[0]->rho2matlab("rhopj0");
	//projectDensities(para_beta);

	//grids[0]->initrho2matlab("rhoinit");
	//grids[0]->rho2matlab("rhopj");
#endif

	float Vgoal = params.volume_ratio;

	int itn = 0;

	snippet::converge_criteria stop_check(1, 10, 5e-4);

	std::vector<double> cRecord, volRecord, ssRecord, dripRecord;
	double* con_value;

	std::vector<double> tRecord, testRecord;

	double Vc = Vgoal - params.volume_ratio;

	int n_constraint = 1;

#ifdef ENABLE_SELFSUPPORT
	n_constraint = 2;
#endif

#ifdef ENABLE_DRIP
	n_constraint = 3;
#endif

	con_value = new double[n_constraint];

	// MMA
	MMA::mma_t mma(grids[0]->n_cijk(), n_constraint);
	mma.init(params.min_cijk, 1);
	float sensScale = 1e6;
	float volScale = 1e3;
	float SSScale = 1e3;
	float dripScale = 1e5;
	gv::gVector dv(grids[0]->n_cijk(), volScale / grids[0]->n_cijk());
	gv::gVector v(1, volScale * (1 - params.volume_ratio));

	gv::gVector gval(n_constraint);
	std::vector<gv::gVector> gdiffval(n_constraint);
	std::vector<float*> gdiff(n_constraint);
	for (int i = 0; i < n_constraint; i++) {
		gdiffval[i] = gv::gVector(grids[0]->n_cijk());
		gdiff[i] = gdiffval[i].data();
	}

	while (itn++ < 100) {
		printf("\n* \033[32mITER %d \033[0m*\n", itn);

		Vgoal *= (1 - params.volume_decrease);
		Vc = Vgoal - params.volume_ratio;
		if (Vgoal < params.volume_ratio) Vgoal = params.volume_ratio;

		//grids[0]->coeff2matlab("coeff_2");
		if (itn > 1)
		{
			// set spline_coeff from mma (cpu2gpu)
			TestSuit::setCoeff(mma.get_x().data());
			// update coeff 2 density		
			grids[0]->coeff2density();
		}
		grids[0]->coeff2matlab("coeff_1");

#ifdef ENABLE_HEAVISIDE
		projectDensities(para_beta);
		//grids[0]->initrho2matlab("rhoinit");
		grids[0]->rho2matlab("rhopj");
#endif

		// compute volume 
		double vol = grids[0]->volumeRatio();
		std::cout << "[TEST]: volume test " << vol << std::endl;
		v[0] = volScale * (vol - params.volume_ratio);
		con_value[0] = vol;

		double ss_value = 0.0;
		double drip_value = 0.0;
		initSSCSens(float{ 0 });
		initDripCSens(float{ 0 });

		// update numeric stencil after density changed
		update_stencil();

		// solve worst displacement by modified power method
		auto t0 = tictoc::getTag();
#if 1
		double c_worst = modifiedPM();
#else
		double c_worst = MGPSOR();
#endif
		auto t1 = tictoc::getTag();
		tRecord.emplace_back(tictoc::Duration<tictoc::ms>(t0, t1));

		grids.writeSupportForce(grids.getPath(snippet::formated("iter%d_fs", itn)));

		printf("-- c_worst = %6.4e  v = %4.3lf \n", c_worst, vol);
		if (isnan(c_worst) || abs(c_worst) < 1e-11) { printf("\033[31m-- Error compliance\033[0m\n"); exit(-1); }
		cRecord.emplace_back(c_worst); volRecord.emplace_back(vol);

#ifdef ENABLE_SELFSUPPORT
		//if (grids.areALLCoeffsEqual())
		//{
		//	std::cout << "\033[34m-- [No need extra operation to the background elements ] --\033[0m" << std::endl;
		//	initSSCSens(float{ 0 });
		//	ss_value = 1.0;
		//}
		//else
		//{
		if (itn > 200)
		{
			SSScale = 1e8;
		}
		else if (itn > 125)
		{
			SSScale = 5e7;
		}
		else if (itn > 100)
		{
			SSScale = 1e7;
		}
		else if (itn > 75)
		{
			SSScale = 1e6;
		}
		else if (itn > 50)
		{
			SSScale = 1e5;
		}
		else if (itn > 25)
		{
			volScale = volScale;
			SSScale = 1e4;
		}
		std::cout << "\033[34m-- [Deal with background points] --\033[0m" << std::endl;
		auto t2 = tictoc::getTag();
		deal_background_points(para_beta);	
		auto t3 = tictoc::getTag();
		testRecord.emplace_back(tictoc::Duration<tictoc::ms>(t2, t3));
		printf("-- self-supporting time = %6.4e  \n", tictoc::Duration<tictoc::ms>(t2, t3));

		ss_value = grids[0]->global_bg_selfsupp_constraint();
		std::cout << "--[TEST] SS value : " << ss_value << std::endl;
		//}
		con_value[1] = ss_value;
		ssRecord.emplace_back(ss_value);
#endif
		
#ifdef ENABLE_DRIP
		//if (grids.areALLCoeffsEqual())
		//{
		//	std::cout << "\033[34m-- [Same Coeff cannot extra surface points] --\033[0m" << std::endl;
		//	initDripCSens(float{ 0 });
		//	drip_value = 0.0;
		//}
		//else
		//{
			if (itn > 160)
			{
				dripScale = 1e9;
			}
			else if (itn > 120)
			{
				dripScale = 1e8;
			}
			else if (itn > 80)
			{
				dripScale = 1e7;
			}
			else if (itn > 40)
			{
				dripScale = 1e6;
			}
			//std::cout << "\033[34m-- [Deal with surface points] --\033[0m" << std::endl;
			//deal_surface_points(para_beta);

			drip_value = grids[0]->global_bg_drip_constraint();
			std::cout << "--[TEST] DRIP value : " << drip_value << std::endl;
		//}
		con_value[2] = drip_value;
		dripRecord.emplace_back(drip_value);
#endif

		// ss_value range to be adaptive
		if (stop_check.update(c_worst, con_value) && Vgoal <= params.volume_ratio && ss_value <= 1e-6 ) break;
		grids.log(itn);
		// compute adjoint variables
		//findAdjointVariabls();

		// compute sensitivity
#ifdef ENABLE_HEAVISIDE
		computeSensitivity2(para_beta);
#else
		computeSensitivity();
#endif

		TestSuit::scaleVector(grids[0]->getCSens(), grids[0]->n_cijk(), sensScale);
		TestSuit::scaleVector(grids[0]->getVolCSens(), grids[0]->n_cijk(), volScale);
		gpu_manager_t::pass_dev_buf_to_matlab("csensscale", grids[0]->getCSens(), grids[0]->n_cijk());
		gpu_manager_t::pass_dev_buf_to_matlab("volcsensscale", grids[0]->getVolCSens(), grids[0]->n_cijk());
		gdiff[0] = grids[0]->getVolCSens();
		gval[0] = volScale * (vol - params.volume_ratio);                
		std::cout << "-- TEST gv[0] : " << gval[0] << std::endl;

#ifdef ENABLE_SELFSUPPORT
		TestSuit::scaleVector(grids[0]->getSSCSens(), grids[0]->n_cijk(), SSScale);
		gpu_manager_t::pass_dev_buf_to_matlab("sscsensscale", grids[0]->getSSCSens(), grids[0]->n_cijk());
		gdiff[1] = grids[0]->getSSCSens();
		float ss_goal = 0.f;
		//if (ss_value < 1e-3) {
		//	ss_goal = 2e-4;
		//}
		gval[1] = SSScale * (ss_value - ss_goal);
		std::cout << "-- TEST gv[1] : " << gval[1] << std::endl;
#endif
		
#ifdef ENABLE_DRIP
		TestSuit::scaleVector(grids[0]->getDripCSens(), grids[0]->n_cijk(), dripScale);		
		gpu_manager_t::pass_dev_buf_to_matlab("dripcsensscale", grids[0]->getDripCSens(), grids[0]->n_cijk());
		gdiff[2] = grids[0]->getDripCSens();
		gval[2] = dripScale * (drip_value);
		std::cout << "-- TEST gv[2] : " << gval[2] << std::endl;
#endif

		if (itn % 10 == 0)
		{
			grids.writeCSens(grids.getPath(snippet::formated("csens%d", itn)), grids[0]->getCSens(), grids[0]->n_cijk());
			grids.writeCSens(grids.getPath(snippet::formated("volcsens%d", itn)), grids[0]->getVolCSens(), grids[0]->n_cijk());
#ifdef ENABLE_SELFSUPPORT
			grids.writeCSens(grids.getPath(snippet::formated("sscsens%d", itn)), grids[0]->getSSCSens(), grids[0]->n_cijk());
#endif
#ifdef ENABLE_DRIP
			grids.writeCSens(grids.getPath(snippet::formated("dripcsens%d", itn)), grids[0]->getDripCSens(), grids[0]->n_cijk());
#endif
		}

		std::cout << "-- TEST Heaviside beta : " << para_beta << std::endl;

		mma.update(grids[0]->getCSens(), gdiff.data(), gval.data());

#ifdef ENABLE_HEAVISIDE
		if (itn % 20 == 0 && itn > 2 && para_beta < 8)
		{
			para_beta = para_beta * 2;
		}
#endif

		// DEBUG
		if (itn % 5 == 0) {
			grids.writeDensity(grids.getPath("out.vdb"));
			grids.writeSensitivity(grids.getPath("sens.vdb"));
		}

		bio::write_vector(grids.getPath("crec_iter"), cRecord);
		bio::write_vector(grids.getPath("vrec_iter"), volRecord);
		bio::write_vector(grids.getPath("trec_iter"), tRecord);
		bio::write_vector(grids.getPath("testrec_iter"), testRecord);
#ifdef ENABLE_SELFSUPPORT
		bio::write_vector(grids.getPath("ssrec_iter"), ssRecord);
#endif
#ifdef ENABLE_DRIP
		bio::write_vector(grids.getPath("driprec_iter"), dripRecord);
#endif
	}

	printf("\n=   finished   =\n");

	// write result density field
	grids.writeDensity(grids.getPath("out.vdb"));

	// write worst compliance record during optimization
	bio::write_vector(grids.getPath("cworst"), cRecord);
	// write volume record during optimization
	bio::write_vector(grids.getPath("vrec"), volRecord);
#ifdef ENABLE_SELFSUPPORT
	bio::write_vector(grids.getPath("ssrec"), ssRecord);
#endif
#ifdef ENABLE_DRIP
	bio::write_vector(grids.getPath("driprec"), dripRecord);
#endif

	// write time cost record during optimization
	bio::write_vector(grids.getPath("trec"), tRecord);
	// write last worst f and u
	grids.writeSupportForce(grids.getPath("flast"));
	grids.writeDisplacement(grids.getPath("ulast"));
}

grid::HierarchyGrid& getGrids(void)
{
	return grids;
}

void setBoundaryCondition(std::function<bool(double[3])> fixarea, std::function<bool(double[3])> loadarea, std::function<Eigen::Matrix<double, 3, 1>(double[3])> loadforce)
{
	grids._inFixedArea = fixarea;
	grids._inLoadArea = loadarea;
	grids._loadField = loadforce;
}

void initDensities(double rho)
{
#if 1
	grids[0]->init_rho(rho);
#else
	float* rh0_noise = new float[grids[0]->n_rho()];
	for (int i = 0; i < grids[0]->n_rho(); i++)
	{
		rh0_noise[i] = static_cast<float>(rand()) / RAND_MAX; // 生成0到1之间的随机数
	}
	grids[0]->init_rholist(rh0_noise);
	delete[] rh0_noise;
#endif
}

void initCoeffs(double coeff)
{
#if 1
	grids[0]->init_coeff(coeff);
#else
	float* coeff_noise = new float[grids[0]->n_cijk()];
	for (int i = 0; i < grids[0]->n_cijk(); i++)
	{
		coeff_noise[i] = coeff - static_cast<float>(rand() % 101) / 10000.0f; // 生成0到0.01之间的随机数
	}
	grids[0]->init_coefflist(coeff_noise);
	delete[] coeff_noise;
#endif
}

void initVolSens(double ratio)
{
	grids[0]->init_volsens(ratio);
}

void initVolCSens(double ratio)
{
	grids[0]->init_volcsens(ratio);
}

void initSSCSens(double sens)
{
	grids[0]->init_sscsens(sens);
}

void initDripCSens(double sens)
{
	grids[0]->init_dripcsens(sens);
}

void update_stencil(void)
{
	grids.update_stencil();
}

void test_rigid_displacement(void) {
	grids[0]->reset_residual();
	for (int n = 0; n < 3; n++) {
		for (int i = 0; i < 6; i++) {
			uploadRigidDisplacement(grids[0]->getDisplacement(), i);
			//grids[0]->reset_displacement();
			grids[0]->reset_force();
			grids[0]->update_residual();
			grids[0]->residual2matlab("r");
			printf("-- u_rigid %d res = %lf\n", i, grids[0]->residual());
		}
	}
}

void cgalTest(void) {
	grids.cgalTest();
}

void deal_surface_points(float beta) {
	auto t4 = tictoc::getTag();
	// update surface points in CPU
	grids[0]->generate_spline_surface_nodes(beta);
	// CPU 2 GPU
	grids[0]->uploadSurfacePoints();
	grids[0]->uploadSurfacePointsSymbol();
	grids[0]->uploadSurfaceSymbol2device();
	auto t5 = tictoc::getTag();
	printf("-- self-supporting time_1 = %6.4e  \n", tictoc::Duration<tictoc::ms>(t4, t5));

	t4 = tictoc::getTag();
	grids[0]->compute_spline_surface_point_normal();
	float direction = grids[0]->correct_spline_surface_point_normal_direction(beta); // mark false
	grids[0]->compute_selfsupp_flag_actual();
	grids[0]->compute_selfsupp_flag_virtual();
	t5 = tictoc::getTag();
	printf("-- self-supporting time_2 = %6.4e  \n", tictoc::Duration<tictoc::ms>(t4, t5));

	t4 = tictoc::getTag();
	grids[0]->compute_spline_selfsupp_constraint();
	grids[0]->compute_spline_selfsupp_constraint_dcoeff();
	// * 1 / num_constraint
	grids[0]->scale_spline_selfsupp_constraint_dcoeff();
	t5 = tictoc::getTag();
	printf("-- self-supporting time_3 = %6.4e  \n", tictoc::Duration<tictoc::ms>(t4, t5));

	t4 = tictoc::getTag();
	//grids[0]->compute_spline_drip_constraint();
	grids[0]->compute_spline_drip_constraint_test();
	//grids[0]->compute_spline_drip_constraint_dcoeff();
	grids[0]->compute_spline_drip_constraint_dcoeff_test();
	// * 1 / num_constraint
	grids[0]->scale_spline_drip_constraint_dcoeff();
	t5 = tictoc::getTag();
	printf("-- self-supporting time_4 = %6.4e  \n", tictoc::Duration<tictoc::ms>(t4, t5));
}

void deal_background_points(float beta) {
	grids[0]->uploadbgSymbol2device();

	grids[0]->compute_spline_background_ele_value();
	grids[0]->compute_spline_background_ele_normal();

	float direction = grids[0]->correct_spline_background_ele_normal_direction(beta);
	grids[0]->compute_background_selfsupp_flag_actual();
	grids[0]->compute_background_selfsupp_flag_virtual();

	grids[0]->compute_spline_background_selfsupp_constraint();
	grids[0]->compute_spline_bg_selfsupp_constraint_dcoeff();
	grids[0]->scale_spline_bg_selfsupp_constraint_dcoeff();

#ifdef ENABLE_DRIP
	grids[0]->compute_spline_background_drip_constraint();
	grids[0]->compute_spline_bg_drip_constraint_dcoeff();
	grids[0]->scale_spline_bg_drip_constraint_dcoeff();
#endif
}
