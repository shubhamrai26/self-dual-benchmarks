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

#include "mockturtle/networks/aig.hpp"
#include "mockturtle/networks/xmg.hpp"
#include "mockturtle/algorithms/node_resynthesis.hpp"
#include "mockturtle/algorithms/node_resynthesis/xmg3_npn.hpp"
#include "mockturtle/algorithms/cleanup.hpp"
#include "mockturtle/io/aiger_reader.hpp"
#include "mockturtle/properties/xmgcost.hpp"

#include "experiments.hpp"

#include <lorina/lorina.hpp>
#include <fmt/format.h>

void experiment1()
{
  experiments::experiment<std::string, std::string, std::string, float, bool>
    exp( "node_resynthesis", "benchmark", "AIG gates [= ANDs]", "XMG gates [= XOR3s + MAJs]", "runtime", "equivalent" );
  for ( auto const& benchmark : experiments::epfl_benchmarks() )
  {
    fmt::print( "[i] processing {}\n", benchmark );

    /* read the benchmarks */
    mockturtle::aig_network aig;
    auto const result = lorina::read_aiger( experiments::benchmark_path( benchmark ), mockturtle::aiger_reader( aig ) );
    if ( result != lorina::return_code::success )
    {
      fmt::print( "[e] reading benchmark {} failed\n[e] continuing with the next benchmark file\n", benchmark );
      continue;
    }

    /* resynthesize the benchmarks */
    mockturtle::xmg_network xmg;
    mockturtle::xmg3_npn_resynthesis<mockturtle::xmg_network> resyn;

    mockturtle::node_resynthesis_params ps;
    mockturtle::node_resynthesis_stats st;
    mockturtle::node_resynthesis( xmg, aig, resyn, ps, &st );

    /* profile XMG gates */
    mockturtle::xmg_cost_params xmg_st;
    num_gate_profile( xmg, xmg_st );

    /* verify reasults using ABC's CEC command */
    auto const cec = benchmark == "hyp" ? true : experiments::abc_cec( xmg, benchmark );

    /* fill benchmark table */
    exp( benchmark,
         /* AIG: */ fmt::format( "{}", aig.num_gates() ),
         /* XMG: */ fmt::format( "{} = {} + {}", xmg.num_gates(), xmg_st.total_xor3, xmg_st.total_maj ),
         /* runtime */ mockturtle::to_seconds( st.time_total ),
         /* verify: */ cec );
  }

  exp.save();
  exp.table();
}

int main()
{
  /* experiment #1: node resynthesis of EPFL benchmarks given as AIGs into X3MGs using NPN4-DB generated with exact synthesis */
  experiment1();

  return 0;
}
