/*
Copyright(c) 2018 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io
*/

#include <iostream>
#include <sstream>
#include <chrono>
#include <regex>
#include <time.h>
#include <stdio.h>

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4996)
#endif

#include <vector>
#include <chrono>
#include <map>

#include "bm.h"
#include "bmalgo.h"
#include "bmserial.h"
#include "bmrandom.h"
#include "bmsparsevec.h"
#include "bmsparsevec_compr.h"
#include "bmsparsevec_algo.h"
#include "bmsparsevec_serial.h"
#include "bmalgo_similarity.h"
#include "bmsparsevec_util.h"


#include "bmdbg.h"
#include "bmtimer.h"

static
void show_help()
{
    std::cerr
        << "BitMagic SNP Search Sample Utility (c) 2018" << std::endl
        << "-isnp   file-name          -- input set file (SNP FASTA) to parse" << std::endl
        << "-svout  spase vector output  -- sparse vector name to save" << std::endl
        << "-svin   spase vector input   -- sparse vector file name to load " << std::endl
        << "-diag                      -- run diagnostics"                       << std::endl
        << "-timing                    -- collect timings"                       << std::endl
      ;
}




// Arguments
//
std::string  sv_out_name;
std::string  sv_in_name;
std::string  isnp_name;
bool         is_diag = false;
bool         is_timing = false;
bool         is_bench = false;

static
int parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "-h") || (arg == "--help"))
        {
            show_help();
            return 0;
        }
        
        if (arg == "-svout" || arg == "--svout")
        {
            if (i + 1 < argc)
            {
                sv_out_name = argv[++i];
            }
            else
            {
                std::cerr << "Error: -svout requires file name" << std::endl;
                return 1;
            }
            continue;
        }
        if (arg == "-svin" || arg == "--svin")
        {
            if (i + 1 < argc)
            {
                sv_in_name = argv[++i];
            }
            else
            {
                std::cerr << "Error: -svin requires file name" << std::endl;
                return 1;
            }
            continue;
        }

        if (arg == "-isnp" || arg == "--isnp")
        {
            if (i + 1 < argc)
            {
                isnp_name = argv[++i];
            }
            else
            {
                std::cerr << "Error: -isnp requires file name" << std::endl;
                return 1;
            }
            continue;
        }

        if (arg == "-diag" || arg == "--diag" || arg == "-d" || arg == "--d")
            is_diag = true;
        if (arg == "-timing" || arg == "--timing" || arg == "-t" || arg == "--t")
            is_timing = true;
        if (arg == "-bench" || arg == "--bench" || arg == "-b" || arg == "--b")
            is_bench = true;

    } // for i
    return 0;
}


// Globals
//
typedef bm::sparse_vector<unsigned, bm::bvector<> > sparse_vector_u32;
typedef bm::compressed_sparse_vector<unsigned, sparse_vector_u32 > compressed_sparse_vector_u32;

// ----------------------------------------------------------------------------

bm::chrono_taker::duration_map_type  timing_map;


static
int load_snp_report(const std::string& fname, sparse_vector_u32& sv)
{
    bm::chrono_taker tt1("1. parse input SNP chr report", 1, &timing_map);

    std::ifstream fin(fname.c_str(), std::ios::in);
    if (!fin.good())
        return -1;

    unsigned rs_id, rs_pos;
    size_t idx;

    std::string line;
    std::string delim = " \t";

    std::regex reg("\\s+");
    std::sregex_token_iterator it_end;

    bm::bvector<> bv_rs; 
    bv_rs.init();

    unsigned rs_cnt = 0;
    for (unsigned i = 0; std::getline(fin, line); ++i)
    {
        if (line.empty() ||
            !isdigit(line.front())
            )
            continue;

        // regex based tokenizer
        std::sregex_token_iterator it(line.begin(), line.end(), reg, -1);
        std::vector<std::string> line_vec(it, it_end);
        if (line_vec.empty())
            continue; 
        
        // parse columns of interest
        try
        {
            rs_id = unsigned(std::stoul(line_vec.at(0), &idx));
            
            if (bv_rs.test(rs_id))
            {
                continue;
            }
            rs_pos = unsigned(std::stoul(line_vec.at(11), &idx));

            bv_rs.set_bit_no_check(rs_id);
            sv.set(rs_pos, rs_id);

            ++rs_cnt;
        }
        catch (std::exception& /*ex*/)
        {
            continue; // detailed disgnostics commented out
            // error detected, becuase some columns are sometimes missing 
            // just ignore it
            //
            /*
            std::cerr << ex.what() << "; ";
            std::cerr << "rs=" << line_vec.at(0) << " pos=" << line_vec.at(11) << std::endl;
            continue;
            */
        }
        if (rs_cnt % (4 * 1024) == 0)
            std::cout << "\r" << rs_cnt << " / " << i; // PROGRESS report
    } // for i

    std::cout << std::endl;
    std::cout << "SNP count=" << rs_cnt << std::endl;
    return 0;
}

