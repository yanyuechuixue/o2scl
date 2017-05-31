/*
  -------------------------------------------------------------------
  
  Copyright (C) 2006-2017, Andrew W. Steiner
  
  This file is part of O2scl.
  
  O2scl is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.
  
  O2scl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with O2scl. If not, see <http://www.gnu.org/licenses/>.

  -------------------------------------------------------------------
*/
#ifndef O2SCL_ANNEAL_PARA_H
#define O2SCL_ANNEAL_PARA_H

#include <o2scl/anneal_gsl.h>
#include <o2scl/vector.h>

#ifndef DOXYGEN_NO_O2NS
namespace o2scl {
#endif
  
  /** \brief Multidimensional minimization by simulated annealing 
      (Boost multi-threaded version)

      This header-only class additionally requires the Boost
      libraries. It performs simulated annealing using an arbitrary
      number of processors using <tt>boost::thread</tt>, which is
      closely related to the standard Unix pthread library. It works
      very similarly to \ref anneal_gsl, it performs \ref ntrial
      evaluations over each processor, then applying the metropolis
      algorithm to the results from all of the processors at the end.
     
      Because <tt>np</tt> function calls are happening simultaneously,
      where <tt>np</tt> is the number of processors, <tt>np</tt>
      copies of the function parameters of type <tt>param_t</tt> must
      also be specified. The user-defined function to minimize must
      also be thread-safe, allowing multiple threads to call the
      function at once (albeit given different parameters). The
      default type for these <tt>np</tt> copies of the parameters of
      type <tt>param_t</tt> is <tt>std::vector<param_t></tt>.

      This works particularly well for functions which are not trivial
      to evaluate, i.e. functions where the execution time is more
      longer than the bookkeeping that \ref anneal_para performs between
      trials. For functions which satisfy this requirement, this
      algorithm scales nearly linearly with the number of processors.

      Verbose I/O for this class happens only outside the theads
      unless the user places I/O in the streams in the function that
      is specified.

      \future There may be a good way to remove the function indirection
      here to make this class a bit faster.
  */
  template<class func_t=multi_funct,
    class vec_t=boost::numeric::ublas::vector<double>,
    class rng_t=int, class rng_dist_t=rng_gsl>
    class anneal_para : public anneal_gsl<func_t,vec_t,rng_t,rng_dist_t> {

  public:

  typedef boost::numeric::ublas::vector<double> ubvector;

  /// The number of OpenMP threads
  size_t n_threads;
  
  /// The MPI starting time
  double mpi_start_time;

  /// The maximum time
  double mpi_max_time;
  
  anneal_para() {
    n_threads=1;
    mpi_start_time=0.0;
    max_time=0.0;
  }

  virtual ~anneal_para() {
  }

