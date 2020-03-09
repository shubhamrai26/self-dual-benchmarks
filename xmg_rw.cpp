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
#include "mockturtle/algorithms/cut_rewriting.hpp"
#include "mockturtle/algorithms/cleanup.hpp"
#include "mockturtle/io/aiger_reader.hpp"
#include "mockturtle/io/verilog_reader.hpp"
#include "mockturtle/properties/xmgcost.hpp"

#include "experiments.hpp"

#include <lorina/lorina.hpp>
#include <kitty/kitty.hpp>
#include <fmt/format.h>

template<typename Ntk>
bool read_benchmark( Ntk& ntk, std::string const& benchmark, std::string const& path_type = "", std::string const& file_type = "aig" )
{
  if ( file_type == "aig" )
  {
    auto const result = lorina::read_aiger( experiments::benchmark_path( benchmark, path_type, file_type ), mockturtle::aiger_reader( ntk ) );
    if ( result != lorina::return_code::success )
    {
      fmt::print( "[e] reading benchmark {} failed\n"
                  "[e] continuing with the next benchmark file\n", benchmark );
      return false;
    }
  }
  else if ( file_type == "v" )
  {
    auto const result = lorina::read_verilog( experiments::benchmark_path( benchmark, path_type, file_type ), mockturtle::verilog_reader( ntk ) );
    if ( result != lorina::return_code::success )
    {
      fmt::print( "[e] reading benchmark {} failed\n"
                  "[e] continuing with the next benchmark file\n", benchmark );
      return false;
    }
  }
  else
  {
    fmt::print( "[e] unsupported benchmark extension\n"
                "[e] continuing with the next benchmark file\n", benchmark );
    return false;
  }

  /* success */
  return true;
}

struct experiment1_params
{
  bool verify = true;
};

void experiment1( experiment1_params const& ep, std::vector<std::string> const& benchmarks = experiments::epfl_benchmarks(), std::string const& path_type = "", std::string const& file_type = "aig" )
{
  std::cout << "===========================================================================" << std::endl;
  std::cout << "EXPERIMENT#1: node_resynthesis" << std::endl;
  std::cout << "===========================================================================" << std::endl;

  experiments::experiment<std::string, std::string, std::string, float, bool>
    exp( "node_resynthesis", "benchmark", "AIG gates [= ANDs]", "XMG gates [= XOR3s + MAJs]", "runtime", "equivalent" );
  for ( auto const& benchmark : benchmarks )
  {
    fmt::print( "[i] processing {}\n", benchmark );

    /* read the benchmarks */
    mockturtle::aig_network aig;
    if ( !read_benchmark( aig, benchmark, path_type, file_type ) )
      continue;

    /* resynthesize the benchmarks */
    mockturtle::xmg_network xmg;
    mockturtle::xmg3_npn_resynthesis<mockturtle::xmg_network> resyn;

    mockturtle::node_resynthesis_params ps;
    mockturtle::node_resynthesis_stats st;
    mockturtle::node_resynthesis( xmg, aig, resyn, ps, &st );

    /* profile XMG gates */
    mockturtle::xmg_cost_params xmg_st;
    num_gate_profile( xmg, xmg_st );

    /* verify results using ABC's CEC command */
    auto const cec = ( !ep.verify || benchmark == "hyp" ) ? true : experiments::abc_cec( xmg, benchmark, path_type, file_type );

    /* fill benchmark table */
    exp( benchmark,
         /* AIG: */ fmt::format( "{:7d}", aig.num_gates() ),
         /* XMG: */ fmt::format( "{:7d} = {:7d} + {:7d}", xmg.num_gates(), xmg_st.total_xor3, xmg_st.total_maj ),
         /* runtime */ mockturtle::to_seconds( st.time_total ),
         /* verify: */ cec );
  }

  exp.save();
  exp.table();
}

struct experiment2_params
{
  uint32_t num_rewrite_times{1u};
  bool verify{true};
};

