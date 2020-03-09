/* mockturtle: C++ logic network library
 * Copyright (C) 2018-2019  EPFL
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string>
#include <vector>

#include <fmt/format.h>
#include <lorina/lorina.hpp>
#include <mockturtle/algorithms/xmg_resub.hpp>
#include <mockturtle/algorithms/cleanup.hpp>
#include <mockturtle/algorithms/resubstitution.hpp>
#include <mockturtle/algorithms/node_resynthesis.hpp>
#include <mockturtle/algorithms/xmg_optimization.hpp>
#include <mockturtle/algorithms/cut_rewriting.hpp>
#include <mockturtle/algorithms/node_resynthesis/xmg3_npn.hpp>
#include <mockturtle/io/aiger_reader.hpp>
#include <mockturtle/io/verilog_reader.hpp>
#include <mockturtle/io/blif_reader.hpp>
#include <mockturtle/io/bench_reader.hpp>
#include <mockturtle/networks/xmg.hpp>
#include <mockturtle/networks/aig.hpp>
#include <mockturtle/properties/xmgcost.hpp>
#include <mockturtle/io/write_verilog.hpp>
#include <mockturtle/views/topo_view.hpp>


#include <experiments.hpp>

int main()
{
    using namespace experiments;
    using namespace mockturtle;
  
  experiment<std::string, uint32_t, float, std::string, std::string, std::string, float, bool> exp( "xmg_resubstituion", "benchmark", "tot_it", "size_impr", "runtime rw/rs", "sd", " sd'", "area_impr", "equivalent" );

  for ( auto const& benchmark : epfl_benchmarks() )
  {
    //if (benchmark != "voter" && benchmark != "div" && 
    //        if( benchmark != "ctrl" ) 
    //    continue;
    fmt::print( "[i] processing {}\n", benchmark );
    
    abc_lut_reader_mf ( benchmark );

    klut_network klut;
    auto result = lorina::read_bench( benchmark_path( benchmark, "_mf_bench", "bench" ), mockturtle::bench_reader( klut ) );
    
    if (result == lorina::return_code::parse_error)
    {
        std::cout << "Parsing error in Verilog" << std::endl;
        continue;
    }

    xmg_network xmg;

    mockturtle::xmg3_npn_resynthesis<xmg_network> resyn2;
    mockturtle::node_resynthesis( xmg, klut, resyn2 );
    const auto cec3 = benchmark == "hyp" ? true : abc_cec( xmg, benchmark );

    topo_view topo(xmg);
    xmg = cleanup_dangling(xmg);
    const auto cec4 = benchmark == "hyp" ? true : abc_cec( xmg, benchmark );

    std::cout << "no of gates in XMG   "  << xmg.num_gates() << std::endl;

    //Set the genlib path here used with ABC
    std::string const genlib_path = "/home/shubham/My_work/abc-vlsi-cad-flow/std_libs/date_lib_count_tt_4.genlib";

    float area_before = abc_techmap( xmg, genlib_path );

    xmg_cost_params ps1, ps2;
    int32_t size_before, size_after, size_per_iteration;
    uint32_t num_iters = 0;
    float rs = 0;
    float rw = 0;
    float runtime = 0;
    bool equiv = true;
    
    // XMG resubstitution parameter set
    resubstitution_params resub_ps;
    resubstitution_stats resub_st;
    resub_ps.max_pis = 8u;
    //resub_ps.progress = true;
    resub_ps.max_inserts = 1u;  
    resub_ps.use_dont_cares = true; 
    resub_ps.window_size = 12u;  

    // XMG rewriting parameter set
    cut_rewriting_params cr_ps;
    cut_rewriting_stats cr_st;
    cr_ps.cut_enumeration_ps.cut_size = 4;
    //cr_ps.progress = true;

    std::cout << "Before Optimizations" <<  std::endl; 
    ps1.reset();
    num_gate_profile( xmg, ps1 );
    ps1.report();
    size_before = xmg.num_gates();
    double sd_rat = ( double( ps1.actual_maj + ps1.actual_xor3 )/  size_before ) * 100;
    std::string sd_before = fmt::format( "{}/{} = {}", ( ps1.actual_maj + ps1.actual_xor3 ),  size_before, sd_rat);
    float total_imp;

    do 
    {
        num_iters++;
        size_per_iteration = xmg.num_gates();

        xmg3_npn_resynthesis<xmg_network> resyn;
        cut_rewriting( xmg, resyn, cr_ps, &cr_st );
        xmg = cleanup_dangling( xmg );

        const auto cec2 = benchmark == "hyp" ? true : abc_cec( xmg, benchmark );

        xmg_resubstitution(xmg, resub_ps, &resub_st);
        xmg = cleanup_dangling( xmg );
    
        const auto cec = benchmark == "hyp" ? true : abc_cec( xmg, benchmark );

        if (size_per_iteration == 0u)
          total_imp = 0;
        else
        {
          int diff = size_per_iteration - xmg.num_gates();
          total_imp = 100 * (double(std::abs(diff))/size_per_iteration);
        }
        
        rw += to_seconds( cr_st.time_total );
        rs += to_seconds( resub_st.time_total );
        equiv &= cec2 & cec;
        std::cout << "eqivalent before " << cec3 << "equivalence after topp " << cec4 << " equivalence check after rs  " << cec2  << " after rw " << cec << std::endl;

    } while ( total_imp > 0.5 );

    size_after = xmg.num_gates();
    float final_improvement = ( double( std::abs( int( size_after - size_before ) ) ) / size_before ) * 100 ;

    std::cout << "After Optimizations" <<  std::endl; 
    ps2.reset();
    num_gate_profile( xmg, ps2);
    ps2.report();
    sd_rat = ( double( ps2.actual_maj + ps2.actual_xor3 )/  size_after ) * 100;
    std::string sd_after = fmt::format( "{}/{} = {}", ( ps2.actual_maj + ps2.actual_xor3 ),  size_after, sd_rat );
    
    auto area_after= abc_techmap( xmg, genlib_path );
    float area_imp = ( ( area_before - area_after ) / area_before ) * 100 ; 

    std::string rt = fmt::format( " {:>5.2f} / {:>5.2f}" , rw, rs  );
    exp ( benchmark, num_iters, final_improvement, rt, sd_before, sd_after, area_imp, equiv );
  }
  
  exp.save();
  exp.table();
  return 0;
}
