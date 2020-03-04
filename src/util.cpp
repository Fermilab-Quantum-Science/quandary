#include "util.hpp"

PetscErrorCode Ikron(Mat A, int dimI, double alpha, Mat *Out, InsertMode insert_mode){

    int ierr;
    int ncols;
    const PetscInt* cols; 
    const PetscScalar* Avals;
    PetscInt* shiftcols;
    PetscScalar* vals;
    int dimA;
    int dimOut;
    int nonzeroOut;
    int rowID;

    MatGetSize(A, &dimA, NULL);

    ierr = PetscMalloc1(dimA, &shiftcols); CHKERRQ(ierr);
    ierr = PetscMalloc1(dimA, &vals); CHKERRQ(ierr);

    /* Loop over dimension of I */
    for (int i = 0; i < dimI; i++){

        /* Set the diagonal block (i*dimA)::(i+1)*dimA */
        for (int j=0; j<dimA; j++){
            MatGetRow(A, j, &ncols, &cols, &Avals);
            rowID = i*dimA + j;
            for (int k=0; k<ncols; k++){
                shiftcols[k] = cols[k] + i*dimA;
                vals[k] = Avals[k] * alpha;
            }
            MatSetValues(*Out, 1, &rowID, ncols, shiftcols, vals, insert_mode);
            MatRestoreRow(A, j, &ncols, &cols, &Avals);
        }

    }
    // MatAssemblyBegin(*Out, MAT_FINAL_ASSEMBLY);
    // MatAssemblyEnd(*Out, MAT_FINAL_ASSEMBLY);

    PetscFree(shiftcols);
    PetscFree(vals);
    return 0;
}

PetscErrorCode kronI(Mat A, int dimI, double alpha, Mat *Out, InsertMode insert_mode){
    
    int ierr;
    int dimA;
    const PetscInt* cols; 
    const PetscScalar* Avals;
    int rowid;
    int colid;
    double insertval;
    int dimOut;
    int nonzeroOut;
    int ncols;
    MatInfo Ainfo;
    MatGetSize(A, &dimA, NULL);

    ierr = PetscMalloc1(dimA, &cols); CHKERRQ(ierr);
    ierr = PetscMalloc1(dimA, &Avals);

    /* Loop over rows in A */
    for (int i = 0; i < dimA; i++){
        MatGetRow(A, i, &ncols, &cols, &Avals);

        /* Loop over non negative columns in row i */
        for (int j = 0; j < ncols; j++){
            //printf("A: row = %d, col = %d, val = %f\n", i, cols[j], Avals[j]);
            
            // dimI rows. global row indices: i, i+dimI
            for (int k=0; k<dimI; k++) {
               rowid = i*dimI + k;
               colid = cols[j]*dimI + k;
               insertval = Avals[j] * alpha;
               MatSetValues(*Out, 1, &rowid, 1, &colid, &insertval, insert_mode);
              //  printf("Setting %d,%d %f\n", rowid, colid, insertval);
            }
        }
        MatRestoreRow(A, i, &ncols, &cols, &Avals);
    }

    // MatAssemblyBegin(*Out, MAT_FINAL_ASSEMBLY);
    // MatAssemblyEnd(*Out, MAT_FINAL_ASSEMBLY);

    PetscFree(cols);
    PetscFree(Avals);

    return 0;
}



PetscErrorCode AkronB(int dim, Mat A, Mat B, double alpha, Mat *Out, InsertMode insert_mode){

    int dimOut = dim*dim;

    int ncolsA,ncolsB;
    const int *colsA, *colsB;
    const double *valsA, *valsB;
    // Iterate over rows of A 
    for (int irowA = 0; irowA < dim; irowA++){
        // Iterate over non-zero columns in this row of A
        MatGetRow(A, irowA, &ncolsA, &colsA, &valsA);
        for (int j=0; j<ncolsA; j++) {
            int icolA = colsA[j];
            double valA = valsA[j];
            /* put a B-block at position (irowA*dim, icolA*dim): */
            // Iterate over rows of B 
            for (int irowB = 0; irowB < dim; irowB++){
                // Iterate over non-zero columns in this B-row
                MatGetRow(B, irowB, &ncolsB, &colsB, &valsB);
                for (int k=0; k< ncolsB; k++) {
                    int icolB = colsB[k];
                    double valB = valsB[k];
                    /* Insert values in Out */
                    int rowOut = irowA*dim + irowB;
                    int colOut = icolA*dim + icolB;
                    double valOut = valA * valB * alpha; 
                    MatSetValue(*Out, rowOut, colOut, valOut, insert_mode);
                }
                MatRestoreRow(B, irowB, &ncolsB, &colsB, &valsB);
            }
        }   
        MatRestoreRow(A, irowA, &ncolsA, &colsA, &valsA);
    }  

  return 0;
}


PetscErrorCode MatIsAntiSymmetric(Mat A, PetscReal tol, PetscBool *flag) {
  
  int ierr; 

  /* Create B = -A */
  Mat B;
  ierr = MatConvert(A, MATSAME, MAT_INITIAL_MATRIX, &B); CHKERRQ(ierr);
  ierr = MatScale(B, -1.0); CHKERRQ(ierr);

  /* Test if B^T = A */
  ierr = MatIsTranspose(B, A, tol, flag); CHKERRQ(ierr);

  /* Cleanup */
  ierr = MatDestroy(&B); CHKERRQ(ierr);

  return ierr;
}



