
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
#include <deque>

namespace dg::huffman_encoder::constants{

    static inline constexpr size_t ALPHABET_SIZE        = 2;
    static inline constexpr size_t ALPHABET_BIT_SIZE    = ALPHABET_SIZE * CHAR_BIT;
    static inline constexpr size_t DICT_SIZE            = size_t{1} << ALPHABET_BIT_SIZE;
    static inline constexpr bool L                      = false;
    static inline constexpr bool R                      = true;
}

namespace dg::huffman_encoder::types{
    
    using bit_container_type    = uint64_t;
    using bit_array_type        = std::pair<bit_container_type, size_t>;
    using word_type             = std::array<char, constants::ALPHABET_SIZE>;
    using num_rep_type          = std::conditional_t<constants::ALPHABET_SIZE == 1u, 
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

namespace dg::huffman_encoder::utility{

    template <class T, class TransformLambda>
    auto vector_transform(const std::vector<T>& lhs, const TransformLambda& transform_lambda) -> std::vector<decltype(transform_lambda(std::declval<T>()))>{

        auto rs = std::vector<decltype(transform_lambda(std::declval<T>()))>();
        std::transform(lhs.begin(), lhs.end(), std::back_inserter(rs), transform_lambda);

        return rs;
    } 

    template <class T,  std::enable_if_t<std::is_fundamental_v<T>, bool> = true>
    auto to_bit_deque(T val) -> std::deque<bool>{

        auto rs         = std::deque<bool>();
        auto idx_seq    = std::make_index_sequence<sizeof(T) * CHAR_BIT>{};

        [&]<size_t ...IDX>(const std::index_sequence<IDX...>&){
            (
                [&]{
                    (void) IDX;
                    rs.push_back(static_cast<bool>(val & 1));
                    val >>= 1;
                }(), ...
            );
        }(idx_seq);

        return rs;
    } 

}

namespace dg::huffman_encoder::byte_array{

    using namespace huffman_encoder::types;

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

    template <size_t SZ>
    constexpr auto read_bit(const char * op, size_t idx, const std::integral_constant<size_t, SZ>) -> bit_container_type{

        bit_container_type cursor{};
        dg::compact_serializer::core::deserialize(op + slot(idx), cursor);

        return (cursor >> offs(idx)) & ((size_t{1} << SZ) - 1);
    }

    constexpr auto read_bit_padd_requirement() -> size_t{
        
        return sizeof(bit_container_type) * CHAR_BIT;
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
}

namespace dg::huffman_encoder::bit_array{

    using namespace huffman_encoder::types;

    auto make(size_t container, size_t sz) -> bit_array_type{

        return {container, sz};
    }

    auto container(const bit_array_type& data) -> bit_container_type{

        return data.first;
    }

    auto size(const bit_array_type& data) -> size_t{

        return data.second;
    }

    auto array_cap() -> size_t{

        return sizeof(bit_container_type) * CHAR_BIT;
    }

    void append(bit_array_type& lhs, const bit_array_type& rhs){

        lhs.first   |= container(rhs) << size(lhs);
        lhs.second  += size(rhs);
    }
    
    void append(bit_array_type& lhs, bool rhs){

        append(lhs, {static_cast<bit_container_type>(rhs), size_t{1}});
    }

    auto split(const bit_array_type& inp, size_t lhs_sz) -> std::pair<bit_array_type, bit_array_type>{

        auto rhs_sz = size(inp) - lhs_sz; 
        auto rhs    = container(inp) >> lhs_sz;
        auto lhs    = (rhs << lhs_sz) ^ container(inp);

        return {make(lhs, lhs_sz), make(rhs, rhs_sz)};
    }

    auto to_bit_array(char c) -> bit_array_type{

        return {static_cast<bit_container_type>(c), CHAR_BIT};
    } 

    auto to_bit_array(const std::vector<bool>& vec) -> bit_array_type{

        auto rs = bit_array_type{};

        for (size_t i = 0; i < vec.size(); ++i){
            append(rs, vec[i]);
        }

        return rs;
    }
} 

namespace dg::huffman_encoder::bit_stream{

