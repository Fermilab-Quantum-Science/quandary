#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <petscmat.h>
#include <vector>
#include "util.hpp"
#pragma once


class Gate {
  protected:
    int dim_v;        /* Input: dimension of target gate (non-vectorized) */
    Mat Va, Vb;       /* Input: Real and imaginary part of V_target, non-vectorized */

  private:
    Mat ReG, ImG;     /* Real and imaginary part of \bar V \kron V */

    Vec Re0, Im0;   /* auxiliary vectors */
  protected:
    int dim_vec;      /* dimension of vectorized system dim_vec = dim_v^2 */
    IS isu, isv;      /* Vector strides for extracting u,v from x = [u,v] */

  public:
    Gate();
    Gate(int dim_V_);
    virtual ~Gate();

    /* Assemble ReG = Re(\bar V \kron V) and ImG = Im(\bar V \kron V) */
    void assembleGate();
    
    /* compare the final state to gate-transformed initialcondition in Frobenius norm || q(T) - V\kronV q(0)||^2 */
    virtual void compare(Vec finalstate, Vec initcond_re, Vec initcond_im, double& frob);
    virtual void compare_diff(const Vec finalstate, const Vec initcond_re, const Vec initcond_im, Vec u0_bar, Vec v0_bar, const double delta_bar);
};

/* X Gate, spanning one qubit. 
 * V = 0 1
 *     1 0
 */
class XGate : public Gate {
  public:
    XGate();
    ~XGate();
};

/* Y Gate, spanning one qubit. 
 * V = 0 -i
 *     i  0
 */
class YGate : public Gate {
  public:
    YGate();
    ~YGate();
};

/* Z Gate, spanning one qubit. 
 * V = 1   0
 *     0  -1
 */
class ZGate : public Gate {
  public:
    ZGate();
    ~ZGate();
};

/* Hadamard Gate 
 * V = 1/sqrt(2) * | 1   1 |
 *                 | 1  -1 |
 */
class HadamardGate : public Gate {
  public:
    HadamardGate();
    ~HadamardGate();
};

/* CNOT Gate, spanning two qubits. This class is mostly hardcoded. TODO: Generalize!
 * V = 1 0 0 0
 *     0 1 0 0 
 *     0 0 0 1
 *     0 0 1 0 
 */
class CNOT : public Gate {
    public:
    CNOT();
    ~CNOT();
};

/* Groundstate Gate pushes every initial condition towards the ground state |0><0|
 * This gate can not be represented by a linear transformation V\kron V * q(0) !!
 * Instead, we overload the 'compare' routine to measure the distance || q(T) - e_1||^2, where e_1 is the first unit vector.
 */
class GroundstateGate : public Gate {
  public:
    GroundstateGate(int dim_v_);
    ~GroundstateGate();

    /* overload the compare function: || q(T) - e_1 ||^2 for fixed unit vector e_1 = (1 0 0 0 0 ...)^T */
    void compare(Vec finalstate, Vec initcond_re, Vec initcond_im, double& frob);
    void compare_diff(const Vec finalstate, const Vec initcond_re, const Vec initcond_im, Vec u0_bar, Vec v0_bar, const double delta_bar);
};

