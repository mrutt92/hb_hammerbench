#ifndef CELLO_JOINER_HPP
#define CELLO_JOINER_HPP
#include <util/class_field.hpp>
#include <bsg_manycore.h>
#include <bsg_tile_config_vars.h>
#include <cstdint>
#include <atomic>
#include <cello/pointer.hpp>
#include <cello/thread_id.hpp>
#include <cello/allocator.hpp>
namespace cello
{

/**
 * @brief joiner base class
 */
class joiner_base
{
public:
    virtual bool joined() const = 0;
};

// forward declarations   
class   single_child;
class  triplet_child;
class      nth_child;

/**
 * @brief Joiner class
 */
class one_child_joiner : public joiner_base
{
public:
    typedef single_child child;

    /**
     * currently only one child supported
     */
    one_child_joiner() : ready_(0) {}

    /**
     * signal that child has completed
     */
    void increment_ready_count() {
        //bsg_print_hexadecimal((unsigned)this | 0x40000000);
        ready() = 1;
    }

    /**
     * join has completed
     */
    bool joined() const override;

    /**
     * returns a created child
     */
    single_child make_child();

    FIELD(int, ready); //!< ready flag
};
}

/**
 * @brief specialization of global_pointer::reference for one_child_joiner
 */
template <>
class bsg_global_pointer::reference<cello::one_child_joiner> {
public:
    BSG_GLOBAL_POINTER_REFERENCE_TRIVIAL(cello::one_child_joiner);
    void increment_ready_count();
    //BSG_GLOBAL_POINTER_REFERENCE_METHOD(cello::one_child_joiner, increment_ready_count);
};

namespace cello
{
/**
 * @brief an only child of a joiner
 */
class single_child {
public:
    /**
     * @brief constructor
     */
    single_child(const global_pointer<one_child_joiner> &p) : parent_(p) {}
    single_child() = default;
    single_child(const single_child&) = default;
    single_child& operator=(const single_child&) = default;
    single_child(single_child&&) = default;
    single_child& operator=(single_child&&) = default;
    
    /**
     * @brief join with parent
     */
    void join() {
        parent()->increment_ready_count();
    }

    FIELD(global_pointer<one_child_joiner>, parent); //!< pointer to parent
};


/**
 * returns a created child
 */
inline single_child one_child_joiner::make_child() {
    global_pointer<one_child_joiner> p = global_pointer<one_child_joiner>::onPodXY(my::pod_x(), my::pod_y(), this);
    return single_child(p);
}

}

namespace cello
{
/**
 * @brief joiner class with n children
 */
class n_child_joiner : public joiner_base
{
public:
    typedef nth_child child;

    /**
     * constructor
     */
    n_child_joiner()
        : ready_(nullptr) {
        ready_ = reinterpret_cast<decltype(ready_)>(cello::allocate(sizeof(*ready_)));
        ready_->store(0);
    }

    /**
     * destructor
     */
    ~n_child_joiner() {
        cello::deallocate(ready_, sizeof(*ready_));
    }

    /**
     * make a new child
     */
    child make_child();

    /**
     * signal that child has completed
     */
    void increment_ready_count() {
        ready_->fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief check that all children have joined
     */
    bool joined() const override;

    std::atomic<int>* ready_;
    int children_ = 0;
};
}

/**
 * @brief specialization of global_pointer::reference for n_child_joiner
 */
template <>
class bsg_global_pointer::reference<cello::n_child_joiner> {
public:
    BSG_GLOBAL_POINTER_REFERENCE_TRIVIAL(cello::n_child_joiner);
    void increment_ready_count();
};

namespace cello
{
/**
 * @brief a child with unknown number of siblings
 */
class nth_child {
public:
    nth_child(global_pointer<n_child_joiner> parent) : parent_(parent) {}
    void join() {
        parent_->increment_ready_count();
    }
    global_pointer<n_child_joiner> parent_;
};

/**
 * make a new child
 */
inline nth_child n_child_joiner::make_child() {
    children_++;
    return nth_child(global_pointer<n_child_joiner>::onPodXY(my::pod_x(), my::pod_y(), this));
}

}

namespace cello
{
/**
 * @brief joiner class with four children
 */
class three_child_joiner : public joiner_base
{
public:
    typedef triplet_child child;
    /**
     * make a new child
     */
    child make_child();

    /**
     * @brief check that all children have joined
     */
    bool joined() const override;

    union {
        uint32_t ready_      = 0;
        char     child_ready_[4];
    };

    unsigned children_made_ = 0;
};

/**
 * @brief a child of this joiner
 */
class triplet_child {
public:
    triplet_child(global_pointer<char> ready) : ready_(ready) {}
    void join() {
        *ready_ = -1;
    }
    global_pointer<char> ready_;
};

/**
 * make a new child
 */
inline triplet_child three_child_joiner::make_child() {
    return triplet_child(global_pointer<char>(&this->child_ready_[children_made_++]));
}

}

inline void bsg_global_pointer::reference<cello::one_child_joiner>::increment_ready_count() {
    register cello::one_child_joiner *p = reinterpret_cast<cello::one_child_joiner*>(addr().raw());
    {
        pod_address_guard grd(addr().ext().pod_addr());
        p->increment_ready_count();
    }
}

inline void bsg_global_pointer::reference<cello::n_child_joiner>::increment_ready_count() {
    register cello::n_child_joiner *p = reinterpret_cast<cello::n_child_joiner*>(addr().raw());
    {
        pod_address_guard grd(addr().ext().pod_addr());
        p->increment_ready_count();
    }
}

#endif
