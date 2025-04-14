#include <cello/cello.hpp>
#include <util/statics.hpp>
#include <algorithm>
#include "common.hpp"

DRAM(csx_type) A;
DRAM(csx_type) B;
DRAM(csx_type) C;
DRAM(partial_table_vector) C_product;

#define fmadd_asm(rd_p, rs1_p, rs2_p, rs3_p)                            \
    asm volatile ("fmadd.s %[rd], %[rs1], %[rs2], %[rs3]"               \
                  : [rd] "=f" (rd_p)                                    \
                  : [rs1] "f" ((rs1_p)), [rs2] "f" ((rs2_p)), [rs3] "f" ((rs3_p)))

#define dbg(fmt, ...)

namespace _exclusive_scan {    
inline index_type floor_log2(index_type x)
{
    index_type i = -1;
    index_type j = i+1;
    while  ((1 << j) <= x) {
        i = j;
        j = j+1;
    }
    return i;
}

inline index_type ceil_log2(index_type x)
{
    index_type j = 0;
    while (x > (1<<j)) {
        j = j+1;
    }
    return j;
}

inline index_type tree_levels(index_type leafs)
{
    return ceil_log2(leafs)+1;
}
    
inline index_type tree_rchild(index_type root)
{
    return 2*root + 2;
}

inline index_type tree_lchild(index_type root)
{
    return 2*root + 1;
}
}

inline void exclusive_scan
(partial_table **in, index_type *out, index_type n) {
    using namespace _exclusive_scan;
    size_t REGIONS = 1;
    while (REGIONS < n) {
        REGIONS <<= 1;
    }
    REGIONS >>= 1;

    size_t region_size = (n + (REGIONS-1))/REGIONS;
    while (region_size < 128) {
        REGIONS >>= 1;
        region_size = (n + (REGIONS-1))/REGIONS;
    }
    REGIONS = std::max(REGIONS, (size_t)1);
    index_type tree_size = 1<<tree_levels(REGIONS);

    // allocate a tree and zero it out
    dbg("exclusive_scan<%d>(%p,%p,%d): "
           "allocating tree with size = %d\n",
        REGIONS, in, out, n, tree_size);    
    std::atomic<index_type> *tree = (std::atomic<index_type>*)cello::allocate(sizeof(std::atomic<index_type>)*tree_size);
    dbg("exclusive_scan: tree = %p\n",
        tree);
    for (index_type i = 0; i < tree_size; i++) {
        tree[i] = 0;
    }

    // perform exclusive scan on each region
    cello::foreach<cello::parallel>
        (static_cast<index_type>(0),
         static_cast<index_type>(REGIONS),
         [=](index_type tid){
        // calculate range
        index_type region_size = (n + (REGIONS-1))/REGIONS;
        index_type start = tid * region_size;
        index_type end   = std::min(start + region_size, n);

        dbg("exclusive scan on region %d: "
            "start = %d, end = %d\n",
            tid, start, end);
        // calculate local sum
        index_type sum = 0;
        for (index_type i = start; i < end; i++) {
            out[i] = sum;
#ifdef USE_RB_TREE
            sum += in[i]->size;
#else
            sum += in[i]->size();
#endif
        }

        // update tree
        index_type r = 0;
        index_type m = REGIONS;
        index_type L = tree_levels(m);
        dbg("exclusive_scan (1): adding %d\n", sum);
        index_type l;
        for (l = 0; l < L; l++) {
            tree[r] += sum;
            dbg("exclusive_scan (1): m = %d, l = %d, r = %d, tid = %d\n",
                m, l, r, tid);
            m >>= 1;
            dbg("exclusive_scan (1): m & tid\n",
                m & tid);
            if (m & tid) {
                r = tree_rchild(r);
            } else {
                r = tree_lchild(r);
            }
        }
    });

    dbg("exclusive_scan: tree[0]=%d\n", tree[0]);
    
    // update with offset for each region
    cello::foreach<cello::parallel>(static_cast<index_type>(0ul),
                                    static_cast<index_type>(REGIONS),
                                    [=](index_type tid){
        // calculate range
        index_type region_size = (n + (REGIONS-1))/REGIONS;
        index_type start = tid * region_size;
        index_type end   = std::min(start + region_size, n);

        // accumulate from sum tree
        index_type s = 0;
        index_type r = 0;
        index_type m = REGIONS;
        index_type L = tree_levels(m);
            
        for (index_type l = 0; l < L; l++) {
            dbg("exclusive_scan (2): m = %d, l = %d, r = %d, tid = %d\n",
                m, l, r, tid);
            m >>= 1;
            if (tid & m) {
                dbg("exclusive_scan (2): m = %d, l = %d, r = %d, tid = %d, adding\n",
                    m, l, r, tid);
                s += tree[tree_lchild(r)];
                r = tree_rchild(r);
            } else {
                r = tree_lchild(r);
            }
        }

        // calculate local sum
        for (index_type i = start; i < end; i++) {
            out[i] += s;
        }
    });

    cello::deallocate(tree, sizeof(std::atomic<index_type>)*tree_size);
}

