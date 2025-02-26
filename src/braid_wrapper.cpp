#ifdef WITH_BRAID

#include "braid_wrapper.hpp"


myBraidVector::myBraidVector() {
  x = NULL;
}

myBraidVector::myBraidVector(int dim) {

    /* Allocate the Petsc Vector */
    VecCreate(PETSC_COMM_WORLD, &x);
    VecSetSizes(x, PETSC_DECIDE, dim);
    VecSetFromOptions(x);
    VecZeroEntries(x);
}


myBraidVector::~myBraidVector() {
  VecDestroy(&x);
}



myBraidApp::myBraidApp(MPI_Comm comm_braid_, double total_time_, int ntime_, TimeStepper* mytimestepper_, MasterEq* ham_, MapParam* config, Output* output_) 
          : BraidApp(comm_braid_, 0.0, total_time_, ntime_) {

  ntime = ntime_;
  total_time = total_time_;
  timestepper = mytimestepper_;
  mastereq = ham_;
  comm_braid = comm_braid_;
  output = output_;
  MPI_Comm_rank(comm_braid, &mpirank_braid);
  MPI_Comm_rank(PETSC_COMM_WORLD, &mpirank_petsc);
  MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);

  /* Init Braid core */
  core = new BraidCore(comm_braid_, this);

  /* Get and set Braid options */
  core->SetNRelax(-1, 1); // Number of CF-relaxations on all (-1) grids (0: F, 1: FCF, 2: FCFCF, etc)
  int printlevel = config->GetIntParam("braid_printlevel", 2);
  core->SetPrintLevel(printlevel);
  int maxlevels = config->GetIntParam("braid_maxlevels", 20);
  core->SetMaxLevels(maxlevels);
  int cfactor = config->GetIntParam("braid_cfactor", 5);
  core->SetCFactor(-1, cfactor);
  int maxiter = config->GetIntParam("braid_maxiter", 50);
  core->SetMaxIter( maxiter);
  double abstol = config->GetDoubleParam("braid_abstol", 1e-6);
  core->SetAbsTol( abstol );
  double reltol = config->GetDoubleParam("braid_reltol", 1e-4);
  core->SetRelTol( reltol );
  bool skip = (PetscBool) config->GetBoolParam("braid_skip", false);
  core->SetSkip( skip);
  bool fmg = (PetscBool) config->GetBoolParam("braid_fmg", false);
  if (fmg) core->SetFMG();

  core->SetNRelax(-1, 1);
  core->SetSeqSoln(0);


  /* Output options */
  accesslevel = config->GetIntParam("braid_accesslevel", 1);
  core->SetAccessLevel( accesslevel );
  // _braid_SetVerbosity(core->GetCore(), 1);

}

myBraidApp::~myBraidApp() {
  /* Delete the core, if drive() has been called */
  delete core;
}

int myBraidApp::getTimeStepIndex(const double t, const double dt){
  int ts = round(t / dt);
  return ts;
}


Vec myBraidApp::getStateVec(const double time) {
  Vec x = NULL;
  braid_BaseVector ubase;
  myBraidVector *u;

  int tindex = getTimeStepIndex(time, total_time/ ntime);

  if (time == total_time) {
    _braid_UGetLast(core->GetCore(), &ubase);
  }
  else _braid_UGetVectorRef(core->GetCore(), 0, tindex, &ubase);
  if (ubase != NULL) { // only true on ONE processor !!
    u = (myBraidVector *)ubase->userVector;
    x = u->x;
  }
  return x; // NULL ON ALL BUT ONE BRAID-PROCESSORS!
}


BraidCore* myBraidApp::getCore() { return core; }

int myBraidApp::printConvHistory(const char* filename){ 
  FILE* braidlog;
  int niter, ntime, nlevels;
  int cfactor;
  double dt;

  /* Get some general information from the core */
  core->GetNLevels(&nlevels);
  core->GetCFactor(&cfactor);

  /* Get braid's residual norms for all iterations */
  core->GetNumIter(&niter);
  double *norms     = new double[niter];
  core->GetRNorms(&niter, norms);

  /* Write log to file */
  if (mpirank_world == 0) {
    braidlog = fopen(filename, "w");
    fprintf(braidlog,"# ntime %d\n", (int) ntime);
    fprintf(braidlog,"# cfactor %d\n", (int) cfactor);
    fprintf(braidlog,"# nlevels %d\n", (int) nlevels);
    for (int i=0; i<niter; i++)
    {
      fprintf(braidlog, "%d  %1.14e\n", i, norms[i]);
    }
    fprintf(braidlog, "\n\n\n");
    fclose(braidlog);
  }

  delete [] norms;

  return 0;
}


