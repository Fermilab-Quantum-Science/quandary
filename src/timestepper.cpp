#include "timestepper.hpp"
#include "petscvec.h"

TimeStepper::TimeStepper() {
  dim = 0;
  mastereq = NULL;
}

TimeStepper::TimeStepper(MasterEq* mastereq_) {
  mastereq = mastereq_;
  dim = 2*mastereq->getDim();
}

TimeStepper::~TimeStepper() {}


ExplEuler::ExplEuler(MasterEq* mastereq_) : TimeStepper(mastereq_) {
  MatCreateVecs(mastereq->getRHS(), &stage, NULL);
  VecZeroEntries(stage);
}

ExplEuler::~ExplEuler() {
  VecDestroy(&stage);
}

void ExplEuler::evolveFWD(double tstart, double tstop, Vec x) {

  double dt = fabs(tstop - tstart);

   /* Compute A(tstart) */
  mastereq->assemble_RHS(tstart);
  Mat A = mastereq->getRHS(); 

  VecSet(x, 1.0);
  VecAssemblyBegin(x); 
  VecAssemblyEnd(x); 
    
  /* update x = x + hAx */
  MatMult(A, x, stage);
  VecAXPY(x, dt, stage);
}

void ExplEuler::evolveBWD(double tstop, double tstart, Vec x, Vec x_adj, Vec grad, bool compute_gradient){
  double dt = fabs(tstop - tstart);

  /* Add to reduced gradient */
  if (compute_gradient) {
    mastereq->computedRHSdp(tstop, x, x_adj, dt, grad);
  }

  /* update x_adj = x_adj + hA^Tx_adj */
  mastereq->assemble_RHS(tstop);
  Mat A = mastereq->getRHS(); 
  MatTranspose(A, MAT_INPLACE_MATRIX, &A);
  MatMult(A, x_adj, stage);
  VecAXPY(x_adj, dt, stage);

}

ImplMidpoint::ImplMidpoint(MasterEq* mastereq_) : TimeStepper(mastereq_) {

  /* Create and reset the intermediate vectors */
  MatCreateVecs(mastereq->getRHS(), &stage, NULL);
  VecDuplicate(stage, &stage_adj);
  VecDuplicate(stage, &rhs);
  VecDuplicate(stage, &rhs_adj);
  VecZeroEntries(stage);
  VecZeroEntries(stage_adj);
  VecZeroEntries(rhs);
  VecZeroEntries(rhs_adj);

  /* Create linear solver */
  KSPCreate(PETSC_COMM_WORLD, &linearsolver);

  /* Set options */
  KSPGetPC(linearsolver, &preconditioner);
  if (mastereq->usematshell) PCSetType(preconditioner, PCNONE);
  double reltol = 1.e-8;
  double abstol = 1.e-10;
  KSPSetTolerances(linearsolver, reltol, abstol, PETSC_DEFAULT, PETSC_DEFAULT);
  KSPSetType(linearsolver, KSPGMRES);
  KSPSetOperators(linearsolver, mastereq->getRHS(), mastereq->getRHS());
  KSPSetFromOptions(linearsolver);

  KSPsolve_counter = 0;
  KSPsolve_iterstaken_avg = 0.0;
}


ImplMidpoint::~ImplMidpoint(){

  /* Print */
  KSPsolve_iterstaken_avg = (int) KSPsolve_iterstaken_avg / KSPsolve_counter;
  int myrank;
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
  if (myrank == 0) printf("KSPsolve: Average iterations = %d\n", KSPsolve_iterstaken_avg);

  /* Free up intermediate vectors */
  VecDestroy(&stage_adj);
  VecDestroy(&stage);
  VecDestroy(&rhs_adj);
  VecDestroy(&rhs);

  /* Free up linear solver */
  KSPDestroy(&linearsolver);
}