// Generate random subset of random values from a sparse vector
//
static
void generate_random_subset(const sparse_vector_u32&  sv, std::vector<unsigned>& vect, unsigned count)
{
    const sparse_vector_u32::bvector_type* bv_null = sv.get_null_bvector();

    bm::random_subset<bm::bvector<> > rand_sampler;
    bm::bvector<> bv_sample;
    rand_sampler.sample(bv_sample, *bv_null, count);

    bm::bvector<>::enumerator en = bv_sample.first();
    for (; en.valid(); ++en)
    {
        unsigned idx = *en;
        unsigned v = sv[idx];
        vect.push_back(v);
    }
}

static
void run_benchmark(const sparse_vector_u32& sv, const compressed_sparse_vector_u32& csv)
{
    const unsigned rs_sample_count = 500;

    std::vector<unsigned> rs_vect;
    generate_random_subset(sv, rs_vect, rs_sample_count);
    if (rs_vect.empty())
    {
        std::cerr << "Benchmark subset empty!" << std::endl;
        return;
    }
    {
        bm::chrono_taker tt1("5. rs search", unsigned(rs_vect.size()), &timing_map);

        bm::bvector<> bv_found;  // search results vector
        bm::sparse_vector_scanner<sparse_vector_u32> scanner; // scanner class

        for (unsigned i = 0; i < rs_vect.size(); ++i)
        {
            unsigned rs_id = rs_vect[i];
            unsigned rs_pos;
            bool found = scanner.find_eq(sv, rs_id, rs_pos);
            //bool found = bv_found.find(rs_pos);

            if (found)
            {
                //std::cout << "rs_id = " << rs_id << " pos=" << rs_pos << std::endl;
            }
            else
            {
                std::cout << "rs_id = " << rs_id << " not found!" << std::endl;
            }
        } // for
    }


    {
        bm::chrono_taker tt1("6. rs search (csv)", unsigned(rs_vect.size()), &timing_map);

        bm::bvector<> bv_found;  // search results vector
        bm::sparse_vector_scanner<compressed_sparse_vector_u32> scanner; // scanner class

        for (unsigned i = 0; i < rs_vect.size(); ++i)
        {
            unsigned rs_id = rs_vect[i];
            unsigned rs_pos;
            bool found = scanner.find_eq(csv, rs_id, rs_pos);

            if (found)
            {
                //std::cout << "rs_id = " << rs_id << " pos=" << rs_pos << std::endl;
            }
            else
            {
                std::cout << "rs_id = " << rs_id << " not found!" << std::endl;
            }
        } // for
    }

}


int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        show_help();
        return 1;
    }

    sparse_vector_u32  sv(bm::use_null);
    compressed_sparse_vector_u32 csv;

    try
    {
        auto ret = parse_args(argc, argv);
        if (ret != 0)
            return ret;

        
        if (!isnp_name.empty())
        {
            auto res = load_snp_report(isnp_name, sv);
            if (res != 0)
            {
                return res;
            }
        }
        if (!sv_in_name.empty())
        {
            bm::chrono_taker tt1("2. Load sparse vector", 1, &timing_map);
            file_load_svector(sv, sv_in_name);
        }


        if (!sv_in_name.empty())
        {
            sparse_vector_u32  sv2(bm::use_null);
            {
                bm::chrono_taker tt1("3. compress sparse vector", 1, &timing_map);
                csv.load_from(sv);
            }
            {
                bm::chrono_taker tt1("4. de-compress sparse vector", 1, &timing_map);
                csv.load_to(sv2);
            }
            if (!sv.equal(sv2))
            {
                std::cerr << "Error with compressed vector!" << std::endl;
                return 1;
            }
            //file_save_svector(csv.get_sv(), sv_in_name + ".zsv");
        }

        if (!sv_out_name.empty())
        {
            bm::chrono_taker tt1("3. Save sparse vector", 1, &timing_map);
            sv.optimize();
            file_save_svector(sv, sv_out_name);
        }
	
        if (is_diag && !sv.empty())
        {
            bm::print_svector_stat(sv, false);
        }

        if (is_bench)
        {
            run_benchmark(sv, csv);
        }


        if (is_timing)  // print all collected timings
        {
            std::cout << std::endl << "Performance:" << std::endl;
            bm::chrono_taker::print_duration_map(timing_map, bm::chrono_taker::ct_ops_per_sec);
        }
    }
    catch (std::exception& ex)
    {
        std::cerr << "Error:" << ex.what() << std::endl;
        return 1;
    }

    return 0;
}


#ifdef _MSC_VER
#pragma warning( pop )
#endif