void experiment2( experiment2_params const& ep, std::vector<std::string> const& benchmarks = experiments::epfl_benchmarks(), std::string const& path_type = "", std::string const& file_type = "aig" )
{
  std::cout << "===========================================================================" << std::endl;
  std::cout << "EXPERIMENT#2: node_resynthesis & rewritiing" << std::endl;
  std::cout << "===========================================================================" << std::endl;

  experiments::experiment<std::string, std::string, std::string, float, bool>
    exp( "node_resynthesis", "benchmark", "AIG gates [= ANDs]", "XMG gates [= XOR3s + MAJs]", "runtime", "equivalent" );
  for ( auto const& benchmark : benchmarks )
  {
    fmt::print( "[i] processing {}\n", benchmark );

    /* read the benchmarks */
    mockturtle::aig_network aig;
    if ( !read_benchmark( aig, benchmark, path_type, file_type ) )
      continue;

    mockturtle::xmg_network xmg;
    mockturtle::xmg3_npn_resynthesis<mockturtle::xmg_network> resyn;

    /* prepare XMG using node resynthesis */
    mockturtle::node_resynthesis_params noderesyn_ps;
    mockturtle::node_resynthesis_stats noderesyn_st;
    mockturtle::node_resynthesis( xmg, aig, resyn, noderesyn_ps, &noderesyn_st );

    mockturtle::stopwatch<>::duration rewrite_time_total{0};
    auto size_before = xmg.size();
    for ( auto i = 0u; i < ep.num_rewrite_times; ++i )
    {
      mockturtle::cut_rewriting_params rewrite_ps;
      rewrite_ps.cut_enumeration_ps.cut_size = 4;
      rewrite_ps.progress = true;

      mockturtle::cut_rewriting_stats rewrite_st;
      mockturtle::cut_rewriting( xmg, resyn, rewrite_ps, &rewrite_st );
      xmg = mockturtle::cleanup_dangling( xmg );

      rewrite_time_total += rewrite_st.time_total;

      /* terminate early if size does not change */
      if ( xmg.size() == size_before )
        break;

      size_before = xmg.size();
    }

    /* profile XMG gates */
    mockturtle::xmg_cost_params xmg_st;
    num_gate_profile( xmg, xmg_st );

    /* verify results using ABC's CEC command */
    auto const cec = ( !ep.verify || benchmark == "hyp" ) ? true : experiments::abc_cec( xmg, benchmark, path_type, file_type );

    /* fill benchmark table */
    exp( benchmark,
         /* AIG: */ fmt::format( "{:7d}", aig.num_gates() ),
         /* XMG: */ fmt::format( "{:7d} = {:7d} + {:7d}", xmg.num_gates(), xmg_st.total_xor3, xmg_st.total_maj ),
         /* runtime */ mockturtle::to_seconds( noderesyn_st.time_total + rewrite_time_total ),
         /* verify: */ cec );
  }

  exp.save();
  exp.table();
}

struct experiment3_params
{
  uint32_t cut_size{5u};
  uint32_t num_rewrite_times{3u};
  bool verify = true;
};

/*! \brief Quantifies self-duality of an XMG by assessing how many 3- to 5-feasiable cuts of a node on average represent a self-dual function. */
double quantify_self_duality_using_average_over_cuts( mockturtle::xmg_network const& xmg )
{
  mockturtle::cut_enumeration_params ps;
  ps.cut_size = 5;
  ps.cut_limit = 12;
  ps.minimize_truth_table = true;

  mockturtle::cut_enumeration_stats st;
  auto const cuts = mockturtle::cut_enumeration<mockturtle::xmg_network, true>( xmg, ps, &st );

  double sum_score = 0.0;
  xmg.foreach_gate( [&]( auto const& n ) {
      auto num_self_dual_node_cuts = 0u;
      auto num_node_cuts = 0u;

      /* foreach cut */
      for ( auto const& cut : cuts.cuts( xmg.node_to_index( n ) ) )
      {
        /* skip trivial cuts */
        if ( cut->size() < 3 )
          continue;

        const auto tt = cuts.truth_table( *cut );
        if ( kitty::is_selfdual( tt ) )
        {
          ++num_self_dual_node_cuts;
        }
        ++num_node_cuts;

        sum_score += double( num_self_dual_node_cuts ) / num_node_cuts;
      }
    });

  return sum_score / xmg.num_gates();
}

/*! \brief Quantifies self-duality of an XMG by assessing if on average a 3- to 5-feasible cuts for a node exists that represents a self-dual function. */
double quantify_self_duality_using_maximum_of_cuts( mockturtle::xmg_network const& xmg )
{
  mockturtle::cut_enumeration_params ps;
  ps.cut_size = 5;
  ps.cut_limit = 12;
  ps.minimize_truth_table = true;

  mockturtle::cut_enumeration_stats st;
  auto const cuts = mockturtle::cut_enumeration<mockturtle::xmg_network, true>( xmg, ps, &st );

  uint32_t num_self_dual_nodes = 0u;
  xmg.foreach_gate( [&]( auto const& n ) {
      bool has_self_dual_node_cut = false;
      for ( auto const& cut : cuts.cuts( xmg.node_to_index( n ) ) )
      {
        /* skip trivial cuts */
        if ( cut->size() < 3 )
          continue;

        const auto tt = cuts.truth_table( *cut );
        if ( kitty::is_selfdual( tt ) )
        {
          has_self_dual_node_cut = true;
        }
      }

      if ( has_self_dual_node_cut )
      {
        ++num_self_dual_nodes;
      }
    });

  return double( num_self_dual_nodes ) / xmg.num_gates();
}