void ImplMidpoint::evolveFWD(double tstart, double tstop, Vec x) {

  /* Compute time step size */
  double dt = fabs(tstop - tstart); // absolute values needed in case this runs backwards! 

  /* Compute A(t_n+h/2) */
  mastereq->assemble_RHS( (tstart + tstop) / 2.0);
  Mat A = mastereq->getRHS(); 

  /* Compute rhs = A x */
  MatMult(A, x, rhs);

  /* Build system matrix I-h/2 A. This modifies the RHS matrix! Make sure to call assemble_RHS before the next use! */
  MatScale(A, - dt/2.0);
  MatShift(A, 1.0);  // WARNING: this can be very slow if some diagonal elements are missing.
  
  /* solve nonlinear equation */
  // KSPReset(linearsolver);
  // KSPSetTolerances(linearsolver, 1.e-8, 1.e-10, PETSC_DEFAULT, PETSC_DEFAULT);
  // KSPSetType(linearsolver, KSPGMRES);
  // KSPSetFromOptions(linearsolver);
  // KSPSetOperators(linearsolver, A, A);// TODO: Do we have to do this in each time step?? 
  KSPSolve(linearsolver, rhs, stage);

  /* Monitor error */
  double rnorm;
  int iters_taken;
  KSPGetResidualNorm(linearsolver, &rnorm);
  KSPGetIterationNumber(linearsolver, &iters_taken);
  //printf("Residual norm %d: %1.5e\n", iters_taken, rnorm);
  KSPsolve_iterstaken_avg += iters_taken;
  KSPsolve_counter++;

  /* If matshell, revert the scaling and shifting */
  if (mastereq->usematshell){
    MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);
  }

  /* --- Update state x += dt * stage --- */
  VecAXPY(x, dt, stage);
}

void ImplMidpoint::evolveBWD(double tstop, double tstart, Vec x, Vec x_adj, Vec grad, bool compute_gradient){
  Mat A;

  /* Compute time step size */
  double dt = fabs(tstop - tstart); // absolute values needed in case this runs backwards! 
  double thalf = (tstart + tstop) / 2.0;

  /* Assemble RHS(t_1/2) */
  mastereq->assemble_RHS( (tstart + tstop) / 2.0);
  A = mastereq->getRHS();

  /* Get Ax_n for use in gradient */
  if (compute_gradient) {
    MatMult(A, x, rhs);
  }

  /* Solve for adjoint stage variable */
  MatScale(A, - dt/2.0);
  MatShift(A, 1.0);  // WARNING: this can be very slow if some diagonal elements are missing.
  KSPSolveTranspose(linearsolver, x_adj, stage_adj);

  double rnorm;
  KSPGetResidualNorm(linearsolver, &rnorm);
  if (rnorm > 1e-3)  printf("Residual norm: %1.5e\n", rnorm);

  // k_bar = h*k_bar 
  VecScale(stage_adj, dt);

  /* Add to reduced gradient */
  if (compute_gradient) {
    KSPSolve(linearsolver, rhs, stage);
    VecAYPX(stage, dt / 2.0, x);
    mastereq->computedRHSdp(thalf, stage, stage_adj, 1.0, grad);
  }

  /* Revert changes to RHS from above */
  A = mastereq->getRHS();
/* If matshell, revert the scaling and shifting */
  if (mastereq->usematshell){
    MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY);
  } else {
    MatShift(A, -1.0); 
    MatScale(A, - 2.0/dt);
  }

  /* Update adjoint state x_adj += dt * A^Tstage_adj --- */
  MatMultTransposeAdd(A, stage_adj, x_adj, x_adj);

}


PetscErrorCode RHSJacobian(TS ts,PetscReal t,Vec u,Mat M,Mat P,void *ctx){

  /* Cast ctx to equation pointer */
  MasterEq *mastereq = (MasterEq*) ctx;

  /* Assembling the Hamiltonian will set the matrix RHS from Re, Im */
  mastereq->assemble_RHS(t);

  /* Set the hamiltonian system matrix */
  M = mastereq->getRHS();

  return 0;
}

PetscErrorCode RHSJacobianP(TS ts, PetscReal t, Vec y, Mat A, void *ctx){

  /* Cast ctx to Hamiltonian pointer */
  MasterEq *mastereq= (MasterEq*) ctx;

  /* Assembling the derivative of RHS with respect to the control parameters */
  printf("THIS IS NOT IMPLEMENTED \n");
  exit(1);
  // mastereq->assemble_dRHSdp(t, y);

  /* Set the derivative */
  // A = mastereq->getdRHSdp();

  return 0;
}