void myBraidApp::InitGrids() {

  double* ta;
  _braid_Grid   *grid;
  int ilower, iupper;

  _braid_GetDistribution(core->GetCore(), &ilower, &iupper);
  _braid_GridInit(core->GetCore(), 0, ilower, iupper, &grid);
  ta = _braid_GridElt(grid, ta);
     for (int i = ilower; i <= iupper; i++)
     {
        ta[i-ilower] = 0.0 + (((braid_Real)i)/ntime)*(total_time);
     }
  _braid_InitHierarchy(core->GetCore(), grid, 0);
  _braid_InitGuess(core->GetCore(), 0);
  _braid_CoreElt(core->GetCore(), warm_restart) = 1;
}


braid_Int myBraidApp::Step(braid_Vector u_, braid_Vector ustop_, braid_Vector fstop_, BraidStepStatus &pstatus){
    double tstart, tstop;
    int tindex;
    int done;

    /* Cast input u to class definition */
    myBraidVector *u = (myBraidVector *)u_;


    /* Grab current time from XBraid */
    pstatus.GetTstartTstop(&tstart, &tstop);
    pstatus.GetTIndex(&tindex);
    pstatus.GetDone(&done); 
  
    // printf("\nBraid %d %f->%f \n", tindex, tstart, tstop);

#ifdef SANITY_CHECK
    // printf("Performing check Hermitian, Trace... \n");
    /* Sanity check. Be careful: This is costly! */
    if (tstop == total_time) 
    {
      printf("Trace check %f ...\n", tstop);
      PetscBool check;
      double tol = 1e-10;
      // StateIsHermitian(u->x, tol, &check);
      // if (!check) {
      //   printf("WARNING at t=%f: rho is not hermitian!\n", tstart);
      //   printf("\n rho :\n");
      //   VecView(u->x, PETSC_VIEWER_STDOUT_WORLD);
      //   exit(1);
      // }
      // else printf("IsHermitian check passed.\n");
      StateHasTrace1(u->x, tol, &check);
      if (!check) {
        printf("WARNING at t=%f: Tr(rho) is NOT one!\n", tstart);
        printf("\n rho :\n");
        VecView(u->x, PETSC_VIEWER_STDOUT_WORLD);
        exit(1);
      }
      else printf("Trace1 check passed.\n");
    }
#endif

    /* Timestepper  */

    /* Add penalty term */
    if (_braid_CoreElt(core->GetCore(), max_levels) == 1 && timestepper->gamma_penalty > 1e-13) {

      timestepper->penalty_integral += timestepper->penaltyIntegral(tstart, u->x);
      // printf("%f %.8f %.8f\n", tstart, weight, penalty_integral); 
    }

    /* Evolve solution forward from tstart to tstop */
    timestepper->evolveFWD(tstart, tstop, u->x);

  return 0;
}


braid_Int myBraidApp::Residual(braid_Vector u_, braid_Vector r_, BraidStepStatus &pstatus){ return 0; }

braid_Int myBraidApp::Clone(braid_Vector u_, braid_Vector *v_ptr){ 

  /* Cast input braid vector to class vector definition */
  myBraidVector *u = (myBraidVector *)u_;

  /* Allocate a new vector */
  myBraidVector* ucopy = new myBraidVector();

  /* First duplicate storage, then copy values */
  VecDuplicate(u->x, &(ucopy->x));
  VecCopy(u->x, ucopy->x);

  /* Set the return pointer */
  *v_ptr = (braid_Vector) ucopy;

  return 0; 
}

braid_Int myBraidApp::Init(braid_Real t, braid_Vector *u_ptr){ 

  /* Get dimensions */
  int nreal = 2 * mastereq->getDim();

  /* Create the vector */
  myBraidVector *u = new myBraidVector(nreal);

  /* Return vector to braid */
  *u_ptr = (braid_Vector) u;
  
  return 0; 
}


braid_Int myBraidApp::Free(braid_Vector u_){ 
  myBraidVector *u = (myBraidVector *)u_;
  delete u;
  return 0; 
}


braid_Int myBraidApp::Sum(braid_Real alpha, braid_Vector x_, braid_Real beta, braid_Vector y_){ 
  myBraidVector *x = (myBraidVector *)x_;
  myBraidVector *y = (myBraidVector *)y_;

    int dim;
    const PetscScalar *x_ptr;
    PetscScalar *y_ptr;

    PetscInt ilow, iupp;
    VecGetOwnershipRange(x->x, &ilow, &iupp);

    VecGetArrayRead(x->x, &x_ptr);
    VecGetArray(y->x, &y_ptr);
    for (int i = ilow; i < iupp; i++) {
        y_ptr[i-ilow] = alpha * x_ptr[i-ilow] + beta * y_ptr[i-ilow];

    }
    VecRestoreArray(y->x, &y_ptr);
    VecRestoreArrayRead(x->x, &x_ptr);


  return 0; 
}