    using namespace huffman_encoder::types; 

    auto stream_to(char * dst, const bit_array_type& src, bit_array_type& stream_buf) -> char *{

        if (bit_array::size(stream_buf) + bit_array::size(src) < bit_array::array_cap()){
            bit_array::append(stream_buf, src);
        } else{
            auto [l, r] = bit_array::split(src, bit_array::array_cap() - bit_array::size(stream_buf));
            bit_array::append(stream_buf, l);
            dst         = dg::compact_serializer::core::serialize(bit_array::container(stream_buf), dst);
            stream_buf  = r;
        }

        return dst;
    } 

    auto exhaust_to(char * dst, bit_array_type& stream_buf) -> char *{

        auto bsz    = byte_array::byte_size(bit_array::size(stream_buf));

        if (bsz == sizeof(bit_container_type)){
            dst = dg::compact_serializer::core::serialize(stream_buf.first, dst);
        } else{
            for (size_t i = 0; i < bsz; ++i){
                (*dst++) = stream_buf.first & char{-1};
                stream_buf.first >>= CHAR_BIT;
            }
        }

        stream_buf.second = 0u;
        return dst;
    }
}

namespace dg::huffman_encoder::make{

    using namespace huffman_encoder::types;
    
    struct CounterNode{
        std::unique_ptr<CounterNode> l;
        std::unique_ptr<CounterNode> r;
        size_t count;
        word_type c;
    };