PetscErrorCode StateIsHermitian(Vec x, PetscReal tol, PetscBool *flag) {
  int ierr;
  int i, j;

  /* Get u and v from x */
  int dim;
  ierr = VecGetSize(x, &dim); CHKERRQ(ierr);
  dim = dim/2;
  Vec u, v;
  IS isu, isv;
  ierr = ISCreateStride(PETSC_COMM_WORLD, dim, 0, 1, &isu); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_WORLD, dim, dim, 1, &isv); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isu, &u); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isv, &v); CHKERRQ(ierr);

  /* Init flags*/
  *flag = PETSC_TRUE;
  PetscBool u_isSymm     = PETSC_TRUE;
  PetscBool v_isAntiSymm = PETSC_TRUE;

  /* Check for symmetric u and antisymmetric v */
  const double *u_array;
  const double *v_array;
  double u_diff, v_diff;
  ierr = VecGetArrayRead(u, &u_array); CHKERRQ(ierr);
  ierr = VecGetArrayRead(v, &v_array); CHKERRQ(ierr);
  int N = sqrt(dim);
  for (i=0; i<N; i++) {
    for (j=i; j<N; j++) {
      u_diff = u_array[i*N+j] - u_array[j*N+i];
      v_diff = v_array[i*N+j] + v_array[j*N+i];
      if (fabs(u_diff) > tol) u_isSymm = PETSC_FALSE;
      if (fabs(v_diff) > tol) v_isAntiSymm = PETSC_FALSE;
      if (!u_isSymm || !v_isAntiSymm) {
        *flag = PETSC_FALSE;
        printf("WARNING: not hermitian: i=%d, j=%d u_diff=%1.14e, v_diff=%1.14e\n", i, j, u_diff, v_diff);
        break;
      }
    }
  }

  ierr = VecRestoreArrayRead(u, &u_array);
  ierr = VecRestoreArrayRead(v, &v_array);
  
  if (!u_isSymm) {
    printf("\n u :\n");
    VecView(u, PETSC_VIEWER_STDOUT_WORLD);
  }
  if (!v_isAntiSymm) {
    printf("\n v :\n");
    VecView(v, PETSC_VIEWER_STDOUT_WORLD);
  }

  return ierr;
}



PetscErrorCode StateHasTrace1(Vec x, PetscReal tol, PetscBool *flag) {

  int ierr;
  int i;

  /* Get u and v from x */
  int dim;
  ierr = VecGetSize(x, &dim); CHKERRQ(ierr);
  dim = dim/2;
  Vec u, v;
  IS isu, isv;
  ierr = ISCreateStride(PETSC_COMM_WORLD, dim, 0, 1, &isu); CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_WORLD, dim, dim, 1, &isv); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isu, &u); CHKERRQ(ierr);
  ierr = VecGetSubVector(x, isv, &v); CHKERRQ(ierr);

  /* Init flags*/
  *flag = PETSC_TRUE;
  PetscBool u_hastrace1   = PETSC_TRUE;
  PetscBool v_haszerodiag = PETSC_TRUE;

  /* Check if diagonal of u sums to 1, and all diagonal elements of v are 0 */ 
  const double *u_array;
  const double *v_array;
  double u_sum = 0.0;
  ierr = VecGetArrayRead(u, &u_array); CHKERRQ(ierr);
  ierr = VecGetArrayRead(v, &v_array); CHKERRQ(ierr);
  int N = sqrt(dim);
  for (i=0; i<N; i++) {
    u_sum += u_array[i*N+i];
    if ( fabs(v_array[i*N+i]) > tol ) {
        v_haszerodiag = PETSC_FALSE;
        break;
    }
  }
  if ( fabs(u_sum - 1.0) > tol ) u_hastrace1 = PETSC_FALSE;

  /* Restore vecs */
  ierr = VecRestoreArrayRead(u, &u_array);
  ierr = VecRestoreArrayRead(v, &v_array);


  /* Answer*/
  if (!u_hastrace1 || !v_haszerodiag) {
    *flag = PETSC_FALSE;
    printf("WARNING: Trace(state) is not one!! Trace(u) = %1.14e, i=%d\n", u_sum, i );
  }
  
  if (!u_hastrace1) {
    printf("\n u :\n");
    VecView(u, PETSC_VIEWER_STDOUT_WORLD);
    printf("\n");
  }
  if (!v_haszerodiag) {
    printf("\n v :\n");
    VecView(v, PETSC_VIEWER_STDOUT_WORLD);
    printf("\n");
  }


  return ierr;
}


void read_vector(const char *filename, double *var, int dim) {

  FILE *file;
  double tmp;

  /* Open file */
  file = fopen(filename, "r");
  if (file == NULL) {
    printf("Can't open %s \n", filename);
    exit(1);
  }

  /* Read data */
  printf("Reading file %s\n", filename);
  for (int ix = 0; ix < dim; ix++) {
    fscanf(file, "%lf", &tmp);
    var[ix] = tmp;
  }

  fclose(file);
}
