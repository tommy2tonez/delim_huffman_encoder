
#ifndef __DG_HUFFMAN_ENCODER__
#define __DG_HUFFMAN_ENCODER__

#include <memory>
#include <vector>
#include <algorithm>
#include <utility>
#include <numeric>
#include <iterator>
#include <cstring>
#include <iostream>
#include "serialization.h"
#include <array>
#include "assert.h"

namespace dg::huffman_encoder::constants{

    static inline constexpr size_t ALPHABET_SIZE    = 2;
    static inline constexpr size_t DICT_SIZE        = size_t{1} << (ALPHABET_SIZE * CHAR_BIT);
    static inline constexpr bool L                  = false;
    static inline constexpr bool R                  = true;
}

namespace dg::huffman_encoder::types{

    using word_type     = std::array<char, constants::ALPHABET_SIZE>;
    using num_rep_type  = std::conditional_t<constants::ALPHABET_SIZE == 1u, 
                                             uint8_t,
                                             std::conditional_t<constants::ALPHABET_SIZE == 2u, 
                                                                uint16_t,
                                                                void>>;
} 

namespace dg::huffman_encoder::model{

    using namespace huffman_encoder::types;

    struct Node{
        std::unique_ptr<Node> l;
        std::unique_ptr<Node> r;
        word_type c;

        template <class Reflector>
        void dg_reflect(const Reflector& reflector) const{
            reflector(l, r, c);
        }

        template <class Reflector>
        void dg_reflect(const Reflector& reflector){
            reflector(l, r, c);
        }
    };

    struct DelimNode{
        std::unique_ptr<DelimNode> l;
        std::unique_ptr<DelimNode> r;
        word_type c;
        uint8_t delim_stat;
    };
}

namespace dg::huffman_encoder::byte_array{

    constexpr auto slot(size_t idx) -> size_t{
        
        return idx / CHAR_BIT;
    }

    constexpr auto offs(size_t idx) -> size_t{

        return idx % CHAR_BIT;
    }

    constexpr auto byte_size(size_t bit_sz) -> size_t{

        return (bit_sz == 0u) ? 0u : slot(bit_sz - 1) + 1;
    }

    constexpr auto true_toggle(size_t offs) -> char{

        return char{1} << offs;
    } 

    constexpr auto false_toggle(size_t offs) -> char{

        return char{-1} ^ true_toggle(offs); 
    }

    constexpr auto read(const char * op, size_t idx) -> bool{

        return (op[slot(idx)] & true_toggle(offs(idx))) != 0;
    }

    constexpr auto read_byte(const char * op, size_t idx) -> char{
        
        auto rs         = char{};
        auto idx_seq    = std::make_index_sequence<CHAR_BIT>{};

        [&]<size_t ...IDX>(const std::index_sequence<IDX...>){
            ([&]{
                (void) IDX;
                rs <<= 1;
                rs |= read(op, idx + (CHAR_BIT - IDX - 1));    
            }(), ...);
        }(idx_seq);

        return rs;
    } 

    constexpr void bit_dump(char * op, size_t idx, char data){

        for (size_t i = 0; i < CHAR_BIT; ++i){
            if (read(&data, i)){
                op[slot(idx)] |= true_toggle(offs(idx));
            } else{
                op[slot(idx)] &= false_toggle(offs(idx));
            }
            idx += 1;
        }
    }

    void bit_dump(char * op, size_t idx, const std::vector<bool>& data) noexcept{

        for (size_t i = 0; i < data.size(); ++i){
            if (data[i]){
                op[slot(idx)] |= true_toggle(offs(idx));
            } else{
                op[slot(idx)] &= false_toggle(offs(idx));
            }
            idx += 1;
        }
    }

    void memcpy(char * dst, size_t dst_offs, const char * src, size_t sz) noexcept{

        std::memcpy(&dst[dst_offs], src, sz);
    } 

    static constexpr auto bit_dump_lambda   = []<class ...Args>(Args&& ...args) noexcept{bit_dump(std::forward<Args>(args)...);};
    static constexpr auto byte_dump_lambda  = []<class ...Args>(Args&& ...args) noexcept{memcpy(std::forward<Args>(args)...);};
}

namespace dg::huffman_encoder::make{

    using namespace huffman_encoder::types;
    
    struct CounterNode{
        std::unique_ptr<CounterNode> l;
        std::unique_ptr<CounterNode> r;
        size_t count;
        word_type c;
    };

