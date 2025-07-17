#ifndef DATASTRUCTURE_VECTOR_HPP
#define DATASTRUCTURE_VECTOR_HPP

#ifdef CELLO_GLOBAL_STEALING
//#error "unrestricted work stealing not supported"
#endif

#include <global_pointer/global_pointer.hpp>
#include <datastructure/types.hpp>
#include <datastructure/id.hpp>

#ifndef HOST
#include <cello/foreach.hpp>
#include <cello/foreach_block.hpp>
#include <cello/delegate.hpp>
#include <vector>
#else
#include <cassert>
#include <bsg_manycore.h>
#include <bsg_manycore_cuda.h>
#endif

namespace datastructure
{
#ifdef HOST
template <typename T, size_t STRIDE>
class vector_host_mirror;
#endif

template <typename T, size_t STRIDE = 1>
class vector
{
public:
#ifndef HOST
    using value_type = T;
    using value_pointer = T *;
    using value_reference = T &;
    using value_constpointer = const T *;
    using value_constreference = const T &;
    using value_gpointer = bsg_global_pointer::pointer<T>;
    using value_greference = bsg_global_pointer::reference<T>;
    using value_gconstpointer = bsg_global_pointer::pointer<const T>;
    using value_gconstreference = bsg_global_pointer::reference<const T>;
#else    
    using value_type = T;
    using value_pointer = hb_mc_eva_t;
    //using value_reference = T &;
    //using value_constpointer = const T *;
    //using value_constreference = const T &;
    using value_gpointer = bsg_global_pointer::pointer<T>;
    using value_greference = bsg_global_pointer::reference<T>;
    using value_gconstpointer = bsg_global_pointer::pointer<const T>;
    using value_gconstreference = bsg_global_pointer::reference<const T>;
    using mirror_type = vector_host_mirror<T, STRIDE>;
    using mirror = vector_host_mirror<T, STRIDE>;    
#endif
    static constexpr size_t stride = STRIDE;
#ifndef HOST
    template <typename cello::Schedule sched = cello::parallel, typename F>
    void foreach(F && f) {
        using namespace cello;
        using joiner = n_child_joiner;
        joiner j;
        joiner *jp = bsg_tile_group_remote_pointer<joiner>(tile_x(), tile_y(), &j);
        on_every_pod(jp, [this, f](){
            size_t start = STRIDE * pod_id();
            size_t step = STRIDE * num_pods();
            size_t end = size();
            cello::foreach<sched>
                (start, end, step, [this, f, end](size_t i) {
                size_t start = i;
                size_t stop = i + STRIDE;
                size_t sz = end;
                asm volatile ("" ::: "memory");
                if (stop > sz) {
                    stop = sz;
                }
                for (size_t j = start; j < stop; j++) {
                    f(j, this->local(j));
                }
            });
            //bsg_print_int(2000000+cello::my::pod_id());
        });
        wait(&j);
    }

    template <typename cello::Schedule sched = cello::parallel, typename F>
    void foreach(size_t grain, F && f) {
        using namespace cello;
        using joiner = n_child_joiner;
        joiner j;
        joiner *jp = bsg_tile_group_remote_pointer<joiner>(tile_x(), tile_y(), &j);
        on_every_pod(jp, [this, f, grain](){
            size_t start = STRIDE * pod_id();
            size_t step = STRIDE * num_pods();
            size_t end = size();
            value_pointer _data = this->data();
            cello::foreach<sched>
                (start, end, step, grain, [this, _data, f, end](size_t i) {
                size_t start = i;
                size_t stop = i + STRIDE;
                size_t sz = end;
                asm volatile ("" ::: "memory");
                if (stop > sz) {
                    stop = sz;
                }
                for (size_t j = start; j < stop; j++) {
                    f(j, _data[this->lcl(j)]);
                }
            });
            //bsg_print_int(2000000+cello::my::pod_id());
        });
        wait(&j);
    }

    template <typename cello::Schedule sched = cello::parallel, typename F>
    void foreach_unrestricted(size_t grain, F && f) {
        using namespace cello;
        size_t start = 0;
        size_t end = size();
        size_t step = STRIDE;
        cello::foreach<sched>
            (start, end, stride, grain, [this, f, end](size_t i){
                size_t start = i;
                size_t stop = i + STRIDE;
                size_t sz = end;
                asm volatile ("" ::: "memory");
                if (stop > sz) {
                    stop = sz;
                }
                for (size_t j = start; j < stop; j++) {
                    f(j, this->at(j));
                }
        });
    }


    template <typename cello::Schedule sched = cello::parallel, typename F>
    void foreach_unrestricted(F && f) {
        using namespace cello;
        size_t start = 0;
        size_t end = size();
        size_t step = STRIDE;
        cello::foreach<sched>
            (start, end, stride, [this, f, end](size_t i){
                size_t start = i;
                size_t stop = i + STRIDE;
                size_t sz = end;
                asm volatile ("" ::: "memory");
                if (stop > sz) {
                    stop = sz;
                }
                for (size_t j = start; j < stop; j++) {
                    f(j, this->at(j));
                }
        });
    }

