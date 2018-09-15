﻿/* * Project     : Hierarchical approximation of large covariance matrices and log-likelihood * File        : .cpp * Description : example how to find maximum Gaussian log-likelihood * Author      : Alexander Litvinenko * Copyright   : ECRC, KAUST, Saudi Arabia, 2017 * */#include <iostream>#include <fstream>#include <vector>#include <boost/format.hpp>#include <boost/math/special_functions/gamma.hpp>#include <boost/math/special_functions/bessel.hpp>#include <gsl/gsl_sf_bessel.h>#include <gsl/gsl_sf_gamma.h>#include <gsl/gsl_rng.h>#include <gsl/gsl_roots.h>#include <gsl/gsl_randist.h>#include <gsl/gsl_errno.h>#include <gsl/gsl_math.h>#include <gsl/gsl_min.h>#include <gsl/gsl_multimin.h>#include <hlib.hh>using namespace std;using namespace HLIB;using boost::format;using HLIB::Time::Wall::now;using HLIB::Time::Wall::since;using real_t = HLIB::real;int flag8=0;int global_k;double global_eps;double l1, l2;double step_h;int nmin;doubleeval_logli (const gsl_vector *sol, void* p);struct my_f_params {   TScalarVector Z;   vector <double*> vertices;  //  virtual std::unique_ptr< TBlockClusterTree > bct;  TBlockClusterTree* bct;  TClusterTree* ct;  //std::unique_ptr< TBlockClusterTree > bct;  //std::unique_ptr< TClusterTree > ct;  double covlength; //cov. length  double nu; //cov. smoothness  double sigma2; //variance sigma2};typedef struct my_f_params smy_f_params;typedef smy_f_params* pmy_f_params;template < typename T >stringmem_per_dof ( T && A ){    const size_t  mem  = A->byte_size();    const size_t  pdof = size_t( double(mem) / double(A->rows()) );    return Mem::to_string( mem ) + " (" + Mem::to_string( pdof ) + "/dof)";}/*double call_compute_max_likelihood(TScalarVector Z, double nu, double covlength, double sigma2,  std::unique_ptr< TBlockClusterTree >  bct, std::unique_ptr< TClusterTree >   ct,  vector <double*> vertices, double output[3])*/double call_compute_max_likelihood(TScalarVector Z, double nu, double covlength, double sigma2,  TBlockClusterTree*  bct, TClusterTree*   ct,  std::vector <double*> vertices, double output[3]){  /*param[0]=nu, param[1]=rho, cov length*/  gsl_function F;  int status;  int iter = 0, max_iter = 200;  //double a = 0.1, b = 1.0; //this is an interval  //pmy_f_params params ;  //struct my_f_params * params ;  smy_f_params params ;  FILE* f1;  //double par[5] = {1.0, 2.0, 10.0, 20.0, 30.0};  const gsl_multimin_fminimizer_type *T = gsl_multimin_fminimizer_nmsimplex2;  //read here https://www.gnu.org/software/gsl/manual/html_node/Multimin-Algorithms-without-Derivatives.html#Multimin-Algorithms-without-Derivatives  //other alternatives   gsl_multimin_fminimizer_nmsimplex2rand //other alternatives   gsl_multimin_fminimizer_nmsimplex2rand  gsl_multimin_fminimizer *s = NULL;  gsl_vector *ss, *x;  gsl_multimin_function minex_func;  double size;    params.bct = bct;  params.ct = ct;  params.Z = Z;  params.nu = nu;  params.covlength = covlength;  params.sigma2 = sigma2;  params.vertices = vertices;  /* Starting point */  x = gsl_vector_alloc(3);  gsl_vector_set (x, 0, nu); // nu  gsl_vector_set (x, 1, covlength);  // theta or cov length  gsl_vector_set (x, 2, sigma2);  // sigma2    /* Set initial step sizes to 0.1 */  ss = gsl_vector_alloc (3);  /* It was like this gsl_vector_set_all (ss, 0.02); /*!Important!*/  gsl_vector_set (ss, 0, 0.002); //nu   gsl_vector_set (ss, 1, 0.004); //theta  gsl_vector_set (ss, 2, 0.001);  //sigma2  /* Initialize method and iterate */  minex_func.n = 3;  minex_func.f =  &eval_logli;  minex_func.params = &params;  s = gsl_multimin_fminimizer_alloc (T, 3); /* or 2? like in the example */  gsl_multimin_fminimizer_set (s, &minex_func, x, ss);  do    {      iter++;      status = gsl_multimin_fminimizer_iterate(s);            if (status)         break;      size = gsl_multimin_fminimizer_size (s);  //return eps for stopping criteria  //    printf("STOPPIG CRITERIA = %4.4g \n", size);      status = gsl_multimin_test_size (size, 1e-5); //This function tests the minimizer specific characteristic size       //(if applicable to the used minimizer) against absolute tolerance epsabs. The test returns GSL_SUCCESS if the size is smaller than tolerance, otherwise GSL_CONTINUE is returned.       if (status == GSL_SUCCESS)        {          printf ("converged to minimum at\n");        }      f1 = fopen( "111iters_2K.txt", "a+");     fprintf (f1, "%d %10.10e  %10.10e %10.10e LogLike() = %10.10f, ABS. TOLERANCE = %10.10f\n", iter,              gsl_vector_get (s->x, 0), gsl_vector_get (s->x, 1), gsl_vector_get (s->x, 2), s->fval, size);  //    fclose(f1);    }  while (status == GSL_CONTINUE && iter < max_iter);  output[0] = gsl_vector_get (s->x, 0); //nu  output[1] = gsl_vector_get (s->x, 1); //theta  output[2] = gsl_vector_get (s->x, 2); //sigma2    gsl_vector_free(x);  gsl_vector_free(ss);  gsl_multimin_fminimizer_free (s);      return status;}double compute_Bessel ( double  d,                 double  pho,                 double  nu,                 double  sigma2 ){    const double temp =  d / pho;    if (d<1e-16)        return sigma2;    else    {        return sigma2 * gsl_sf_bessel_Knu(nu, temp) * std::pow(temp, nu) / std::pow(2.0, nu-1) / gsl_sf_gamma(nu);        // return sigma2 * boost::math::cyl_bessel_k( nu, temp ) * std::pow(temp, nu) / std::pow(2.0, nu-1) / boost::math::tgamma( nu );    }// else}//// covariance coefficient function//class TCovCoeffFn : public TPermCoeffFn< real_t >{private:    const double                _length;    const double                _nu;    const double                _sigma2;    const double                _nugget;    const vector< double * > &  _vertices;public:    // constructor    TCovCoeffFn ( const double               length,                  const double               nu,                  const double               sigma2,                  const double               nugget,                  const vector< double * > & vertices,                  const TPermutation *       row_perm,                  const TPermutation *       col_perm )            : TPermCoeffFn< real_t >( row_perm, col_perm )            , _length( length )            , _nu( nu )            , _sigma2( sigma2 )            , _nugget( nugget )            , _vertices( vertices )    {}    //    // coefficient evaluation    //    virtual void eval  ( const std::vector< idx_t > &  rowidxs,                         const std::vector< idx_t > &  colidxs,                         real_t *                      matrix ) const    {        const size_t  n = rowidxs.size();        const size_t  m = colidxs.size();        for ( size_t  j = 0; j < m; ++j )        {            const idx_t     idx1 = colidxs[ j ];            const double *  y    = _vertices[ idx1 ];                        for ( size_t  i = 0; i < n; ++i )            {                const idx_t     idx0 = rowidxs[ i ];                const double *  x    = _vertices[ idx0 ];                const double    dist = Math::sqrt( Math::square( x[0] - y[0] ) + Math::square( x[1] - y[1] ) );                matrix[ j*n + i ] = compute_Bessel( dist, _length, _nu, _sigma2 );  //                if(j==i)                   //matrix[ j*n + i ] = matrix[ j*n + i ] + _nugget ;                  //if(idx0==idx1)                //    matrix[ j*n + i ] =100;               // else                //    matrix[ j*n + i ] =0;                                }// for        }// for    }    using TPermCoeffFn< real_t >::eval;    //    // return format of matrix, e.g. symmetric or hermitian    //    virtual matform_t  matrix_format  () const { return symmetric; }    };//Use a method described by Abramowitz and Stegun: double gaussrand_Stegun(){    static double U, V;    static int phase = 0;    double Z;    if(phase == 0) {        U = (rand() + 1.) / (RAND_MAX + 2.);        V = rand() / (RAND_MAX + 1.);        Z = sqrt(-2 * log(U)) * sin(2 * M_PI * V);    } else        Z = sqrt(-2 * log(U)) * cos(2 * M_PI * V);    phase = 1 - phase;    return Z;}//Use a method discussed in Knuth and due originally to Marsaglia:double gaussrand_Knuth(){    static double V1, V2, S;    static int phase = 0;    double X;    if(phase == 0) {        do {            double U1 = (double)rand() / RAND_MAX;            double U2 = (double)rand() / RAND_MAX;            V1 = 2 * U1 - 1;            V2 = 2 * U2 - 1;            S = V1 * V1 + V2 * V2;        } while(S >= 1 || S == 0);        X = V1 * sqrt(-2 * log(S) / S);    } else        X = V2 * sqrt(-2 * log(S) / S);    phase = 1 - phase;    return X;}doubleeval_logli (const gsl_vector *sol, void* p){    CFG::set_verbosity( 1 );//    string  datename = "c/meshnew/moist_tri16000.txt";  //  if ( argc > 1 )  //      datename = argv[1];    pmy_f_params params  ;    double nu = gsl_vector_get(sol, 0);    double length = gsl_vector_get(sol, 1);    double sigma2 = gsl_vector_get(sol, 2);    unique_ptr< TProgressBar >  progress( verbose(2) ? new TConsoleProgressBar : nullptr );        params = (pmy_f_params)p;    TScalarVector rhs= (params->Z);    //std::unique_ptr< TClusterTree >   ct= (params->ct);    //std::unique_ptr< TBlockClusterTree >   bct= (params->bct);    //auto  ct= (params->ct);    //auto   bct= (params->bct);    TBlockClusterTree* bct = (params->bct);    TClusterTree* ct = (params->ct);        vector< double * > vertices= (params->vertices);    int                 dim = 2;    int                 N   = 0;    double err2=0.0;              // flatten_leaf( bct->root() );        //TPSMatrixVis  mvis_struct, mvis_mem;        //mvis_struct.structure( true ).id( true ).svd( false );    //mvis_mem.mem_col( true, "coolwarm" ).structure( false ).svd( false );        double  nugget = 1.0e-4;    double sizeA=0.0;    FILE *f4;    auto                        acc = fixed_prec( 1e-5 );    //  auto                        acc_ref = fixed_prec( 1e-12 );    TCovCoeffFn                 coefffn( length,                                             nu,                                             sigma2,                                             nugget,                                             vertices,                                             ct->perm_i2e(), ct->perm_i2e() );    TACAPlus< real_t >          aca( & coefffn );    TDenseMatBuilder< real_t >  h_builder( & coefffn, & aca );          // enable coarsening during construction    h_builder.set_coarsening( true );                                                auto  A = h_builder.build( bct, acc, progress.get() );     TPSMatrixVis              mvis;   TPSMatrixVis  mvis_struct, mvis_mem;    //    mvis_struct.structure( true ).id( true ).svd( true );//    mvis_mem.mem_col( true, "coolwarm" ).structure( true ).svd( true );        mvis.svd(true).print( A.get(), "ACov2000_2" );         flag8=1;    N=A->cols();    //printf("%d \n", N);                                  sizeA =  A->byte_size();//      cout << "    size of H-matr ix = " << mem_per_dof( A ) << endl;      //        cout << "    |A|₂             = " << format( "%.6e" ) % norm_2( A.get() ) << endl;//        cout << "    |A|_F            = " << format( "%.6e" ) % norm_F( A.get() ) << endl;//        cout << "  factorization" << endl;             auto  A_copy  = A->copy();     auto  options = fac_options_t( progress.get() );        //! Extreme important!!!        options.eval = point_wise;        auto  A_inv = ldl_inv( A_copy.get(), acc, options );       mvis.svd(true).print( A_copy.get(), "ACov2000_Chol2" );         //cout << "    size of LU factor = " << mem_per_dof( A_copy ) << endl;       // err2=inv_approx_2( A.get(), A_inv.get() );       // cout << "    inversion error   = " << format( "%.6e" ) % err2 << endl;        double  s = 0.0;                for ( int  i = 0; i < N; ++i )        {            const auto  v = A_copy->entry( i, i );                        s = s + log(v); // should be 2*log(v) for Cholesky      //      printf(" s=%12.10e, v=%10.4e \n", s, v);        }// for        //std::cout << "    log det C (from LDL^T) = " << format( "%.6e" ) % s << std::endl;        //        // solve RHS        //        //cout << "  solve RHS" << endl;//        TStopCriterion  sstop( 250, 1e-16, 0.0 );        TStopCriterion  sstop( 150, 1e-6, 0.0 );        TCG             solver( sstop );        TSolverInfo     sinfo( false, verbose( 4 ) );        auto            solu = A->row_vector();                solver.solve( A.get(), solu.get(), & rhs, A_inv.get(), & sinfo );       // std::cout << "    " << sinfo << std::endl;                auto            dotp = re( rhs.dot( solu.get() ) );        auto            LL = 0.5 * N * log( 2.0 * Math::pi<double>() ) + 0.5*s + 0.5*dotp;      //  std::cout << "    quadratic form = " << format( "%.8e" ) % dotp << std::endl;       // std::cout << "    LogLi = " << format( "%.8e" ) % LL << std::endl;//        f4 = fopen( "111_out.txt", "a+");//        fprintf(f4, "  %12.10e %12.10e %12.10e %12.10e %12.10e %12.10e\n",  nu, length, sigma2, s, dotp, LL); //        fclose(f4);    return LL;}intmain ( int argc, char ** argv ){    INIT();//    char datename[120];    string datename="/home/lit/111gsl/data_ying/Synthetic_sets/synthetic_4000_";    CFG::set_verbosity( 1 );  //  if ( argc > 1 )  //      datename = argv[1];        unique_ptr< TProgressBar >  progress( verbose(2) ? new TConsoleProgressBar : nullptr );        //    // read coordinates    //    vector< double * >  vertices;    TScalarVector       rhs;    int                 dim = 2;    int                 N   = 0;    double err2=0.0;        double  sigma2 = 1.0;    double  length = 0.09;     double  nu     = 0.5;    //double  nugget = 1.0e-7;    FILE *f4;  for ( int  jj = 1; jj < 2; jj++ )   {         INIT();      stringstream ss;      ss << jj;      datename.append(ss.str());      datename.append(".txt");      cout << datename <<endl;    {        ifstream  in( datename.c_str() );        if ( ! in )        {            cout << "error opening " << datename << endl;            exit( 1 );        }// if        in >> N;        cout << "  reading " << N << " coordinates" << endl;                vertices.resize( N );        rhs.set_size( N );                for ( int  i = 0; i < N; ++i )        {            int     index, property=1;            double  x, y, v=0.0;            in >> x >> y >> v;          //  in >> index >> x >> y >> property >> v;           // in >> index >> x >> y;            vertices[i] = new double[ dim ];            vertices[i][0] = x;            vertices[i][1] = y;//            printf("%6.6g, %6.6g, %6.6g, \n ", x,y,v);            rhs.set_entry( i, v );                    }// for    }           //  cout << "  reading " << N << " coordinates" << endl;         TCoordinate  coord( vertices, dim );    //print_vtk( & coord, "coord" );            //    // clustering    //    TAutoBSPPartStrat   part_strat( adaptive_split_axis );    TBSPCTBuilder       ct_builder( & part_strat );    auto                ct = ct_builder.build( & coord ); //virtual std::unique_ptr< TClusterTree >         TStdGeomAdmCond     adm_cond( 2.0, use_min_diam );    TBCBuilder          bct_builder( std::log2( 16 ) );    auto                bct = bct_builder.build( ct.get(), ct.get(), & adm_cond );        // bring RHS into H-ordering    ct->perm_e2i()->permute( & rhs );//    TPSMatrixVis  mvis_struct, mvis_mem;      //  mvis_struct.structure( true ).id( true ).svd( false );    //mvis_mem.mem_col( true, "coolwarm" ).structure( false ).svd( false );       double* output=NULL;       output=(double*)malloc(3*sizeof(double));   output[0]=0.0;   output[1]=0.0;   output[2]=0.0;   TTimer                    timer( WALL_TIME ); //  timer.start();        call_compute_max_likelihood(rhs, nu, length, sigma2, bct.get(), ct.get(),  vertices, output); //  std::cout << "  MLE estimate is found in " << timer  << std::endl;    f4 = fopen( "111_synt_2K.txt", "a+");    fprintf(f4, "  %d   0  %12.10e %12.10e %12.10e \n", N, output[0], output[1], output[2]);     fclose(f4);    free(output);    datename="/home/litvina/111gsl/data_ying/Synthetic_sets/synthetic_4000_";        DONE(); }        return 0;}