    auto count(char * buf, size_t sz) -> std::vector<size_t>{

        auto counter    = std::vector<size_t>(constants::DICT_SIZE);
        auto cycles     = sz / constants::ALPHABET_SIZE;
        auto ibuf       = buf;
        std::fill(counter.begin(), counter.end(), size_t{0u}); 

        for (size_t i = 0; i < cycles; ++i){
            auto num_rep = num_rep_type{};
            dg::compact_serializer::core::deserialize(ibuf, num_rep);
            counter[num_rep] += 1;
            ibuf += constants::ALPHABET_SIZE;
        } 

        return counter;
    }

    auto defaultize_noncounted(std::vector<size_t> count){

        auto transformer    = [](size_t count){return std::min(size_t{1}, count);};
        std::transform(count.begin(), count.end(), count.begin(), transformer);

        return count;
    }

    auto build(std::vector<size_t> counter) -> std::unique_ptr<CounterNode>{

        if (counter.size() != constants::DICT_SIZE){
            std::abort();
        } 

        auto cmp        = [](const auto& lhs, const auto& rhs){return lhs->count > rhs->count;};
        auto heap       = std::vector<std::unique_ptr<CounterNode>>{};

        for (size_t i = 0; i < constants::DICT_SIZE; ++i){
            auto num_rep    = static_cast<num_rep_type>(i);
            auto word       = word_type{};
            dg::compact_serializer::core::serialize(num_rep, word.data());
            auto cnode      = std::make_unique<CounterNode>(CounterNode{nullptr, nullptr, counter[i], word});
            heap.push_back(std::move(cnode));
        }

        std::make_heap(heap.begin(), heap.end(), cmp);

        while (heap.size() != 1){
            std::pop_heap(heap.begin(), heap.end(), cmp);
            std::pop_heap(heap.begin(), std::prev(heap.end()), cmp);
            auto first  = std::move(heap.back()); 
            heap.pop_back();
            auto second = std::move(heap.back());
            heap.pop_back();
            auto count  = first->count + second->count; 
            auto cnode  = std::make_unique<CounterNode>(CounterNode{std::move(first), std::move(second), count, {}});
            heap.push_back(std::move(cnode));
            std::push_heap(heap.begin(), heap.end(), cmp);
        }  

        return std::move(heap.back());
    }

    auto to_model(CounterNode * root) -> std::unique_ptr<model::Node>{

        if (!root){
            return {};
        }

        auto rs     = std::make_unique<model::Node>();
        rs->c       = root->c;
        rs->l       = to_model(root->l.get());
        rs->r       = to_model(root->r.get());

        return rs;
    } 
    
    auto to_delim_model(model::Node * root) -> std::unique_ptr<model::DelimNode>{

        if (!root){
            return {};
        }

        auto rs         = std::make_unique<model::DelimNode>();
        rs->c           = root->c;
        rs->l           = to_delim_model(root->l.get());
        rs->r           = to_delim_model(root->r.get());
        rs->delim_stat  = {};

        return rs;
    }

    void dictionarize(model::DelimNode * root, std::vector<std::vector<bool>>& op, std::vector<bool>& trace){

        bool is_leaf = !bool{root->r} && !bool{root->l};

        if (is_leaf){
            if (!root->delim_stat){
                auto num_rep    = num_rep_type{};
                dg::compact_serializer::core::deserialize(root->c.data(), num_rep);
                op[num_rep]     = trace;
            }
        } else{
            trace.push_back(constants::L);
            dictionarize(root->l.get(), op, trace);
            trace.push_back(constants::R);
            dictionarize(root->r.get(), op, trace);
        }

        trace.pop_back();
    }

    auto dictionarize(model::DelimNode * root) -> std::vector<std::vector<bool>>{

        auto rs     = std::vector<std::vector<bool>>(constants::DICT_SIZE);
        auto trace  = std::vector<bool>();
        dictionarize(root, rs, trace);

        return rs;
    }

    auto find_min_path_to_leaf(model::DelimNode * root, size_t depth = 0) -> std::pair<model::DelimNode *, size_t>{
        
        bool is_leaf    = !bool{root->r} && !bool{root->r};

        if (is_leaf){
            return {root, depth};
        }

        auto [l_leaf, l_depth]  = find_min_path_to_leaf(root->l.get(), depth + 1);
        auto [r_leaf, r_depth]  = find_min_path_to_leaf(root->r.get(), depth + 1);

        if (l_depth < r_depth){
            return {l_leaf, l_depth};
        }

        return {r_leaf, r_depth};
    }