  /// \name Basic usage
  //@{
  /** \brief Desc
   */
  virtual int mmin(size_t nv, vec_t &x0, double &fmin, 
		   std::vector<func_t> &func) {

    if (func.size()<n_threads) {
      if (verbose>0) {
	cout << "mcmc_para::mcmc(): Not enough functions for "
	<< n_threads << " threads. Setting n_threads to "
	<< func.size() << "." << endl;
      }
      n_threads=func.size();
    }

    // Set number of threads
#ifdef O2SCL_OPENMP
    omp_set_num_threads(n_threads);
    n_threads=omp_get_num_threads();
#else
    n_threads=1;
#endif

    // Set starting time
#ifdef O2SCL_MPI
    mpi_start_time=MPI_Wtime();
#else
    mpi_start_time=time(0);
#endif

    if (nvar==0) {
      O2SCL_ERR2("Tried to minimize over zero variables ",
		 " in anneal_para::mmin().",exc_einval);
    }
    
    fmin=0.0;

    step_norm=1.0;

    vector<vec_t> x(n_threads), new_x(n_threads);
    vector<vec_t> best_x(n_threads);
    vec_t old_x(nvar);
    
    ubvector E(n_threads), new_E(n_threads), best_E(n_threads);
    double T;
    ubvector old_E(n_threads), r(n_threads);
    int iter=0;

#ifdef O2SCL_OPENMP
#pragma omp parallel default(shared)
#endif
    {
#ifdef O2SCL_OPENMP
#pragma omp for
#endif
      for(size_t it=0;it<n_threads;it++) {

	// Allocate space
	x[it].resize(nvar);
	new_x[it].resize(nvar);
	best_x[it].resize(nvar);
	
	// Copy initial point
	for(size_t j=0;j<nvar;j++) {
	  x[it][j]=x0[j];
	  best_x[it][j]=x0[j];
	}

	// Perform first function evaluation
	E[it]=func[it](nvar,x[it]);
	best_E[it]=E[it];
      }
    }
    // End of parallel region

    // Setup initial temperature and step sizes
    start(nvar,T);
    
    bool done=false;

    while (!done) {

      size_t nmoves=0;

#ifdef O2SCL_OPENMP
#pragma omp parallel default(shared)
#endif
      {
#ifdef O2SCL_OPENMP
#pragma omp for
#endif
	for(size_t it=0;it<n_threads;it++) {

	  // Copy old value of x for next() function
	  for(size_t j=0;j<nvar;j++) old_x[it][j]=x[it][j];
	  old_E[it]=E[it];
	  
	  for (int i=0;i<this->ntrial;++i) {
	    for (size_t j=0;j<nvar;j++) new_x[it][j]=x[it][j];

	    // This requires that the step() function is threadsafe.
	    // This is the case here because the parent step()
	    // function reads class member data but doesn't write.
	    step(new_x[it],nvar);
	    new_E[it]=func[it](nvar,new_x[it]);
#ifdef O2SCL_MPI
	    double elapsed=MPI_Wtime()-mpi_start_time;
#else
	    double elapsed=time(0)-mpi_start_time;
#endif
	    if (max_time>0.0 && elapsed>max_time) {
	      i=this->ntrial;
	      done=true;
	    }
	
	    // Store best value obtained so far
	    if(new_E[it]<=best_E[it]) {
	      for(size_t j=0;j<nvar;j++) best_x[it][j]=new_x[it][j];
	      best_E[it]=new_E[it];
	    }
	
	    // Take the crucial step: see if the new point is accepted
	    // or not, as determined by the Boltzmann probability
	    if (new_E[it]<E[it]) {
	      for(size_t j=0;j<nvar;j++) x[it][j]=new_x[it][j];
	      E[it]=new_E[it];
	      if (it==0) nmoves++;
	    } else {
	      double r[it]=this->dist();
	      if (r[it] < exp(-(new_E[it]-E[it])/(boltz*T))) {
		for(size_t j=0;j<nvar;j++) x[it][j]=new_x[it][j];
		E[it]=new_E[it];
		if (it==0) nmoves++;
	      }
	    }

	  }
	  
	}
      }
      // End of parallel region

      // Find best point over all threads
      fmin=best_E[0];
      for(size_t iv=0;iv<nvar;iv++) {
	x0=best_x[0][iv];
      }
      for(size_t i=1;i<n_threads;i++) {
	if (best_E[i]<fmin) {
	  fmin=best_E[i];
	  for(size_t iv=0;iv<nvar;iv++) {
	    x0[iv]=best_x[i][iv];
	  }
	}
      }
      
      if (this->verbose>0) {
	this->print_iter(nvar,best_overall,E_overall,iter,T,"anneal_gsl");
	iter++;
      }
	  
      // See if we're finished and proceed to the next step
      if (done==false) {
#ifdef O2SCL_OPENMP
#pragma omp parallel default(shared)
#endif
	{
#ifdef O2SCL_OPENMP
#pragma omp for
#endif
	  vector<bool> done_arr(n_threads);
	  for(size_t it=0;it<n_threads;it++) {
	    next(nvar,old_x[it],old_E[it],x,E,T,nmoves,E_overall,done_arr[it]);
	  }
	}
      }
      // End of parallel region

      // Decrease the temperature
      T/=T_dec;

      // We're done when all threads report done
      done=true;
      for(size_t it=0;it<n_threads;it++) {
	if (done_arr[it]==false) done=false;
      }
      
    }
    
    return 0;
  }

  /// Determine how to change the minimization for the next iteration
  virtual int next(size_t nvar, vec_t &x_old, double min_old, 
		   vec_t &x_new, double min_new, double &T, 
		   size_t n_moves, vec_t &best_x, double best_E, 
		   bool &finished) {
    
    if (T/T_dec<this->tol_abs) {
      finished=true;
      return success;
    }
    if (n_moves==0) {
      // If we haven't made any progress, shrink the step by
      // decreasing step_norm
      step_norm/=step_dec;
      // Also reset x to best value so far
      for(size_t i=0;i<nvar;i++) {
	x_new[i]=best_x[i];
      }
      min_new=best_E;
    }
    return success;
  }

  /** \brief Desc
   */
  virtual int mmin(size_t nv, vec_t &x0, double &fmin, 
		   func_t &func) {
#ifdef O2SCL_OPENMP
    omp_set_num_threads(n_threads);
    n_threads=omp_get_num_threads();
#else
    n_threads=1;
#endif
    std::vector<func_t> vf(n_threads);
    for(size_t i=0;i<n_threads;i++) {
      vf[i]=func;
    }
    return mmin(nv,x0,fmin,vf);

  }

  /// Return string denoting type ("anneal_para")
  virtual const char *type() { return "anneal_para"; }

  };

#ifndef DOXYGEN_NO_O2NS
}
#endif

#endif