braid_Int myBraidApp::SpatialNorm(braid_Vector u_, braid_Real *norm_ptr){ 
  myBraidVector *u = (myBraidVector *)u_;

  double norm;
  VecNorm(u->x, NORM_2, &norm);

  *norm_ptr = norm;
  return 0; 
}


braid_Int myBraidApp::Access(braid_Vector u_, BraidAccessStatus &astatus){ 
  myBraidVector *u = (myBraidVector *)u_;
  int done = 0;
  double t;
  int timestep;

  /* Get time information */
  astatus.GetT(&t);
  astatus.GetDone(&done);
  astatus.GetTIndex(&timestep);
  if (!done) return 0;

  /* Don't print first time step. */
  // if (t == 0.0) return 0;

  if (accesslevel > 0) {
    output->writeDataFiles(timestep, t, u->x, mastereq);
  }

  return 0; 
}


braid_Int myBraidApp::BufSize(braid_Int *size_ptr, BraidBufferStatus &bstatus){ 

  *size_ptr = 2 * mastereq->getDim() * sizeof(double);
  return 0; 
}


braid_Int myBraidApp::BufPack(braid_Vector u_, void *buffer, BraidBufferStatus &bstatus){ 
  
  /* Cast input */
  myBraidVector *u = (myBraidVector *)u_;
  double* dbuffer = (double*) buffer;

  /* Get locally owned range */
  PetscInt xlo, xhi;
  VecGetOwnershipRange(u->x, &xlo, &xhi);

  /* Copy real and imaginary values into the buffer */
  for (PetscInt i=0; i < 2*mastereq->getDim(); i++)
  {
    if (xlo <= i && i < xhi) { // if stored on this proc
      double val;
      VecGetValues(u->x, 1, &i, &val);
      dbuffer[i] = val;
    }
  }

  /* Set size */
  int size =  2 * mastereq->getDim() * sizeof(double);
  bstatus.SetSize(size);

  return 0; 
}


braid_Int myBraidApp::BufUnpack(void *buffer, braid_Vector *u_ptr, BraidBufferStatus &bstatus){ 

  /* Cast buffer to double */
  double* dbuffer = (double*) buffer;

  /* Allocate a new vector */
  int dim = 2 * mastereq->getDim();
  myBraidVector *u = new myBraidVector(dim);

  /* Get locally owned range */
  PetscInt xlo, xhi;
  VecGetOwnershipRange(u->x, &xlo, &xhi);

  /* Copy real and imaginary values from the buffer */
  for (int i=0; i < 2*mastereq->getDim(); i++)
  {
    if (xlo <= i && i < xhi) { // if stored on this proc
      double val;
      VecSetValues(u->x, 1, &i, &(dbuffer[i]), INSERT_VALUES);
    }
  }

  VecAssemblyBegin(u->x);
  VecAssemblyEnd(u->x);

  /* Return vector to braid */
  *u_ptr = (braid_Vector) u;

  return 0; 
}

void myBraidApp::PreProcess(int iinit, const Vec rho_t0, double jbar){

  /* Pass initial condition to braid */
  setInitCond(rho_t0);

  /* Reset penalty integral */
  timestepper->penalty_integral = 0.0;
  Jbar = jbar;

  /* Open output files */
  if (accesslevel > 0 ) output->openDataFiles("rho", iinit);
}


Vec myBraidApp::PostProcess() {

  braid_BaseVector ubase;
  myBraidVector *u;
  int maxlevels = _braid_CoreElt(core->GetCore(), max_levels);

  /* If multilevel solve: Sweep over all points to access TODO: CHECK IF THIS IS NEEDED */
  // if (maxlevels > 1) {
  //   printf("Now FCRelax...\n");
  //   _braid_CoreElt(core->GetCore(), done) = 1;
  //   _braid_FCRelax(core->GetCore(), 0);
  // }

  /* Close output files */
  output->closeDataFiles();

  /* Print XBraid statistics */
  // core->PrintStats();

  return getStateVec(total_time);// this returns NULL for all but the last processors! 
}


double myBraidApp::Drive() { 
  
  int nreq = -1;
  double norm;


  _braid_CoreElt(core->GetCore(), done) = 0;
  core->Drive();
  core->GetRNorms(&nreq, &norm);

  // braid_printConvHistory(braid_core, "braid.out.log");

  return norm; 
}


/* ================================================================*/
/* Adjoint Braid App */
/* ================================================================*/


myAdjointBraidApp::myAdjointBraidApp(MPI_Comm comm_braid_, double total_time_, int ntime_, TimeStepper* mytimestepper_, MasterEq* ham_, MapParam* config, BraidCore *Primalcoreptr_, Output* output_)
        : myBraidApp(comm_braid_, total_time_, ntime_, mytimestepper_, ham_, config, output_) {

  /* Store the primal core */
  primalcore = Primalcoreptr_;

  /* Ensure that primal core stores all points */
  primalcore->SetStorage(0);

  /* Store all points for adjoint, needed for penalty integral term */
  /* Alternatively, recompute the adjoint states during PostProcessing for computing gradient */
  // core->SetStorage(0);
  

  /* Revert processor ranks for solving adjoint */
  core->SetRevertedRanks(1);

}

