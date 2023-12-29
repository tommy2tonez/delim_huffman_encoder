#include "huffman_encoder.h"
#include <string>
#include <iostream>
#include <random>
#include <functional>
#include <chrono>

template <class Executable>
auto timeit(Executable exe) -> size_t{

    using namespace std::chrono;
    auto s = high_resolution_clock::now();
    exe();
    auto l = duration_cast<milliseconds>(high_resolution_clock::now() - s).count();

    return l;    
}

auto randomize_buf(const size_t N) -> std::unique_ptr<char[]>{

    static auto rand_dev    = std::bind(std::uniform_int_distribution<char>{}, std::mt19937{});
    auto buf    = std::unique_ptr<char[]>(new char[N]);
    std::generate(buf.get(), buf.get() + N, rand_dev);

    return buf;
}  

int main(){

    using namespace dg::huffman_encoder::user_interface;
    const size_t RANGE      = 30;
    auto rand_dev           = std::bind(std::uniform_int_distribution<size_t>(0u, RANGE), std::mt19937{});

    while (true){

        auto sz     = rand_dev();
        auto buf    = randomize_buf(sz);
        auto d      = build(count(buf.get(), sz));
        auto sd     = dg::compact_serializer::serialize(d);
        auto ds     = dg::compact_serializer::deserialize<decltype(d)>(sd.first.get(), sd.second);
        auto e      = spawn_fast_engine(ds.get());
        auto bbuf   = std::unique_ptr<char[]>(new char[dg::huffman_encoder::constants::MAX_ENCODING_SZ_PER_BYTE * sz]);
        auto rdbuf  = dg::huffman_encoder::types::bit_array_type{};
        auto last   = e->encode_into(buf.get(), sz, bbuf.get(), rdbuf);
        auto span   = std::distance(bbuf.get(), last);
        
        auto decoded    = std::unique_ptr<char[]>(new char[dg::huffman_encoder::constants::MAX_DECODING_SZ_PER_BYTE * span]);
        auto [_, llast] = e->fast_decode_into(bbuf.get(), 0u, span * CHAR_BIT, decoded.get());
        
        if (std::memcmp(buf.get(), decoded.get(), sz) != 0 || std::distance(decoded.get(), llast) != sz){
            std::cout << "mayday" << std::endl;
            std::abort();
        }
    }


}