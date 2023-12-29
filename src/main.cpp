#include "huffman_encoder.h"
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>

auto read(const char * file) -> std::pair<std::unique_ptr<char[]>, size_t>{

    std::ifstream inp_file(file);

    inp_file.seekg(0u, inp_file.end);
    auto sz     = inp_file.tellg();
    auto buf    = std::unique_ptr<char[]>(new char[sz]);
    inp_file.seekg(0u, inp_file.beg);
    inp_file.read(buf.get(), sz);

    return {std::move(buf), sz}; 
}

template <class Executable>
auto timeit(Executable exe) -> size_t{

    using namespace std::chrono;
    auto s = high_resolution_clock::now();
    exe();
    auto l = duration_cast<milliseconds>(high_resolution_clock::now() - s).count();

    return l;    
}

int main(){

    using namespace dg::huffman_encoder::user_interface;

    const char * buf = "tommy2tonez";
    char c[1000];
    char c2[1000];
    
    auto d  = build(count(buf, 11));
    auto e  = spawn_fast_engine(d.get());

    auto rd = dg::huffman_encoder::types::bit_array_type{};
    auto l  = e.encode_into(buf, 11, c, rd);
    auto [b, c2l] = e.fast_decode_into(c, 0u, std::distance(c, l) * CHAR_BIT, c2); 

    for (size_t i = 0; i < 11; ++i){
        std::cout << c2[i];
    }
}