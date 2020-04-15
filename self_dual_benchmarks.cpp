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

#include "experiments.hpp"

#include <mockturtle/algorithms/reconv_cut.hpp>
#include <mockturtle/io/aiger_reader.hpp>
#include <mockturtle/io/write_verilog.hpp>
#include <mockturtle/networks/aig.hpp>
#include <mockturtle/views/cut_view.hpp>
#include <mockturtle/views/topo_view.hpp>

#include <lorina/aiger.hpp>

#include <fmt/format.h>

#include <string>
#include <vector>

namespace mockturtle
{

aig_network self_dualize_aig( aig_network const& src_aig )
{
  using node = node<aig_network>;
  using signal = signal<aig_network>;
    
  aig_network dest_aig;
  std::unordered_map<node, signal> node_to_signal_one;
  std::unordered_map<node, signal> node_to_signal_two;
  
  /* copy inputs */
  node_to_signal_one[0] = dest_aig.get_constant( false );
  node_to_signal_two[0] = dest_aig.get_constant( false );
  src_aig.foreach_pi( [&]( const auto& n ){
      auto const pi = dest_aig.create_pi();
      node_to_signal_one[n] = pi;
      node_to_signal_two[n] = !pi;
    });

  src_aig.foreach_po( [&]( const auto& f ){
      reconv_cut cutgen( {.cut_size = 99999999u} );

      auto leaves = cutgen( src_aig, src_aig.get_node( f ) );
      std::sort( std::begin( leaves ), std::end( leaves ) );

      /* check if all leaves are pis */
      for ( const auto& l : leaves )
      {
        assert( src_aig.is_pi( l ) );
      }
      
      cut_view<aig_network> view( src_aig, leaves, f );
      topo_view topo_view( view );

      /* create cone once */
      topo_view.foreach_gate( [&]( const auto& g ){
          std::vector<signal> new_fanins;
          topo_view.foreach_fanin( g, [&]( const auto& fi ){
              auto const n = topo_view.get_node( fi );
              new_fanins.emplace_back( topo_view.is_complemented( fi ) ? !node_to_signal_one[n] : node_to_signal_one[n] );
            });

          assert( new_fanins.size() == 2u );
          node_to_signal_one[g] = dest_aig.create_and( new_fanins[0u], new_fanins[1u] );
        });

      /* create cone once */
      topo_view.foreach_gate( [&]( const auto& g ){
          std::vector<signal> new_fanins;
          topo_view.foreach_fanin( g, [&]( const auto& fi ){
              auto const n = topo_view.get_node( fi );
              new_fanins.emplace_back( topo_view.is_complemented( fi ) ? !node_to_signal_two[n] : node_to_signal_two[n] );
            });

          assert( new_fanins.size() == 2u );
          node_to_signal_two[g] = dest_aig.create_and( new_fanins[0u], new_fanins[1u] );
        });

      auto const output_signal_one = topo_view.is_complemented( f ) ? !node_to_signal_one[topo_view.get_node( f )] : node_to_signal_one[topo_view.get_node( f )];
      auto const output_signal_two = topo_view.is_complemented( f ) ? !node_to_signal_two[topo_view.get_node( f )] : node_to_signal_two[topo_view.get_node( f )];

      auto const new_pi = dest_aig.create_pi();
      auto const output = dest_aig.create_or( dest_aig.create_and( new_pi, output_signal_one ), dest_aig.create_and( !new_pi, !output_signal_two ) );
      dest_aig.create_po( output );

      std::cout << ".";
      std::cout.flush();
    });

  std::cout << std::endl;
  return dest_aig;
}

} /* mockturtle */

int main()
{
  using namespace experiments;
  using namespace mockturtle;

  experiment<std::string, uint32_t, uint32_t> exp( "aig_resubstitution", "benchmark", "size_before", "size_after" );

  for ( auto const& benchmark : epfl_benchmarks( ~experiments::hyp ) )
  {
    fmt::print( "[i] processing {}\n", benchmark );
    
    aig_network aig;
    auto const result = lorina::read_aiger( benchmark_path( benchmark ), aiger_reader( aig ) );
    if ( result != lorina::return_code::success )
    {
      return -1;
    }

    std::cout << "[i] #pis = " << aig.num_pis() << ' ' << "#pos = " << aig.num_pos() << std::endl;

    auto const size_before = aig.num_gates();
    auto const new_aig = self_dualize_aig( aig );
    auto const size_after = new_aig.num_gates();

    write_verilog( new_aig, fmt::format( "{}_sd.v", benchmark ) );
    
    exp( benchmark, size_before, size_after );
  }

  exp.save();
  exp.compare();

  return 0;
}
