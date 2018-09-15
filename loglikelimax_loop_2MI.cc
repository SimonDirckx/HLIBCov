﻿//// Project     : HLib// File        : loglikelihood.cc// Description : example for 1d BEM problem// Author      : Alexander Litvinenko, Ronald Kriemann// Copyright   : Max Planck Institute MIS 2004-2018. All Rights Reserved.//#include <iostream>#include <fstream>#include <sstream>#include <vector>#include <boost/format.hpp>#include <boost/program_options.hpp>#include <boost/math/tools/minima.hpp>#include <gsl/gsl_sf_bessel.h>#include <gsl/gsl_sf_gamma.h>#include <gsl/gsl_multimin.h>#include <hlib.hh>//#include <math.h>using namespace std;using boost::format;using namespace boost::program_options;using namespace HLIB;using real_t = HLIB::real;template < typename T >stringmem_per_dof ( T && A ){    const size_t  mem  = A->byte_size();    const size_t  pdof = size_t( double(mem) / double(A->rows()) );    return Mem::to_string( pdof );    // return Mem::to_string( mem );}template < typename T >stringmem_total ( T && A ){    const size_t  mem  = A->byte_size();    return Mem::to_string( mem );}//// covariance coefficient function//class TMaternCovarianceCoeffFn : public TPermCoeffFn< real_t >{private:    const double               _length;    const double               _nu;    const double               _sigmasq;    const double               _nugget;    const double               _scale_fac;    const vector< T2Point > &  _vertices;public:    // constructor    TMaternCovarianceCoeffFn ( const double               length,                               const double               nu,                               const double               sigma,                               const double               nugget,                               const vector< T2Point > &  vertices,                               const TPermutation *       row_perm,                               const TPermutation *       col_perm )        : TPermCoeffFn< real_t >( row_perm, col_perm )        , _length( length )        , _nu( nu )        , _sigmasq( sigma*sigma )        , _nugget( nugget )        , _scale_fac( _sigmasq / ( std::pow(2.0, nu-1) * gsl_sf_gamma(nu) ) )        , _vertices( vertices )    {}    //    // coefficient evaluation    //    virtual void eval  ( const std::vector< idx_t > &  rowidxs,                         const std::vector< idx_t > &  colidxs,                         real_t *                      matrix ) const    {        const size_t  n = rowidxs.size();        const size_t  m = colidxs.size();        for ( size_t  j = 0; j < m; ++j )        {            const idx_t    idx1 = colidxs[ j ];            const T2Point  y    = _vertices[ idx1 ];                        for ( size_t  i = 0; i < n; ++i )            {                const idx_t    idx0 = rowidxs[ i ];                const T2Point  x    = _vertices[ idx0 ];                const double   dist = norm2( x - y ); // Math::sqrt( Math::square( x[0] - y[0] ) + Math::square( x[1] - y[1] ) );                matrix[ j*n + i ] = compute_Bessel( dist, _length, _nu, _sigmasq );              }// for        }// for    }    using TPermCoeffFn< real_t >::eval;    //    // compute modified bessel function    //    double     compute_Bessel ( const double  d,                     const double  pho,                     const double  nu,                     const double  sigmasq ) const    {        if ( d < 1e-16 )            return sigmasq;        else        {            const double temp =  d / pho;            return _scale_fac * gsl_sf_bessel_Knu( nu, temp ) * std::pow( temp, nu );        }// else    }    //    // return format of matrix, e.g. symmetric or hermitian    //    virtual matform_t  matrix_format  () const { return symmetric; }    };//// read data from file//voidread_data ( const string &            datafile,            vector< T2Point > &       vertices,            BLAS::Vector< double > &  Z_data ){    ifstream  in( datafile.c_str() );    string    line;        if ( ! in )    {        std::cout << "error opening " << datafile << std::endl;        exit( 1 );    }// if    size_t  N = 0;        {        std::getline( in, line );        std::istringstream  sline( line );                sline >> N;    }    std::cout << "  reading " << N << " coordinates" << std::endl;            vertices.resize( N );    Z_data = BLAS::Vector< double >( N );            for ( idx_t  i = 0; i < idx_t(N); ++i )    {        std::getline( in, line );        std::istringstream  sline( line );                        int     index    = i, flag=0;        double  x, y;        double  v        = 0.0;        // int     property = 1;        // sline >> index >> x >> y >> flag >> v;        sline >> x >> y >> v;        vertices[index] = T2Point( x, y );        Z_data(index)   = v;    }// for}//// print matrix in different formats//voidprint_matrix ( const TMatrix *  A,               const string &   name ){    TPSMatrixVis  mvis_str, mvis_mem;        mvis_str.color( false ).structure( true ).id( true ).svd( false );    mvis_mem.mem_col( true ).structure( false ).svd( false );            mvis_str.print( A, "loglikelihood_" + name + "s" );    mvis_mem.print( A, "loglikelihood_" + name + "m" );}//// Functor for Loglikelyhood Problem//struct LogLikeliHoodProblem{    const double               nugget;    const vector< T2Point > &  vertices;    const TPermutation *       perm_i2e;    const TBlockClusterTree *  bct;    const double               eps;    const double               epslu;    const double               epsabs;    const double               shift;    const bool                 use_ldl;    const TVector *            Z;    LogLikeliHoodProblem ( const double               anugget,                           const vector< T2Point > &  avertices,                           const TPermutation *       aperm_i2e,                           const TBlockClusterTree *  abct,                           const double               aeps,                           const double               aepslu,                           const double               aepsabs,                           const double               ashift,                           const bool                 ause_ldl,                           const TVector *            aZ )            : nugget( anugget )            , vertices( avertices )            , perm_i2e( aperm_i2e )            , bct( abct )            , eps( aeps )            , epslu( aepslu )            , epsabs( aepsabs )            , shift( ashift )            , use_ldl( ause_ldl )            , Z( aZ )    {}    //    // function operator for minimization algorithm    // - in: nu, covariance length and sigma    // - out: 1/2 ( n·log(2π) + log( det(C) ) + Z^T C^-1 Z )    //        double    eval ( const double  nu,           const double  covlength,           const double  sigma )    {        unique_ptr< TProgressBar >  progress( verbose(3) ? new TConsoleProgressBar : nullptr );        if ( verbose(1) )            cout << "    ─┬ "                 << "ν = " << format( "%e" ) % nu << ", "                 << "θ = " << format( "%e" ) % covlength << ", "                 << "σ = " << format( "%e" ) % sigma                 << endl;        if ( verbose(1) )        {                        cout << "     ├╴building H-matrix ( relative ε = " << format( "%.1e" ) % eps << " )" << endl;            cout << "     ├╴building H-matrix ( abs ε = " << format( "%.1e" ) % epsabs << " )" << endl;            cout << "     ├╴size =" << format( "%d" ) % vertices.size()  << endl;        }            auto                        acc = fixed_prec( eps, epsabs  );        //auto acc = fixed_rank( eps, 1e-8 );        TMaternCovarianceCoeffFn    coefffn( covlength, nu, sigma, shift, vertices, perm_i2e, perm_i2e );        TACAPlus< real_t >          aca( & coefffn );        TDenseMatBuilder< real_t >  h_builder( & coefffn, & aca );        auto  tic = Time::Wall::now();            auto  C   = h_builder.build( bct, acc, progress.get() );        cout << " Size n= " << format("%d") % C->row_is() <<endl;        auto  toc = Time::Wall::since( tic );        if ( verbose(2) )            cout << "     │ └╴done in " << toc << ", size = " << mem_per_dof( C ) << std::endl;        if ( verbose(2) )            cout << "     │ └╴done in " << toc << ", size = " << mem_total( C ) << std::endl;        if ( verbose(4) )            print_matrix( C.get(), "C" );        //        // factorise        //        if ( shift != 0.0 )        {            if ( verbose(1) )                std::cout << std::endl << "     ├╴regularization ( C + λ·I, λ = " << format( "%.1e" ) % shift                          << " )" << std::endl;            add_identity( C.get(), shift );        }// if            //        // factorise        //            if ( verbose(1) )            cout << "     ├╴" << ( use_ldl ? "LDL" : "Cholesky" ) << " factorisation"                 << " ( ε = " << format( "%.1e" ) % epslu << " )" << std::endl;        auto  C_fac       = C->copy();        auto  fac_acc     = fixed_prec( epslu );//        auto  fac_acc     = fixed_rank( epslu, epsabs );        auto  fac_options = fac_options_t( progress.get() );            // important since we need the diagonal elements        fac_options.eval = point_wise;        tic = Time::Wall::now();        if ( use_ldl )            ldl( C_fac.get(), fac_acc, fac_options );        else            chol( C_fac.get(), fac_acc, fac_options );        toc = Time::Wall::since( tic );        unique_ptr< TFacInvMatrix >  C_inv;        if ( use_ldl )            C_inv = make_unique< TLDLInvMatrix >( C_fac.get(), symmetric, point_wise );        else            C_inv = make_unique< TLLInvMatrix >( C_fac.get(), symmetric );            if ( verbose(2) )        {                cout << " Size n= " << format("%d") % C->row_is() <<endl;            cout << "     │ └╴done in " << toc << ", size = " << mem_per_dof( C_fac ) << ", "                 << "error = " << format( "%.6e" ) % inv_approx_2( C.get(), C_inv.get() ) << std::endl;        }        if ( verbose(2) )            cout << mem_total( C_fac ) << std::endl;                                          if ( verbose(4) )            print_matrix( C_fac.get(), "L" );        //        // compute log( det( A ) ) = log( det( L L ) )   = 2*log( det( L ) )        // or                      = log( det( L D L ) ) =   log( det( D ) )        //                double  log_det_C = 0.0;                for ( auto  i : C->row_is() )        {            const auto  c_ii = C_fac->entry( i, i );            if ( use_ldl ) log_det_C += std::log( c_ii );            else           log_det_C += 2*std::log( c_ii );        }// for        if ( verbose(1) )            cout << "     ├╴log(det(C)) = " << format( "%.6e" ) % log_det_C << std::endl;                //        // compute C^-1 Z        //        if ( verbose(1) )            cout << "     ├╴computing C^-1 Z" << endl;        CFG::Solver::use_exact_residual = true;                TStopCriterion  sstop( 250, 1e-16, 0.0 );        TCG             solver( sstop );        TSolverInfo     sinfo( false, verbose( 4 ) );        auto            sol = C->row_vector();                solver.solve( C.get(), sol.get(), Z, C_inv.get(), & sinfo );        if ( verbose(2) )            cout << "     │ └╴" << sinfo << std::endl;                auto            ZdotCZ = re( Z->dot( sol.get() ) );        const size_t    N      = C->rows();        auto            LL     = - 0.5 * ( N * std::log( 2.0 * Math::pi<double>() ) + log_det_C + ZdotCZ );        if ( verbose(1) )            cout << "     └╴" << "LogLi = " << format( "%.8e" ) % LL << std::endl;                //FILE* f4 = fopen( "111res_1May2018_LL_vs_eps.txt", "a+");        //fprintf(f4, "  %d %3.3e %3.3e %4.4e %4.4e %4.4e %4.4e %4.4e %4.4e  \n", N, eps, //epslu, epsabs, nu, covlength, sigma,        //        LL,  log_det_C, ZdotCZ);         //fclose(f4);                     return -LL;    }    double    operator () ( const T3Point &  param )    {        return eval( param[0], param[1], param[2] );    }};//// maximization routine using Simplex algorithm//doublemaximize_likelihood_simplex ( double &                   nu,                              double &                   covlength,                              double &                   sigma,                              LogLikeliHoodProblem &     problem );//// main function//intmain ( int      argc,       char **  argv ){    INIT();        string  datafile = argv[1];    double  eps       = 1e-1;    double  fac_eps   = 1e-1;    double  epsabs   = 1e-1;    double  shift     = 0.0;    bool    use_ldl   = false;        //    // define command line options    //    options_description             all_opts;    options_description             vis_opts( "usage: loglikelihood [options] datafile\n  where options include" );    options_description             hid_opts( "Hidden options" );    positional_options_description  pos_opts;    variables_map                   vm;    // standard options    vis_opts.add_options()        ( "help,h",                       ": print this help text" )        ( "threads,t",   value<int>(),    ": number of parallel threads" )        ( "verbosity,v", value<int>(),    ": verbosity level" )        ( "eps,e",       value<double>(), ": set relative H accuracy" )        ( "epslu",       value<double>(), ": set only relative H factorization accuracy" )        ( "epsabs",      value<double>(), ": set absolute H factorization accuracy" )        ( "shift",       value<double>(), ": regularization parameter" )        ( "ldl",                          ": use LDL factorization" )        ;        hid_opts.add_options()        ( "data",        value<string>(), ": datafile defining problem" )        ;    // options for command line parsing    all_opts.add( vis_opts ).add( hid_opts );    // all "non-option" arguments should be "--data" arguments    pos_opts.add( "data", -1 );    //    // parse command line options    //    try    {        store( command_line_parser( argc, argv ).options( all_opts ).positional( pos_opts ).run(), vm );        notify( vm );    }// try    catch ( required_option &  e )    {        std::cout << e.get_option_name() << " requires an argument, try \"-h\"" << std::endl;        exit( 1 );    }// catch    catch ( unknown_option &  e )    {        std::cout << e.what() << ", try \"-h\"" << std::endl;        exit( 1 );    }// catch    //    // eval command line options    //    if ( vm.count( "help") )    {        std::cout << vis_opts << std::endl;        exit( 1 );    }// if    if ( vm.count( "eps"       ) ) eps      = vm["eps"].as<double>();    if ( vm.count( "epslu"     ) ) fac_eps  = vm["epslu"].as<double>();    if ( vm.count( "epsabs"    ) ) epsabs   = vm["epsabs"].as<double>();    if ( vm.count( "shift"     ) ) shift    = vm["shift"].as<double>();    if ( vm.count( "threads"   ) ) CFG::set_nthreads( vm["threads"].as<int>() );    if ( vm.count( "verbosity" ) ) CFG::set_verbosity( vm["verbosity"].as<int>() );    if ( vm.count( "ldl"       ) ) use_ldl  = true;    // default to general eps    if ( fac_eps == -1 )        fac_eps = eps;    if ( epsabs == -1 )        epsabs = 1e-15;        if ( vm.count( "data" ) )        datafile = vm["data"].as<string>();    else    {        std::cout << "usage: loglikelihood [options] datafile" << std::endl;        exit( 1 );    }// if    unique_ptr< TProgressBar >  progress( verbose(2) ? new TConsoleProgressBar : nullptr );        //    // read coordinates    //                       //string datename="/home/litvina/111gsl/data_ying/WHOLE_DOMAIN_MESHES/Nest_whole_moist_256000_1.txt";   //string datename="/home/litvina/111gsl/data_ying/moist_tri512812.txt";   //  string datename="/home/litvina/111gsl/data_ying/moist_tri1000000short.txt";   //string datename="/home/litvina/Dropbox/111Hcov_paper/111exact_data/sim_data_hlibpro.txt";   //cout << datename <<endl;                   double  nu     = 0.225;//0.325;//0.5;  //0.325;//0.84; //0.9    double  length = 0.58;  //3.1;  //0.988;//0.62;//0.64; //0.7     double  sigma  = 1.02; //1.2;  //3.0;  //1.0;//0.94; //1.0    double  nugget = 0.0;    FILE* f4;            //sprintf(datafile1, "111_7Mai2018_vs_eps_%d.txt", ii);  //sprintf(datafile0, "/home/litvina/111gsl/data_ying/Synthetic28April2018/K%d_1e-8_%d.txt", i3, ii);        //sprintf(datafile1, "111_12Mai2018_vs_eps_2MI.txt");        //char datafile0[100];  //sprintf(datafile0, "/home/litvina/111gsl/data_ying/moist_tri2000000short.txt");        //sprintf(datafile0, "/home/litvina/111gsl/data_ying/Synthetic28April2018/K2000_1e-8.txt");    //datafile.assign(datafile0, (datafile.length()+3));      //std::cout << "━━ reading data ( " << datafile << " )" << std::endl;  //    cout << datafile <<endl;      //FILE *f4;      //f4 = fopen( datafile, "a+");    vector< T2Point >       vertices;    BLAS::Vector< double >  Z_data;            // //    read_data( "/home/litvina/111gsl/data_ying/moist_tri99999.txt", vertices, Z_data );//    read_data( "/home/litvina/111gsl/data_ying/moist_tri2000000short.txt", vertices, Z_data );//    read_data( "/home/litvina/111gsl/data_ying/nonnested_moist_whole/moist_2000_1.txt", vertices, Z_data );//    read_data( "/home/litvina/111gsl/data_ying/moist_tri512812.txt", vertices, Z_data );//read_data( "/home/litvina/111gsl/data_ying/May15_2018/Nest_whole_moist_2000K_1.txt", vertices, Z_data );read_data( "/home/litvina/111gsl/data_ying/May15_2018/Nest_whole_moist_512K_1.txt", vertices, Z_data );//read_data( "/home/litvina/111gsl/data_ying/May15_2018/Nest_whole_moist_1000K_1.txt", vertices, Z_data );//read_data( "/home/litvina/111gsl/data_ying/WHOLE_DOMAIN_MESHES/Nest_whole_moist_2000000_2.txt", vertices, Z_data );//read_data( "/home/litvina/111gsl/data_ying/May15_2018/Nest_whole_moist_128K_1.txt", vertices, Z_data );                        //    read_data( "/home/litvina/111gsl/data_ying/moist_tri9999.txt", vertices, Z_data );    int N = vertices.size();    TCoordinate  coord( vertices );    if ( verbose(4) )        print_vtk( & coord, "loglikelihood_coord" );            //    // set up H-matrix structure    //            TAutoBSPPartStrat   part_strat( adaptive_split_axis );    TBSPCTBuilder       ct_builder( & part_strat );    auto                ct = ct_builder.build( & coord );      //  TStdGeomAdmCond     adm_cond( 2.0, use_min_diam );    TStdGeomAdmCond     adm_cond( 2.0, use_max_diam );  //  TWeakStdGeomAdmCond     adm_cond( 2.0, use_min_diam );  //TOffDiagAdmCond     adm_cond;    TBCBuilder          bct_builder; // keep top levels leaf free    auto                bct = bct_builder.build( ct.get(), ct.get(), & adm_cond );    if ( verbose(4) )        print_ps( bct->root(), "loglikelihood_bct.eps" );    //    // set up vector Z    //    auto  Z = make_unique< TScalarVector >( *ct->root(), Z_data );        // bring vector into H-ordering    ct->perm_e2i()->permute( Z.get() );        //    // initial data for Matern covariance kernel    //        //    // maxmize loglikelyhood problem (use minimization)    //    std::cout << "━━ computing maximal LogLikelihood" << std::endl;        //  LogLikeliHoodProblem  problem( nugget, vertices, ct->perm_i2e(), bct.get(), eps*i2,  fac_eps*i2, epsabs*i2, shift, use_ldl, Z.get() );    LogLikeliHoodProblem  problem( nugget, vertices, ct->perm_i2e(), bct.get(), eps, fac_eps, epsabs, shift, use_ldl, Z.get() );        auto  tic = Time::Wall::now();    auto  res = maximize_likelihood_simplex( nu, length, sigma, problem );    auto  toc  = Time::Wall::since( tic );    auto  LL  = res;        cout << "  done in " << toc         << endl         << "  logliklihood at "         << " ν = " << format( "%.4e" ) % nu         << ", θ = " << format( "%.4e" ) % length         << ", σ = " << format( "%.4e" ) % sigma         << " is " << format( "%.4e" ) % LL         << endl;//      datafile.assign(datafile0, (datafile.length()+1));               f4 = fopen( "111_16Aug2018moist_2000K.txt", "a+");    fprintf(f4, " %d  %3.3e %3.3e %10.8e %10.8e %10.8e %10.8e %10.8e %6.5e \n",  N, eps, fac_eps, epsabs, nu, length, sigma*sigma, sigma, shift); //    fprintf(f4, "  %d %d  %3.3e %3.3e %10.8e %10.8e %10.8e %10.8e %10.8e %6.5e \n", j, N, //eps*i2, fac_eps*i2, epsabs*i2, nu, length, sigma*sigma, sigma, shift);  //   fprintf(f4, " %d  %12.10e %12.10e \n", jj, gl_min_nu, gl_min_LL);     fclose(f4);           DONE();      return 0;}//// evaluate loglikelihood for given triple (ν,θ,σ)//doubleeval_logli ( const gsl_vector *  param,             void *              data ){    double nu    = gsl_vector_get( param, 0 );    double theta = gsl_vector_get( param, 1 );    double sigma = gsl_vector_get( param, 2 );    LogLikeliHoodProblem *  problem = static_cast< LogLikeliHoodProblem * >( data );    return problem->eval( nu, theta, sigma );}//// maximization routine using Simplex algorithm//doublemaximize_likelihood_simplex ( double &                nu,                              double &                covlength,                              double &                sigma,                              LogLikeliHoodProblem &  problem ){    int        status   = 0;    const int  max_iter = 700;        const gsl_multimin_fminimizer_type *  T = gsl_multimin_fminimizer_nmsimplex2;    gsl_multimin_fminimizer *             s = NULL;    gsl_vector *                          ss;    gsl_vector *                          x;    gsl_multimin_function                 minex_func;    double                                size;      // start point    x = gsl_vector_alloc( 3 );    gsl_vector_set( x, 0, nu );    gsl_vector_set( x, 1, covlength );    gsl_vector_set( x, 2, sigma );      // set initial step sizes to 0.1    ss = gsl_vector_alloc( 3 );    gsl_vector_set( ss, 0, 0.01 ); // nu     gsl_vector_set( ss, 1, 0.02 ); // theta    gsl_vector_set( ss, 2, 0.02 ); // sigma    // Initialize method and iterate    minex_func.n      = 3;    minex_func.f      = & eval_logli;    minex_func.params = & problem;    s = gsl_multimin_fminimizer_alloc( T, 3 );    gsl_multimin_fminimizer_set(s, & minex_func, x, ss );    int     iter = 0;    double  LL = 0;        do    {        iter++;        status = gsl_multimin_fminimizer_iterate( s );              if ( status != 0 )             break;        size   = gsl_multimin_fminimizer_size( s );    // return eps for stopping criteria        status = gsl_multimin_test_size( size, 1e-6 ); // This function tests the minimizer specific characteristic size         if ( status == GSL_SUCCESS )            cout << "converged to minimum" << endl;        nu        = gsl_vector_get( s->x, 0 );        covlength = gsl_vector_get( s->x, 1 );        sigma     = gsl_vector_get( s->x, 2 );        LL        = s->fval;        cout << format( "%3d" ) % iter             << format( "  %.8e" ) % nu             << format( "  %.8e" ) % covlength             << format( "  %.8e" ) % sigma             << ", LogLikeliHood = " << format( "%.8e" ) % LL             << ", abs. tol. = " << format( "%.8e" ) % size             << endl;            } while (( status == GSL_CONTINUE ) && ( iter < max_iter ));    FILE* f4 = fopen( "111res_16Aug2018moist_iters_2000K.txt", "a+");    fprintf(f4, "  %d  %8.8e %8.8e %8.8e %8.8e %8.8e \n", iter,   nu, covlength, sigma, LL, size);  //   fprintf(f4, " %d  %12.10e %12.10e \n", jj, gl_min_nu, gl_min_LL);     fclose(f4);             gsl_multimin_fminimizer_free( s );    gsl_vector_free( ss );    gsl_vector_free( x );      return LL;}