    auto count(const char * buf, size_t sz) -> std::vector<size_t>{

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

        auto transformer    = [](size_t count){return std::max(size_t{1}, count);};
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

    void encode_dictionarize(model::DelimNode * root, std::vector<std::vector<bool>>& op, std::vector<bool>& trace){

        bool is_leaf = !bool{root->r} && !bool{root->l};

        if (is_leaf){
            if (!root->delim_stat){
                auto num_rep    = num_rep_type{};
                dg::compact_serializer::core::deserialize(root->c.data(), num_rep);
                op[num_rep]     = trace;
            }
        } else{
            trace.push_back(constants::L);
            encode_dictionarize(root->l.get(), op, trace);
            trace.push_back(constants::R);
            encode_dictionarize(root->r.get(), op, trace);
        }

        trace.pop_back();
    }

    auto encode_dictionarize(model::DelimNode * root) -> std::vector<std::vector<bool>>{

        auto rs     = std::vector<std::vector<bool>>(constants::DICT_SIZE);
        auto trace  = std::vector<bool>();
        encode_dictionarize(root, rs, trace);

        return rs;
    }
    
    auto walk(model::DelimNode * root, std::deque<bool> trace) -> std::pair<std::vector<char>, size_t>{

        auto cursor     = root;
        auto init_sz    = trace.size();

        while (!trace.empty()){

            bool cur = trace.front();
            trace.pop_front();

            if (cur == constants::L){
                cursor = cursor->l.get();
            } else{
                cursor = cursor->r.get();
            }

            bool is_leaf = !bool{cursor->l} && !bool{cursor->r};

            if (is_leaf){
                if (cursor->delim_stat){
                    break;
                }
                
                auto [byte_rep, trailing]  = walk(root, trace);

                if (trailing == trace.size()){
                    return {{cursor->c.begin(), cursor->c.end()}, trace.size()};
                }

                auto aggregated = std::vector<char>(cursor->c.begin(), cursor->c.end());
                aggregated.insert(aggregated.end(), byte_rep.begin(), byte_rep.end());

                return {aggregated, trailing};
            }
        }

        return {{}, init_sz};    
    } 

    auto decode_dictionarize(model::DelimNode * root) -> std::vector<std::pair<std::vector<char>, size_t>>{

        auto rs     = std::vector<std::pair<std::vector<char>, size_t>>(constants::DICT_SIZE);

        for (size_t i = 0; i < constants::DICT_SIZE; ++i){
            rs[i]   = walk(root, utility::to_bit_deque(static_cast<num_rep_type>(i))); 
        }

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

    class FastEngine{

        private:

            std::vector<bit_array_type> encoding_dict;
            std::vector<bit_array_type> delim;
            std::unique_ptr<model::DelimNode> delim_tree;
            std::vector<std::pair<std::vector<char>, size_t>> decoding_dict;

        public:

            FastEngine(std::vector<bit_array_type> encoding_dict, 
                       std::vector<bit_array_type> delim, 
                       std::unique_ptr<model::DelimNode> delim_tree,
                       std::vector<std::pair<std::vector<char>, size_t>> decoding_dict): encoding_dict(std::move(encoding_dict)),
                                                                                         delim(std::move(delim)),
                                                                                         delim_tree(std::move(delim_tree)),
                                                                                         decoding_dict(std::move(decoding_dict)){}
             
            auto noexhaust_encode_into(const char * inp_buf, size_t inp_sz, char * op_buf, bit_array_type& rdbuf) const noexcept -> char *{
                
                size_t cycles   = inp_sz / constants::ALPHABET_SIZE; 
                size_t rem      = inp_sz - (cycles * constants::ALPHABET_SIZE);
                auto ibuf       = inp_buf;

                for (size_t i = 0; i < cycles; ++i){
                    auto num_rep    = num_rep_type{};
                    ibuf            = dg::compact_serializer::core::deserialize(ibuf, num_rep);
                    auto& bit_rep   = encoding_dict[num_rep];
                    op_buf          = bit_stream::stream_to(op_buf, bit_rep, rdbuf);
                }

                op_buf  = bit_stream::stream_to(op_buf, this->delim[rem], rdbuf);

                if (rem != 0){
                    for (size_t i = 0; i < rem; ++i){
                        op_buf  = bit_stream::stream_to(op_buf, bit_array::to_bit_array(ibuf[i]), rdbuf);
                    }
                }                

                return op_buf;
            }
            
            auto encode_into(const char * inp_buf, size_t inp_sz, char * op_buf, bit_array_type& rdbuf) const noexcept -> char *{

                return bit_stream::exhaust_to(noexhaust_encode_into(inp_buf, inp_sz, op_buf, rdbuf), rdbuf);
            }
            
            auto fast_decode_into(const char * inp_buf, size_t bit_offs, size_t bit_last, char * op_buf) const noexcept -> std::pair<size_t, char *>{
                
                auto cursor     = this->delim_tree.get();
                auto root       = this->delim_tree.get();
                auto bad_bit    = bool{false};
                 
                while (true){

                    bool dictionary_prereq = (bit_offs + byte_array::read_bit_padd_requirement() < bit_last) && (cursor == root) && (!bad_bit);

                    if (dictionary_prereq){
                        auto tape = byte_array::read_bit(inp_buf, bit_offs, std::integral_constant<size_t, constants::ALPHABET_BIT_SIZE>{});
                        const auto& mapped_bytes = this->decoding_dict[tape];
                        std::memcpy(op_buf, mapped_bytes.first.data(), mapped_bytes.first.size());
                        op_buf   += mapped_bytes.first.size();
                        bit_offs += constants::ALPHABET_BIT_SIZE - mapped_bytes.second;
                        bad_bit  = mapped_bytes.second == constants::ALPHABET_BIT_SIZE;
                    } else{
                        bad_bit     = false;
                        auto tape   = byte_array::read(inp_buf, bit_offs++); 
                        
                        if (tape == constants::L){
                            cursor = cursor->l.get();
                        } else{
                            cursor = cursor->r.get();
                        }

                        bool is_leaf = !bool{cursor->r} && !bool{cursor->l};

                        if (is_leaf){
                            if (cursor->delim_stat){
                                auto trailing_sz    = cursor->delim_stat -1;
                                for (size_t i = 0; i < trailing_sz; ++i){
                                    (*op_buf++) = byte_array::read_byte(inp_buf, bit_offs);
                                    bit_offs += CHAR_BIT;
                                }
                                return {bit_offs, op_buf};
                            }
                            std::memcpy(op_buf, cursor->c.data(), constants::ALPHABET_SIZE);
                            op_buf += constants::ALPHABET_SIZE;
                            cursor = root;
                        }
                    }
                } 
            }

            auto decode_into(const char * inp_buf, size_t bit_offs, char * op_buf) const noexcept -> std::pair<size_t, char *>{

                auto cursor     = this->delim_tree.get();
                auto root       = this->delim_tree.get();
                 
                while (true){
                    auto tape   = byte_array::read(inp_buf, bit_offs++); 
                    
                    if (tape == constants::L){
                        cursor = cursor->l.get();
                    } else{
                        cursor = cursor->r.get();
                    }

                    bool is_leaf = !bool{cursor->r} && !bool{cursor->l};

                    if (is_leaf){
                        if (cursor->delim_stat){
                            auto trailing_sz    = cursor->delim_stat -1;
                            for (size_t i = 0; i < trailing_sz; ++i){
                                (*op_buf++) = byte_array::read_byte(inp_buf, bit_offs);
                                bit_offs += CHAR_BIT;
                            }
                            return {bit_offs, op_buf};
                        }
                        std::memcpy(op_buf, cursor->c.data(), constants::ALPHABET_SIZE);
                        op_buf += constants::ALPHABET_SIZE;
                        cursor = root;
                    }
                } 
            }
    };

    // //API - should consider array approach if compile-time deterministic - 
    class RowEncodingEngine{

        private:

            std::vector<std::unique_ptr<FastEngine>> encoders;
        
        public:

            RowEncodingEngine(std::vector<std::unique_ptr<FastEngine>> encoders): encoders(std::move(encoders)){}

            auto encode_into(const std::vector<std::pair<const char *, size_t>>& data, char * buf) const -> char *{

                assert(data.size() == this->encoders.size()); 
                auto rdbuf = types::bit_array_type{};

                for (size_t i = 0; i < data.size(); ++i){
                    buf = this->encoders[i]->noexhaust_encode_into(data[i].first, data[i].second, buf, rdbuf);
                }

                return bit_stream::exhaust_to(buf, rdbuf);
            }

            auto decode_into(const char * buf, std::vector<std::pair<char *, size_t>>& data) const -> const char *{

                assert(data.size() == this->encoders.size());
                size_t buf_bit_offs = 0u;
                auto last   = std::add_pointer_t<char>();

                for (size_t i = 0; i < this->encoders.size(); ++i){
                    std::tie(buf_bit_offs, last) = this->encoders[i]->decode_into(buf, buf_bit_offs, data[i].first);
                    data[i].second = std::distance(data[i].first, last); 
                }

                return buf + byte_array::byte_size(buf_bit_offs);
            }
    };
}

namespace dg::huffman_encoder::user_interface{

    using namespace huffman_encoder::types; 

    auto count(const char * buf, size_t sz) -> std::vector<size_t>{

        return make::count(buf, sz);
    }

    auto build(std::vector<size_t> counter) -> std::unique_ptr<model::Node>{

        auto counter_node   = make::build(make::defaultize_noncounted(std::move(counter)));
        return make::to_model(counter_node.get());
    }

    auto spawn_fast_engine(model::Node * huffman_tree) -> core::FastEngine{

        auto delim          = std::vector<std::vector<bool>>(constants::ALPHABET_SIZE);
        auto decoding_tree  = make::to_delim_tree(huffman_tree);
        auto decoding_dict  = make::decode_dictionarize(decoding_tree.get());
        auto encoding_dict  = make::encode_dictionarize(decoding_tree.get());
        make::find_delim(decoding_tree.get(), delim);

        auto transformed_ed = utility::vector_transform(encoding_dict, static_cast<bit_array_type(*)(const std::vector<bool>&)>(bit_array::to_bit_array));
        auto transformed_dl = utility::vector_transform(delim, static_cast<bit_array_type(*)(const std::vector<bool>&)>(bit_array::to_bit_array));
        
        return core::FastEngine(std::move(transformed_ed), std::move(transformed_dl), std::move(decoding_tree), std::move(decoding_dict));
    }

    auto spawn_row_engine(std::vector<std::unique_ptr<core::FastEngine>> engines){

        return std::make_unique<core::RowEncodingEngine>(std::move(engines));
    }

}

#endif