    auto to_delim_tree(model::Node * huffman_tree) -> std::unique_ptr<model::DelimNode>{

        auto delim_model    = to_delim_model(huffman_tree);

        for (size_t i = 0; i < constants::ALPHABET_SIZE; ++i){
            auto [leaf, v]      = find_min_path_to_leaf(delim_model.get());
            leaf->l             = std::make_unique<model::DelimNode>(model::DelimNode{{}, {}, leaf->c, leaf->delim_stat});
            leaf->r             = std::make_unique<model::DelimNode>(model::DelimNode{{}, {}, {}, static_cast<uint8_t>(i + 1)}); 
        }

        return delim_model;
    } 

    void find_delim(model::DelimNode * root, std::vector<std::vector<bool>>& rs, std::vector<bool>& trace){

        bool is_leaf    = !bool{root->l} && !bool{root->r};

        if (is_leaf){
            if (root->delim_stat){
                rs[root->delim_stat - 1]   = trace; 
            }
        } else{
            trace.push_back(constants::L);
            find_delim(root->l.get(), rs, trace);
            trace.push_back(constants::R);
            find_delim(root->r.get(), rs, trace);
        }

        trace.pop_back();
    }

    void find_delim(model::DelimNode * root, std::vector<std::vector<bool>>& rs){

        auto trace  = std::vector<bool>{};
        find_delim(root, rs, trace);
    }
}

namespace dg::huffman_encoder::core{
    
    using namespace types;

    class DelimEngine{

        private:

            std::vector<std::vector<bool>> encoding_dict;
            std::vector<std::vector<bool>> delim;
            std::unique_ptr<model::DelimNode> decoding_dict;  

        public:

            DelimEngine(std::vector<std::vector<bool>> encoding_dict, 
                        std::vector<std::vector<bool>> delim,
                        std::unique_ptr<model::DelimNode> decoding_dict): encoding_dict(std::move(encoding_dict)),
                                                                          delim(std::move(delim)),
                                                                          decoding_dict(std::move(decoding_dict)){} 

            template <class BitDumpDevice = decltype(byte_array::bit_dump_lambda)>
            auto encode_into(const char * inp_buf, size_t inp_sz, char * op_buf, size_t op_bit_offs, const BitDumpDevice& bit_dumper = byte_array::bit_dump_lambda) const noexcept -> size_t{
                
                auto cycles = inp_sz / constants::ALPHABET_SIZE;
                auto rem    = inp_sz - (cycles * constants::ALPHABET_SIZE);
                auto ibuf   = inp_buf;

                for (size_t i = 0; i < cycles; ++i){
                    auto num_rep        = num_rep_type{};
                    ibuf                = dg::compact_serializer::core::deserialize(ibuf, num_rep);
                    const auto& bvec    = encoding_dict[num_rep];
                    static_assert(noexcept(bit_dumper(op_buf, op_bit_offs, bvec)));
                    bit_dumper(op_buf, op_bit_offs, bvec); 
                    op_bit_offs        += bvec.size();
                }

                static_assert(noexcept(bit_dumper(op_buf, op_bit_offs, this->delim[rem])));
                bit_dumper(op_buf, op_bit_offs, this->delim[rem]);
                op_bit_offs += this->delim[rem].size();

                for (size_t i = 0; i < rem; ++i){
                    bit_dumper(op_buf, op_bit_offs, ibuf[i]);
                    op_bit_offs += CHAR_BIT;
                }

                return op_bit_offs;
            }

            template <class ByteDumpDevice = decltype(byte_array::byte_dump_lambda)>
            auto decode_into(const char * inp_buf, size_t inp_bit_offs, char * op_buf, size_t op_offs, const ByteDumpDevice& byte_dumper = byte_array::byte_dump_lambda) const noexcept -> std::pair<size_t, size_t>{
                 
                model::DelimNode * cur  = this->decoding_dict.get(); 

                while (true){

                    bool r    = byte_array::read(inp_buf, inp_bit_offs++);

                    if (r == constants::L){
                        cur = cur->l.get();
                    } else{
                        cur = cur->r.get();
                    }

                    bool is_leaf    = !bool{cur->r} && !bool{cur->l};

                    if (is_leaf){
                        if (cur->delim_stat){
                            auto trailing_sz    = cur->delim_stat - 1;
                            for (size_t i = 0; i < trailing_sz; ++i){
                                char b  = byte_array::read_byte(inp_buf, inp_bit_offs);
                                static_assert(noexcept(byte_dumper(op_buf, op_offs++, &b, sizeof(char))));
                                byte_dumper(op_buf, op_offs++, &b, sizeof(char));
                                inp_bit_offs += CHAR_BIT;
                            }
                            return {inp_bit_offs, op_offs};
                        }

                        static_assert(noexcept(byte_dumper(op_buf, op_offs, cur->c.data(), constants::ALPHABET_SIZE)));
                        byte_dumper(op_buf, op_offs, cur->c.data(), constants::ALPHABET_SIZE);
                        op_offs += constants::ALPHABET_SIZE; 
                        cur     = this->decoding_dict.get();
                    }
                }
            }
    };
    