PetscErrorCode TSInit(TS ts, MasterEq* mastereq, PetscInt NSteps, PetscReal Dt, PetscReal Tfinal, Vec x, bool monitor){
  int ierr;

  ierr = TSSetProblemType(ts,TS_LINEAR);CHKERRQ(ierr);
  ierr = TSSetType(ts, TSTHETA); CHKERRQ(ierr);
  ierr = TSThetaSetTheta(ts, 0.5); CHKERRQ(ierr);   // midpoint rule
  ierr = TSSetRHSFunction(ts,NULL,TSComputeRHSFunctionLinear,mastereq);CHKERRQ(ierr);
  ierr = TSSetRHSJacobian(ts,mastereq->getRHS(),mastereq->getRHS(),RHSJacobian,mastereq);CHKERRQ(ierr);
  ierr = TSSetTimeStep(ts,Dt);CHKERRQ(ierr);
  ierr = TSSetMaxSteps(ts,NSteps);CHKERRQ(ierr);
  ierr = TSSetMaxTime(ts,Tfinal);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_STEPOVER);CHKERRQ(ierr);
  if (monitor) {
    ierr = TSMonitorSet(ts, Monitor, NULL, NULL); CHKERRQ(ierr);
  }
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);
  ierr = TSSetSolution(ts, x); CHKERRQ(ierr);


  return ierr;
}



PetscErrorCode Monitor(TS ts,PetscInt step,PetscReal t,Vec x,void *ctx) {
  PetscErrorCode ierr;
  PetscFunctionBeginUser;

    // Vec *stage;
    // int nr;
    // TSGetStages(ts, &nr, &stage);

  const PetscScalar *x_ptr;
  ierr = VecGetArrayRead(x, &x_ptr);
  printf("Monitor ->%d,%f, x[1]=%1.14e\n", step, t, x_ptr[1]);
  ierr = VecRestoreArrayRead(x, &x_ptr);

  PetscFunctionReturn(0);
}

PetscErrorCode AdjointMonitor(TS ts,PetscInt step,PetscReal t,Vec x, PetscInt numcost, Vec* lambda, Vec* mu, void *ctx) {
  PetscErrorCode ierr;

    // Vec *stage;
    // int nr;
    // TSGetStages(ts, &nr, &stage);

  const PetscScalar *x_ptr;
  const PetscScalar *lambda_ptr;
  const PetscScalar *mu_ptr;
  ierr = VecGetArrayRead(x, &x_ptr);
  ierr = VecGetArrayRead(lambda[0], &lambda_ptr);
  ierr = VecGetArrayRead(mu[0], &mu_ptr);
  // ierr = VecGetArrayRead(stage[0], &x_ptr);
  printf("AdjointMonitor %d: %f->, dt=%f  x[1]=%1.14e, lambda[0]=%1.14e, mu[0]=%1.14e\n", step, t, ts->time_step, x_ptr[1], lambda_ptr[0], mu_ptr[0] );
  ierr = VecRestoreArrayRead(x, &x_ptr);
  ierr = VecRestoreArrayRead(lambda[0], &lambda_ptr);
  ierr = VecRestoreArrayRead(mu[0], &mu_ptr);

  PetscFunctionReturn(0);
}



PetscErrorCode TSPreSolve(TS ts, bool tj_store){
  int ierr; 

  ierr = TSSetUp(ts); CHKERRQ(ierr);
  if (tj_store) ierr = TSTrajectorySetUp(ts->trajectory,ts);CHKERRQ(ierr);

  /* reset time step and iteration counters */
  if (!ts->steps) {
    ts->ksp_its           = 0;
    ts->snes_its          = 0;
    ts->num_snes_failures = 0;
    ts->reject            = 0;
    ts->steprestart       = PETSC_TRUE;
    ts->steprollback      = PETSC_FALSE;
  }
  ts->reason = TS_CONVERGED_ITERATING;
  if (ts->steps >= ts->max_steps) ts->reason = TS_CONVERGED_ITS;
  else if (ts->ptime >= ts->max_time) ts->reason = TS_CONVERGED_TIME;

  if (!ts->steps && tj_store) {
    ierr = TSTrajectorySet(ts->trajectory,ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);
  }
  // ierr = TSMonitor(ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);

  return ierr;
}