myAdjointBraidApp::~myAdjointBraidApp() {
}


int myAdjointBraidApp::getPrimalIndex(int ts) { 
  return ntime - ts; 
}

braid_Int myAdjointBraidApp::Step(braid_Vector u_, braid_Vector ustop_, braid_Vector fstop_, BraidStepStatus &pstatus) {

  myBraidVector *u = (myBraidVector *)u_;

  double tstart, tstop;
  int ierr;
  int tindex;
  int level, done;
  bool compute_gradient;

  /* Update gradient only when done */
  pstatus.GetLevel(&level);
  pstatus.GetDone(&done);
  if (done){
    compute_gradient = true;
  }
  else {
    compute_gradient = false;
  }

  /* Grab current time from XBraid  */
  pstatus.GetTstartTstop(&tstart, &tstop);
  pstatus.GetTIndex(&tindex);

  double dt = tstop - tstart;
  // printf("\n %d: Braid %d %f->%f, dt=%f \n", mpirank, tindex, tstart, tstop, dt);

  /* Timestepper */

  /* Get original time */
  double tstart_orig = total_time - tstart;
  double tstop_orig  = total_time - tstop;

  /* Get uprimal at tstop_orig */
  myBraidVector *uprimal_tstop;
  braid_BaseVector ubaseprimal_tstop;
  int tstop_orig_id  = getTimeStepIndex(tstop_orig, total_time/ntime);
  _braid_UGetVectorRef(primalcore->GetCore(), 0, tstop_orig_id, &ubaseprimal_tstop);
  if (ubaseprimal_tstop == NULL) printf("ubaseprimal_tstop is null!\n");
  uprimal_tstop  = (myBraidVector*) ubaseprimal_tstop->userVector;

  /* Reset gradient, if neccessary */
  if (!done) VecZeroEntries(timestepper->redgrad);

  /* Evolve u backwards in time and update gradient */
  timestepper->evolveBWD(tstop_orig, tstart_orig, uprimal_tstop->x, u->x, timestepper->redgrad, compute_gradient);

  /* Derivative of penalty objective */
  if (_braid_CoreElt(core->GetCore(), max_levels) == 1 && timestepper->gamma_penalty > 1e-13) {
    timestepper->penaltyIntegral_diff(tstop_orig, uprimal_tstop->x, u->x, Jbar);
  }


  return 0;  
}


braid_Int myAdjointBraidApp::Init(braid_Real t, braid_Vector *u_ptr) {

  /* Allocate the adjoint vector and set to zero */
  myBraidVector *u = new myBraidVector(2*mastereq->getDim());

  /* Reset the reduced gradient */
  VecZeroEntries(timestepper->redgrad); 


  /* Return new vector to braid */
  *u_ptr = (braid_Vector) u;

  return 0;
}


void myAdjointBraidApp::PreProcess(int iinit, const Vec rho_t0_bar, double jbar){

  /* Pass initial condition to braid */
  setInitCond(rho_t0_bar);

  /* Reset penalty integral */
  timestepper->penalty_integral = 0.0;
  Jbar = jbar;

  /* Reset the reduced gradient */
  VecZeroEntries(timestepper->redgrad); 

  /* Open output files for adjoint */
  // if (accesslevel > 0) output->openDataFiles("rho_adj", iinit);
}



Vec myAdjointBraidApp::PostProcess() {

  int maxlevels;
  maxlevels = _braid_CoreElt(core->GetCore(), max_levels);

  if (maxlevels > 1) {
    // printf("Error: Gradient computation with braid_maxlevels>1 is wrong. Neex to sweep over all points here!\n");
    // exit(1);
    /* Sweep over all points to collect the gradient */
    VecZeroEntries(timestepper->redgrad);
    _braid_CoreElt(core->GetCore(), done) = 1;
    _braid_FCRelax(core->GetCore(), 0);
  }

  /* Close output files */
  output->closeDataFiles();

  return 0;
}

void myBraidApp::setInitCond(const Vec rho_t0){
  braid_BaseVector ubase;
  int size;
  Vec x;
      
  /* Get braids vector at t == 0  and copy initial condition */
  _braid_UGetVectorRef(core->GetCore(), 0, 0, &ubase);
  if (ubase != NULL)  // only true on one processor (first, if primal app; last, if adjoint app)
  {
    x = ((myBraidVector *)ubase->userVector)->x;

    /* Copy initial condition into braid's vector */
    VecCopy(rho_t0, x);
  }
}

#endif