    //API - should consider array approach if compile-time deterministic - 
    class RowEncodingEngine{

        private:

            std::vector<std::unique_ptr<DelimEngine>> encoders;
        
        public:

            RowEncodingEngine(std::vector<std::unique_ptr<DelimEngine>> encoders): encoders(std::move(encoders)){}

            auto encode_into(const std::vector<std::pair<const char *, size_t>>& data, char * buf) const -> char *{

                assert(data.size() == this->encoders.size()); 
                size_t buf_bit_offs = 0u;

                for (size_t i = 0; i < data.size(); ++i){
                    buf_bit_offs = this->encoders[i]->encode_into(data[i].first, data[i].second, buf, buf_bit_offs);
                }

                return buf + byte_array::byte_size(buf_bit_offs);
            }

            auto decode_into(const char * buf, std::vector<std::pair<char *, size_t>>& data) const -> const char *{

                assert(data.size() == this->encoders.size());
                size_t buf_bit_offs = 0u;

                for (size_t i = 0; i < this->encoders.size(); ++i){
                    data[i].second = 0u;
                    std::tie(buf_bit_offs, data[i].second) = this->encoders[i]->decode_into(buf, buf_bit_offs, data[i].first, data[i].second); 
                }

                return buf + byte_array::byte_size(buf_bit_offs);
            }

            auto count_encode(const std::vector<std::pair<const char *, size_t>>& data) const -> size_t{

                assert(data.size() == this->encoders.size());
                size_t buf_bit_offs = 0u;
                auto empty_lambda   = [](...) noexcept{};

                for (size_t i = 0; i < data.size(); ++i){
                    buf_bit_offs = this->encoders[i]->encode_into(data[i].first, data[i].second, {}, buf_bit_offs, empty_lambda);
                } 

                return byte_array::byte_size(buf_bit_offs);
            }

            auto count_decode(const char * buf) const -> std::vector<size_t>{

                auto rs             = std::vector<size_t>(this->encoders.size());
                size_t buf_bit_offs = 0u;
                auto empty_lambda   = [](...) noexcept{};

                for (size_t i = 0; i < this->encoders.size(); ++i){
                    std::tie(buf_bit_offs, rs[i])  = this->encoders[i]->decode_into(buf, buf_bit_offs, {}, 0u, empty_lambda);
                }

                return rs;
            }
    };

}

namespace dg::huffman_encoder::user_interface{

    auto count(char * buf, size_t sz) -> std::vector<size_t>{

        return make::count(buf, sz);
    }

    auto build(std::vector<size_t> counter) -> std::unique_ptr<model::Node>{

        auto counter_node   = make::build(make::defaultize_noncounted(std::move(counter)));
        return make::to_model(counter_node.get());
    }

    auto spawn_delim_engine(model::Node * huffman_tree) -> std::unique_ptr<core::DelimEngine>{

        auto delim          = std::vector<std::vector<bool>>(constants::ALPHABET_SIZE);
        auto decoding_dict  = make::to_delim_tree(huffman_tree);
        auto encoding_dict  = make::dictionarize(decoding_dict.get());
        make::find_delim(decoding_dict.get(), delim);
        auto rs             =  core::DelimEngine(std::move(encoding_dict), std::move(delim), std::move(decoding_dict));

        return std::make_unique<core::DelimEngine>(std::move(rs));
    }

    auto spawn_multi_fields_engine(std::vector<std::unique_ptr<core::DelimEngine>> encoders) -> std::unique_ptr<core::RowEncodingEngine>{

        return std::make_unique<core::RowEncodingEngine>(std::move(encoders));
    }
}

#endif