PetscErrorCode TSStepMod(TS ts, bool tj_store){
  int ierr; 

  ierr = TSMonitor(ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);
  ierr = TSPreStep(ts);CHKERRQ(ierr);
  ierr = TSStep(ts);CHKERRQ(ierr);
  ierr = TSPostEvaluate(ts);CHKERRQ(ierr);
  ierr = TSMonitor(ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);

  if (tj_store) ierr = TSTrajectorySet(ts->trajectory,ts,ts->steps,ts->ptime,ts->vec_sol);CHKERRQ(ierr);
  ierr = TSPostStep(ts);CHKERRQ(ierr);

  return ierr;
}

PetscErrorCode TSPostSolve(TS ts){
  int ierr;
  ts->solvetime = ts->ptime;
  return ierr;
}


PetscErrorCode TSAdjointPreSolve(TS ts){
  int ierr;

  /* reset time step and iteration counters */
  ts->adjoint_steps     = 0;
  ts->ksp_its           = 0;
  ts->snes_its          = 0;
  ts->num_snes_failures = 0;
  ts->reject            = 0;
  ts->reason            = TS_CONVERGED_ITERATING;

  if (!ts->adjoint_max_steps) ts->adjoint_max_steps = ts->steps;
  if (ts->adjoint_steps >= ts->adjoint_max_steps) ts->reason = TS_CONVERGED_ITS;


  return ierr;
}


PetscErrorCode TSAdjointStepMod(TS ts, bool tj_save) {
  int ierr;


  if (tj_save) ierr = TSTrajectoryGet(ts->trajectory,ts,ts->steps,&ts->ptime);CHKERRQ(ierr);

  ierr = TSAdjointMonitor(ts,ts->steps,ts->ptime,ts->vec_sol,ts->numcost,ts->vecs_sensi,ts->vecs_sensip);CHKERRQ(ierr);

  ierr = TSAdjointStep(ts);CHKERRQ(ierr);
  if (ts->vec_costintegral && !ts->costintegralfwd) {
    ierr = TSAdjointCostIntegral(ts);CHKERRQ(ierr);
  }
  ierr = TSAdjointMonitor(ts,ts->steps,ts->ptime,ts->vec_sol,ts->numcost,ts->vecs_sensi,ts->vecs_sensip);CHKERRQ(ierr);

  return ierr;
}

PetscErrorCode TSAdjointPostSolve(TS ts, bool tj_save){
  int ierr; 
  if (tj_save) ierr = TSTrajectoryGet(ts->trajectory,ts,ts->steps,&ts->ptime);CHKERRQ(ierr);
  ierr = TSAdjointMonitor(ts,ts->steps,ts->ptime,ts->vec_sol,ts->numcost,ts->vecs_sensi,ts->vecs_sensip);CHKERRQ(ierr);
  ts->solvetime = ts->ptime;
  if (tj_save) ierr = TSTrajectoryViewFromOptions(ts->trajectory,NULL,"-ts_trajectory_view");CHKERRQ(ierr);
  ierr = VecViewFromOptions(ts->vecs_sensi[0],(PetscObject) ts, "-ts_adjoint_view_solution");CHKERRQ(ierr);
  ts->adjoint_max_steps = 0;

  return ierr;
}


PetscErrorCode  TSSetAdjointSolution(TS ts,Vec lambda, Vec mu)
{
  PetscErrorCode ierr;
  PetscFunctionBegin;
  PetscValidHeaderSpecific(ts,TS_CLASSID,1);
  PetscValidHeaderSpecific(lambda,VEC_CLASSID,3);

  ierr = PetscObjectReference((PetscObject)lambda);CHKERRQ(ierr);
  if (ts->vecs_sensi[0]) ierr = VecDestroy(&ts->vecs_sensi[0]);CHKERRQ(ierr);
  ts->vecs_sensi[0] = lambda;
  // if (ts->vecs_sensip[0]) ierr = VecDestroy(&ts->vecs_sensip[0]);CHKERRQ(ierr);
  ts->vecs_sensip[0] = mu;

  PetscFunctionReturn(0);
}
