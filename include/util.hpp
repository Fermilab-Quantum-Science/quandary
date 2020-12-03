#include <petscmat.h>
#pragma once

int getIndexReal(const int i); // Return storage index of Re(x[i]) (colocated: x[2*i], blocked: x[i])
int getIndexImag(const int i); // Return storage index of Im(x[i]) (colocated: x[2*i+1], blocked: x[i+dim])

int getVecID(const int i, const int j, const int dim); // Return index of vectorized element from matrix element (i,j) in square matrix of dimension dim. 


/* Kronecker product : Id \kron A, where Id is the Identitymatrix 
 * Mat Out must be allocated with nonzerosA * dimI
 * Input: mat A       Matrix that is multiplied
 *        int dimI    Dimension of the identity
 *        insert_mode  either INSERT_VALUES or ADD_VALUES
 * Output: Mat Out = Id \kron A
 */
PetscErrorCode Ikron(const Mat A, const int dimI, const double alpha, Mat *Out, InsertMode insert_mode);

/* Kronecker product : A \kron Id, where Id is the Identitymatrix 
 * Mat Out must be allocated with nonzerosA * dimI
 * Input: mat A       Matrix that is multiplied
 *        int dimI    Dimension of the identity
 *        insert_mode  either INSERT_VALUES or ADD_VALUES
 * Output: Mat Out = A \kron Id
 */
PetscErrorCode kronI(const Mat A, const int dimI, const double alpha, Mat *Out, InsertMode insert_mode);

/* Computes kronecker product A \kron B for SQUARE matrices A,B \in \R^{dim \times dim} */
/* This works in PETSC SERIAL only. */
/* The output matrix has to be allocated before with size dim*dim \times dim*dim */
PetscErrorCode AkronB(const int dim, const Mat A, const Mat B, const double alpha, Mat *Out, InsertMode insert_mode);

/* Tests if a matrix A is anti-symmetric (A^T=-A) */
PetscErrorCode MatIsAntiSymmetric(Mat A, PetscReal tol, PetscBool *flag);


/* Test if the vectorized state vector x=[u,v] represents a hermitian matrix.
 * For this to be true, we need u being symmetric, v being antisymmetric
 */
PetscErrorCode StateIsHermitian(Vec x, PetscReal tol, PetscBool *flag);


/* Test if vectorized state vector x=[u,v] represent matrix with Trace 1 */
PetscErrorCode StateHasTrace1(Vec x, PetscReal tol, PetscBool *flag);


/**
 * Read data from file
 */
void read_vector(const char *filename, double *var, int dim);