int cello_main(int argc, char *argv[])
{
    dbg("A: %d rows, %d columns, %d non-zeros\n", A.rows(), A.cols(), A.nnz());
    dbg("B: %d rows, %d columns, %d non-zeros\n", B.rows(), B.cols(), B.nnz());
    bsg_print_hexadecimal(0xA);
    bsg_fence();
    C_product.foreach([](int i, partial_table *& result) {
        partial_table *accum = static_cast<partial_table*>(cello::allocate(sizeof(partial_table)));
        new (accum) partial_table();
        auto [A_idx_start, A_idx_end, A_val_start, A_val_end] = A.inner_indices_values_range_lcl(i);        
        asm volatile ("" ::: "memory");
        
        value_type *A_val_p = A_val_start;
        for (index_type *A_idx_p = A_idx_start;
             A_idx_p != A_idx_end;
             A_idx_p++, A_val_p++) {
            index_type A_idx = *A_idx_p;
            value_type A_val = *A_val_p;

            auto [B_idx_start, B_idx_end, B_val_start, B_val_end] = B.inner_indices_values_range(A_idx);
            auto B_val_p = B_val_start;
            for (auto B_idx_p = B_idx_start;
                 B_idx_p != B_idx_end;
                 B_idx_p++, B_val_p++) {
                index_type B_idx = *B_idx_p;
                value_type B_val = *B_val_p;
                value_type C_val = A_val * B_val;
                auto C_entry = accum->find(B_idx);
#ifdef USE_RB_TREE
                if (C_entry == nullptr) {
                    accum->insert(B_idx, C_val);
                } else {
                    value_type C_val = C_entry->val;
                    fmadd_asm(C_val, A_val, B_val, C_val);
                    C_entry->val = C_val;
                }
#else
                if (C_entry == accum->end()) {
                    accum->insert({B_idx, C_val});
                } else {
                    value_type C_val = C_entry->second;
                    fmadd_asm(C_val, A_val, B_val, C_val);
                    C_entry->second = C_val;
                }
#endif
            }
        }
        result = accum;
    });
    bsg_print_hexadecimal(0xB);
    bsg_fence();
    cello::on_every_pod([](){
        exclusive_scan
            (C_product.data(),
             C.outer_pointers().data(),
             C.outer_pointers().local_size()
             );
        index_type nnz = C.outer_pointers().data()[C.outer_pointers().local_size()-1];
        C.inner_indices() = static_cast<index_type*>(cello::allocate(sizeof(index_type)*nnz));
        C.values() = static_cast<value_type*>(cello::allocate(sizeof(value_type)*nnz));
        for (index_type x = 0; x < cello::my::num_pods_x(); x++) {
            for (index_type y = 0; y < cello::my::num_pods_y(); y++) {
                bsg_global_pointer::pod_address pod(x,y);
                bsg_global_pointer::pod_address_guard guard(pod);
                std::atomic<index_type> *nnzp = reinterpret_cast<std::atomic<index_type>*>(&C.nnz());
                nnzp->fetch_add(nnz);
            }
        }
    });
    bsg_print_hexadecimal(0xC);
    bsg_fence();
    C_product.foreach([](index_type i, partial_table * nonzeros) {
        auto [C_idx_p, _, C_val_p, __] = C.inner_indices_values_range_lcl(i);
#ifdef USE_RB_TREE
        for (partial_table::iterator it(nonzeros); it.good(); it.next()) {
            index_type C_idx = it->key;
            value_type C_val = it->val;
            *C_idx_p++ = C_idx;
            *C_val_p++ = C_val;            
        }
#else
        for (auto it = nonzeros->begin(); it != nonzeros->end(); it++) {
            index_type C_idx = it->first;
            value_type C_val = it->second;
            *C_idx_p++ = C_idx;
            *C_val_p++ = C_val;
        }
#endif
    });
    bsg_print_hexadecimal(0xD);
    bsg_fence();
    return 0;
}
