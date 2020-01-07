#include "IpTNLP.hpp"

#pragma once

using namespace Ipopt;

class OptimProblem : public TNLP {

    public:
        OptimProblem();
        virtual ~OptimProblem();

    /* --- Overload required Ipopt interface routines --- */
    
	/* Return info about the optim problem*/
	virtual bool get_nlp_info(Index& n, Index& m, Index&, Index& nnz_h_lag, IndexStyleEnum& index_style);

	/* Return the bounds */
	virtual bool get_bounds_info(Index n, Number* x_l, Number* x_u, Index m, Number* g_l, Number* g_u);



	/* Return the initial guess (starting point of the optimization */
	virtual bool get_starting_point(Index n, bool init_x, Number* x, bool init_z, Number* z_L, Number* z_U, Index m, bool init_lambda, Number* lambda);

	/* Return the objective function value */
	virtual bool eval_f(Index n, const Number* x, bool new_x, Number& obj_value);

	/* Return the gradient of the objective function */
	virtual bool eval_grad_f(Index n, const Number* x, bool new_x, Number* grad_f);
	
	/* Return the residual of the constraints */
	virtual bool eval_g(Index n, const Number* x, bool new_x, Index m, Number* g);

	/* Return:
	 *   1) The structure of the jacobian (if "values" is NULL)
	 *   2) The values of the jacobian (if "values" is not NULL)
	 */
	virtual bool eval_jac_g(Index n, const Number* x, bool new_x, Index m, Index nele_jac, Index* iRow, Index *jCol, Number* values);


	/* Return:
	 *   1) The structure of the hessian of the lagrangian (if "values" is NULL)
	 *   2) The values of the hessian of the lagrangian (if "values" is not NULL)
	 */
	virtual bool eval_h(Index n, const Number* x, bool new_x, Number obj_factor, Index m, const Number* lambda, bool new_lambda, Index nele_hess, Index* iRow, Index* jCol, Number* values);


	/* This method is called when the algorithm is complete so the TNLP can store/write the solution */
	virtual void finalize_solution(SolverReturn status, Index n, const Number* x, const Number* z_L, const Number* z_U, Index m, const Number* g, const Number* lambda, Number obj_value, const IpoptData* ip_data,IpoptCalculatedQuantities* ip_cq);


	private:
	/* Methods to block default compiler methods.
	 * The compiler automatically generates the following three methods.
	 * Since the default compiler implementation is generally not what
	 * you want (for all but the most simple classes), we usually
	 * put the declarations of these methods in the private section
	 * and never implement them. This prevents the compiler from
	 * implementing an incorrect "default" behavior without us
	 * knowing. (See Scott Meyers book, "Effective C++") 
     */
	//  HS071_NLP();
	OptimProblem(const OptimProblem&);
	OptimProblem& operator=(const OptimProblem&);
};