    value_reference local(size_t i) {
        return data()[lcl(i)];
    }

    value_constreference local(size_t i) const {
        return data()[lcl(i)];
    }
#endif

#define VECTOR_INDEX_METHODS(type)                                      \
    size_t pod(size_t i) const {                                        \
        return (i % (STRIDE * datastructure::num_pods())) / STRIDE;     \
    }                                                                   \
    int pod_x(size_t i) const {                                         \
        return pod(i) % datastructure::num_pods_x();                    \
    }                                                                   \
    int pod_y(size_t i) const {                                         \
        return pod(i) / datastructure::num_pods_x();                    \
    }                                                                   \
    size_t lcl(size_t i) const {                                        \
        return STRIDE*(i / (datastructure::num_pods()*STRIDE))          \
            +  i % STRIDE;                                              \
    }                                                                   \

#ifndef HOST
#define VECTOR_AT_PTR_METHODS(type)                                     \
    VECTOR_INDEX_METHODS(type);                                         \
    typename type::value_gpointer at_ptr(size_t i) {                    \
        return type::value_gpointer::onPodXY(pod_x(i), pod_y(i), data()+lcl(i)); \
    }                                                                   \
    typename type::value_gconstpointer at_ptr(size_t i) const {         \
        return type::value_gconstpointer::onPodXY(pod_x(i), pod_y(i), data()+lcl(i)); \
    }
#else
#define VECTOR_AT_PTR_METHODS(type)                                     \
    VECTOR_INDEX_METHODS(type);                                         \
    typename type::value_gpointer at_ptr(size_t i) {                    \
        uintptr dataptr = data();                                       \
        return type::value_gpointer::onPodXY(pod_x(i), pod_y(i), dataptr + lcl(i)*sizeof(typename type::value_type)); \
    }                                                                   \
    typename type::value_gconstpointer at_ptr(size_t i) const {         \
        uintptr dataptr = data();                                       \
        return type::value_gconstpointer::onPodXY(pod_x(i), pod_y(i), dataptr + lcl(i)*sizeof(typename type::value_type)); \
    }
#endif

#define VECTOR_AT_METHODS(type)                                         \
    VECTOR_AT_PTR_METHODS(type);                                        \
    typename type::value_greference at(size_t i) {                      \
        return *at_ptr(i);                                              \
    }                                                                   \
    typename type::value_gconstreference at(size_t i) const {           \
        return *at_ptr(i);                                              \
    }                                                                   \
    typename type::value_greference operator[](size_t i) {              \
        return at(i);                                                   \
    }                                                                   \
    typename type::value_gconstreference operator[](size_t i) const {   \
        return at(i);                                                   \
    }

    VECTOR_AT_METHODS(vector);
    FIELD(value_pointer ,data);
    FIELD(uintptr       ,size);
    FIELD(uintptr       ,local_size);
};
}

template <typename T, size_t STRIDE>
class bsg_global_pointer::reference<datastructure::vector<T, STRIDE>>
{
public:
    using type = datastructure::vector<T, STRIDE>;
    BSG_GLOBAL_POINTER_REFERENCE_TRIVIAL(type);
    BSG_GLOBAL_POINTER_REFERENCE_FIELD(type, data);
    BSG_GLOBAL_POINTER_REFERENCE_FIELD(type, size);
    BSG_GLOBAL_POINTER_REFERENCE_FIELD(type, local_size);
    VECTOR_AT_METHODS(type);
};

#ifdef HOST
namespace datastructure
{
/**
 * @brief A class that is meant to mirror a vector on the device from the host
 */
template <typename T, size_t STRIDE = 1>
class vector_host_mirror
{
public:
    using vector_type = datastructure::vector<T, STRIDE>;

    VECTOR_INDEX_METHODS(vector_host_mirror);


    hb_mc_pod_id_t host_pod_index(hb_mc_coordinate_t pod) const {
        return hb_mc_coordinate_to_index(pod, bsg_global_pointer::the_device->mc->config.pods);
    }

    hb_mc_pod_id_t host_pod_index(size_t i) const {
        hb_mc_coordinate_t pod = {.x=pod_x(i), .y=pod_y(i)};
        return host_pod_index(pod);
    }

    T& at(size_t i) {
        return data[host_pod_index(i)][lcl(i)];
    }

    const T& at(size_t i) const {
        return data[host_pod_index(i)][lcl(i)];
    }

    std::vector<T>& vector_of(size_t i) {
        return data[host_pod_index(i)];
    }

    /**
     * do something foreach pod coordinate
     */
    template <typename F>
    int foreach_pod_id(F && f) {
        hb_mc_coordinate_t pod;
        constexpr hb_mc_coordinate_t zero = {0,0};
        constexpr hb_mc_dimension_t pods = {.x=bsg_pods_X, .y=bsg_pods_Y};
        foreach_coordinate(pod, zero, pods) {
            BSG_CUDA_CALL(f(host_pod_index(pod)));
        }
        return 0;
    }


