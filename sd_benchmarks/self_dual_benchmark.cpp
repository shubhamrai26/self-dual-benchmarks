#include <fmt/format.h>
#include <lorina/lorina.hpp>
#include<mockturtle/networks/xmg.hpp>
#include <mockturtle/properties/xmgcost.hpp>
#include <mockturtle/algorithms/xmg_resub.hpp>
#include <mockturtle/algorithms/cleanup.hpp>
#include <mockturtle/algorithms/resubstitution.hpp>
#include <mockturtle/algorithms/node_resynthesis.hpp>
#include <mockturtle/algorithms/xmg_optimization.hpp>
#include <mockturtle/algorithms/cut_rewriting.hpp>
#include <mockturtle/algorithms/node_resynthesis/xmg3_npn.hpp>
#include <mockturtle/algorithms/xmg_algebraic_rewriting.hpp>
#include<iostream>
#include<time.h>
#include <mockturtle/views/depth_view.hpp>

#include <experiments.hpp>
   
using namespace mockturtle; 
using namespace experiments;

std::string const genlib_path = "/home/shubham/My_work/abc-vlsi-cad-flow/std_libs/date_lib_count_tt_4.genlib";


struct opt_parameters
{
    uint32_t size;
    float opt_time;
};

float area_from_abc ( const xmg_network& xmg)
{
    return abc_map( xmg , genlib_path );
}

void profile( const xmg_network& xmg)
{
    xmg_cost_params xmg_ps;
    xmg_ps.reset();
    num_gate_profile( xmg, xmg_ps );
    xmg_ps.report();
}

void create_xmg( xmg_network& xmg)  
{
    srand(time(NULL));

    using signal = mockturtle::xmg_network::signal;

    std::array<signal,1000> pis;
    std::array<signal,10000> wires;
    signal pos;

    auto iSecret = rand() % 10 + 1;

    for (int i = 0; i < 1000; i++)
    {
        pis[i] = xmg.create_pi( );
        auto i1 = rand() % 1000 + 1;
        auto i2 = rand() % 1000 + 1;
        auto i3 = rand() % 1000 + 1;
        for (int j = 0; j < 10; j++)
        { 
            wires[i]= xmg.create_maj(pis[i1], pis[(i2* i3) % 500 ], pis[( i3 * i1 ) % 300 ]);
            wires[i+j] = xmg.create_xor3( pis[( i1 + 51 ) % 771 ], pis[ ( i2 + 43 ) % 121 ], pis[ ( i3+1 ) % 153]);
        }
    }

    auto const out = xmg.create_xor3( wires[rand()%100], wires[rand()%200], wires[rand()%300]);
    xmg.create_po( out );
    std::cout << "xmg size " << xmg.num_gates() << std::endl;
    profile(xmg);
}

opt_parameters call_rs( xmg_network& xmg)
{

    // XMG resubstitution parameter set
    resubstitution_params resub_ps;
    resubstitution_stats resub_st;
    resub_ps.max_pis = 8u;
    //resub_ps.progress = true;
    resub_ps.max_inserts = 1u;  
    resub_ps.use_dont_cares = true; 
    resub_ps.window_size = 12u;  

    xmg_resubstitution( xmg, resub_ps, &resub_st);
    xmg = cleanup_dangling( xmg ); 
    
    opt_parameters oparam;
    oparam.size = xmg.num_gates( );
    oparam.opt_time = to_seconds( resub_st.time_total );

    return oparam;
}

opt_parameters call_rw( xmg_network& xmg)
{
    // XMG rewriting parameter set
    cut_rewriting_params cr_ps;
    cut_rewriting_stats cr_st;
    cr_ps.cut_enumeration_ps.cut_size = 4;
    //cr_ps.progress = true;
    
    mockturtle::xmg3_npn_resynthesis<xmg_network> resyn;
    cut_rewriting( xmg, resyn, cr_ps, &cr_st );
    xmg = cleanup_dangling( xmg );
    
    opt_parameters oparam;
    oparam.size = xmg.num_gates( );
    oparam.opt_time = to_seconds( cr_st.time_total );

    return oparam;
}

int main()
{
    xmg_network xmg;
    create_xmg( xmg );
    xmg = cleanup_dangling( xmg );
    profile(xmg);
    
    auto const init_area = area_from_abc( xmg );

    uint32_t num_iters;
    uint32_t size_per_iteration;
    float total_opt_time = 0; 
    float total_imp = 0;

    // Call optimizations until convergence
    do 
    {
        num_iters++;

        auto rw_params = call_rw( xmg );
        auto rs_params = call_rs( xmg );

        total_opt_time = total_opt_time + rs_params.opt_time + rw_params.opt_time;
        size_per_iteration = rs_params.size; 

        if (size_per_iteration == 0u)
            total_imp = 0;
        else
        {
            int diff = size_per_iteration - xmg.num_gates();
            total_imp = 100 * (double(std::abs(diff))/size_per_iteration);
        }

    } while ( total_imp > 0.5 );

    profile(xmg);
    float area_after = area_from_abc( xmg );

    std::cout << "init area " << init_area << " after area " << area_after << std::endl;
    return 0;
}
