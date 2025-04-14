#ifndef COMMON_HPP
#define COMMON_HPP
#include <datastructure/csx.hpp>
#include <util/class_field.hpp>
#include <util/list.hpp>
#ifndef HOST
#include <utility>
//#define USE_RB_TREE
#ifdef  USE_RB_TREE
#include "rb_tree.hpp"
#else
#include <map>
#endif
#include <cello/cello.hpp>
#endif

/* data value type */
using value_type = float;

/* indexing type */
using index_type = int32_t;

/* nonzero type */
struct nonzero_type {
    FIELD(index_type, idx);
    FIELD(value_type, val);
};

#ifndef HOST
/* table of partial results */
#ifdef  USE_RB_TREE
using partial_table = rb_tree<index_type, value_type>;
#else
using partial_table = std::map<index_type, value_type, std::less<index_type>, cello::allocator<std::pair<const index_type, value_type>>>;
#endif
using partial_table_ptr = partial_table*;
#else
using partial_table_ptr = hb_mc_eva_t;
#endif

/* compressed sparse matrix type */
using csx_type = datastructure::compressed_sparse_matrix<value_type, index_type, datastructure::CSX_FLAG_ROW_MAJOR>;

/* partial table vector type */
using partial_table_vector = datastructure::vector<partial_table_ptr, 1>;

#ifdef HOST
using csx_mirror = csx_type::mirror_type;
using partial_table_vector_mirror = partial_table_vector::mirror_type;
#endif




#endif
