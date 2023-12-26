#include "huffman_encoder.h"
#include <string>
#include <iostream>

int main(){

    using namespace dg::huffman_encoder::user_interface;

    auto test       = std::string{"tommy2tonez"}; 
    auto d          = build(count(test.data(), test.size()));
    auto e          = spawn_delim_engine(d.get());
    auto [buf,sz]   = e.encode(test.data(), test.size());
    auto [bbuf,ssz] = e.decode(buf.get());

    for (size_t i = 0; i < ssz; ++i){
        std::cout << bbuf[i];
    }
}