    /**
     * do something foreach pod coordinate
     */
    template <typename F>
    int foreach_pod(F && f) {
        hb_mc_coordinate_t pod;
        constexpr hb_mc_coordinate_t zero = {0,0};
        constexpr hb_mc_dimension_t pods = {.x=bsg_pods_X, .y=bsg_pods_Y};
        foreach_coordinate(pod, zero, pods) {
            BSG_CUDA_CALL(f(pod));
        }
        return 0;
    }

    /**
     * @brief Construct a vector_host_mirror from a vector
     * @param vptr A pointer to the vector to mirror
     */
    vector_host_mirror(bsg_global_pointer::pointer<vector_type> vptr) : vptr(vptr) {
        data.resize(bsg_global_pointer::the_device->num_pods);
    }

    /**
     * @brief Free memory on the device and set the size to 0
     */
    int clear_device() {
        BSG_CUDA_CALL(foreach_pod([=](hb_mc_coordinate_t pod) {
            hb_mc_pod_id_t pod_id = host_pod_index(pod);
            vptr.set_pod_x(pod.x).set_pod_y(pod.y);
            hb_mc_eva_t eva = vptr->data();
            BSG_CUDA_CALL(hb_mc_device_pod_free(bsg_global_pointer::the_device, pod_id, eva));
            vptr->data() = 0;
            vptr->size() = 0;
            vptr->local_size() = 0;
            return 0;
        }));
        return 0;
    }

    /**
     * @brief Free memory on the host and set the size to 0
     */
    int clear_host() {
        for (auto & v :data) {
            v.clear();
        }
        size = 0;
        return 0;
    }

    /**
     * @brief Initialize the device from the host
     *
     * Set the size to that of the host and allocate memory on the device
     */
    int init_device_from_host() {
        BSG_CUDA_CALL(clear_device());
        hb_mc_pod_id_t pod_id;
        size_t alloc_sz = (size + (datastructure::num_pods()-1))/datastructure::num_pods();
        BSG_CUDA_CALL(foreach_pod([=](hb_mc_coordinate_t pod){
            hb_mc_pod_id_t pod_id = host_pod_index(pod);
            hb_mc_eva_t eva;
            assert(alloc_sz >= data[pod_id].size() && "bad allocation size");
            BSG_CUDA_CALL(hb_mc_device_pod_malloc(bsg_global_pointer::the_device, pod_id, alloc_sz * sizeof(T), &eva));
            vptr.set_pod_x(pod.x)
                .set_pod_y(pod.y);
            vptr->data() = eva;
            vptr->size() = size;
            vptr->local_size() = data[pod_id].size();
            return 0;
        }));
        return 0;
    }

    /**
     * @brief Initialize the host from the device
     *
     * Set the size to that of the device and allocate memory on the host
     */
    int init_host_from_device() {
        clear_host();
        size = vptr->size();
        size_t sz = (size + bsg_global_pointer::the_device->num_pods - 1) / bsg_global_pointer::the_device->num_pods;
        BSG_CUDA_CALL(foreach_pod([=](hb_mc_coordinate_t pod){
            vptr.set_pod_x(pod.x)
                .set_pod_y(pod.y);
            data[host_pod_index(pod)].resize(vptr->local_size());
            return 0;
        }));
        return 0;
    }

    /**
     * @brief Initialize the host from a vector
     * @param init_data The vector to initialize from
     */
    int init_host_from(const std::vector<T> & init_data) {
        size = init_data.size();
        data.resize(bsg_global_pointer::the_device->num_pods);
        for (size_t i = 0; i < size; i++) {
            vector_of(i).push_back(init_data[i]);
        }

        return 0;
    }

    /**
     * @brief Synchronize the device with the host
     */
    int sync_device(std::vector<std::vector<hb_mc_dma_htod_t>> & jobs_in) {
        clear_device();
        init_device_from_host();

        BSG_CUDA_CALL(foreach_pod_id([&](hb_mc_pod_id_t pod_id){
            jobs_in[pod_id].push_back({
                    vptr->data(), data[pod_id].data(), data[pod_id].size() * sizeof(T)
                });
            return 0;
        }));
        return 0;
    }

    /**
     * @brief Synchronize the host with the device
     */
    int sync_host(std::vector<std::vector<hb_mc_dma_dtoh_t>> & jobs_out) {
        clear_host();
        init_host_from_device();
        BSG_CUDA_CALL(foreach_pod_id([&](hb_mc_pod_id_t pod_id){
            jobs_out[pod_id].push_back({
                    vptr->data(), data[pod_id].data(), data[pod_id].size() * sizeof(T)
                });
            return 0;
        }));
        return 0;
    }


    bsg_global_pointer::pointer<vector_type> vptr;
    std::vector<std::vector<T>> data;
    size_t size = 0;
};
}
#endif
#endif