void experiment3( experiment3_params const& ep, std::vector<std::string> const& benchmarks = experiments::epfl_benchmarks(), std::string const& path_type = "", std::string const& file_type = "aig" )
{
  std::cout << "===========================================================================" << std::endl;
  std::cout << "EXPERIMENT#3: node_resynthesis, rewriting, and quantify self-duality" << std::endl;
  std::cout << "===========================================================================" << std::endl;

  experiments::experiment<std::string, std::string, std::string, double, double, double, double, bool>
    exp( "node_resynthesis", "benchmark", "AIG gates [= ANDs]", "XMG gates [= XOR3s + MAJs]", "self-dual node-ratio", "self-dual avg cut-ratio", "self-dual best cut-ratio", "runtime", "equivalent" );
  for ( auto const& benchmark : benchmarks )
  {
    fmt::print( "[i] processing {}\n", benchmark );

    /* read the benchmarks */
    mockturtle::aig_network aig;
    if ( !read_benchmark( aig, benchmark, path_type, file_type ) )
      continue;

    mockturtle::xmg_network xmg;
    mockturtle::xmg3_npn_resynthesis<mockturtle::xmg_network> resyn;

    /* prepare XMG using node resynthesis */
    mockturtle::node_resynthesis_params noderesyn_ps;
    mockturtle::node_resynthesis_stats noderesyn_st;
    mockturtle::node_resynthesis( xmg, aig, resyn, noderesyn_ps, &noderesyn_st );

    mockturtle::stopwatch<>::duration rewrite_time_total{0};
    auto size_before = xmg.size();
    for ( auto i = 0u; i < ep.num_rewrite_times; ++i )
    {
      mockturtle::cut_rewriting_params rewrite_ps;
      rewrite_ps.cut_enumeration_ps.cut_size = 4;
      rewrite_ps.progress = true;

      mockturtle::cut_rewriting_stats rewrite_st;
      mockturtle::cut_rewriting( xmg, resyn, rewrite_ps, &rewrite_st );
      xmg = mockturtle::cleanup_dangling( xmg );

      rewrite_time_total += rewrite_st.time_total;

      /* terminate early if size does not change */
      if ( xmg.size() == size_before )
        break;

      size_before = xmg.size();
    }

    /* profile XMG gates */
    mockturtle::xmg_cost_params xmg_st;
    num_gate_profile( xmg, xmg_st );

    auto const score1 = quantify_self_duality_using_average_over_cuts( xmg );
    auto const score2 = quantify_self_duality_using_maximum_of_cuts( xmg );

    /* verify results using ABC's CEC command */
    auto const cec = ( !ep.verify || benchmark == "hyp" ) ? true : experiments::abc_cec( xmg, benchmark, path_type, file_type );

    /* fill benchmark table */
    exp( benchmark,
         /* AIG: */ fmt::format( "{:7d}", aig.num_gates() ),
         /* XMG: */ fmt::format( "{:7d} = {:7d} + {:7d}", xmg.num_gates(), xmg_st.total_xor3, xmg_st.total_maj ),
         /* self-duality ratio: */ double( xmg_st.actual_xor3 + xmg_st.actual_maj ) / double( xmg.num_gates() ),
         /* self-duality score #1: */ score1,
         /* self-duality score #2: */ score2,
         /* runtime */ mockturtle::to_seconds( noderesyn_st.time_total + rewrite_time_total ),
         /* verify: */ cec );
  }

  exp.save();
  exp.table();
}

int main()
{
  /* NOTE that we disable equivalence checking for cryptographic benchmarks because it is typically too time consuming */

#if 0
  /* experiment #1: node resynthesis of benchmarks into X3MGs */
  {
    experiment1( experiment1_params{true} );
    experiment1( experiment1_params{false}, experiments::crypto_benchmarks(), "_crypto", "v" );
  }

  /* experiment #2: node resynthesis and 1x nad 3x rewriting of benchmarks into X3MGs using NPN4-DB generated with exact synthesis */
  {
    experiment2( experiment2_params{1u, true} );
    experiment2( experiment2_params{1u, false}, experiments::crypto_benchmarks(), "_crypto", "v" );
    experiment2( experiment2_params{2u, true} );
    experiment2( experiment2_params{2u, false}, experiments::crypto_benchmarks(), "_crypto", "v" );
  }
#endif

  /* experiment #3: node resynthesis, rewriting, and quantify self-duality */
  {
    experiment3( experiment3_params{5u, true} );
  }

  return 0;
}

/***
 * TODOs:
 * - Quantify the self-duality of a benchmark circuit
 */
