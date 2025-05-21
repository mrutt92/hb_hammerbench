#include <util/lock.hpp>
#include <util/list.hpp>
#include <util/statics.hpp>
#include <cello/scheduler.hpp>
#include <cello/task_queue.hpp>
#include <cello/task.hpp>
#include <cello/thread_id.hpp>
#include <cello/pointer.hpp>
#include <cello/delegate_queue.hpp>
#include <cello/joiner.hpp>
#include <global_pointer/global_pointer.hpp>
#include <bsg_manycore.h>
#include <bsg_manycore.hpp>
#include <bsg_tile_config_vars.h>
namespace cello
{

DMEM(int) scheduler_seed = 0;

// Not great in terms of randomness, but should be faster than rand()
inline int fast_random()
{
    scheduler_seed = ( 214013 * scheduler_seed + 2531011 );
    return ( scheduler_seed >> 16 ) & 0x7FFF;
}

using work_queue = util::lockable<task_queue, util::tile_lock>;
using del_queue  = util::lockable<delegate_queue, util::tile_lock>;

/**
 *  my queue
 */
DMEM(work_queue) my_tasks;
DMEM(work_queue *) my_tasks_ptr = &my_tasks;
DMEM(del_queue) my_delegates;
DMEM(del_queue *) my_delegates_ptr = &my_delegates;

#ifdef GLOBAL_STEALING
global_pointer<work_queue>
#else
work_queue*
#endif
tasks_of(int id)
{
    thread_id_decoded decode;
    thread_id_decode(&decode, id);

    work_queue *lcl = bsg_tile_group_remote_pointer<work_queue>(decode.tile_x, decode.tile_y, &my_tasks);
#ifdef GLOBAL_STEALING
    global_pointer<work_queue> glbl = global_pointer<work_queue>::onPodXY(decode.pod_x, decode.pod_y, lcl);
#else
    return lcl;
#endif
}

global_pointer<del_queue> delegates_of(int id)
{
    thread_id_decoded decode;
    thread_id_decode(&decode, id);

    del_queue *lcl = bsg_tile_group_remote_pointer<del_queue>(decode.tile_x, decode.tile_y, &my_delegates);
    global_pointer<del_queue> glbl = global_pointer<del_queue>::onPodXY(decode.pod_x, decode.pod_y, lcl);
    return glbl;
}

/**
 * @brief initialize the scheduler
 */
void scheduler_initialize(config *cfg)
{
    scheduler_seed = my::tile_id();
    my_tasks_ptr = bsg_tile_group_remote_pointer<work_queue>(my::tile_x(), my::tile_y(), &my_tasks);
    new (my_tasks_ptr) work_queue;
    my_delegates_ptr = bsg_tile_group_remote_pointer<del_queue>(my::tile_x(), my::tile_y(), &my_delegates);
    new (my_delegates_ptr) delegate_queue;
}

/**
 * detach a task from the current thread of execution
 */
void spawn(task * t)
{
    my_tasks_ptr->owner_push(t);
}

void execute_task(int victim_id, task * t)
{
    t->execute();
}

void execute_task(int victim_id, global_pointer<task> t)
{
    thread_id_decoded decode;
    thread_id_decode(&decode, victim_id);
    if (decode.pod != my::pod_id()) {
        task *lcl = reinterpret_cast<task*>(allocate(t->size()));
        bsg_global_pointer::memcpy
            (reinterpret_cast<char*>(lcl)
             ,bsg_global_pointer::pointer_cast<char, task>(t)
             ,t->size()
             );
        lcl->execute();
        deallocate(lcl, t->size());
    }else {
        t->execute();
    }
}


/**
 * delegate a task to a tile
 */
void delegate(int delegate_id, task *t)
{
    auto delegate_tasks = delegates_of(delegate_id);
    global_pointer<task> glbl = global_pointer<task>::onPodXY(my::pod_x(), my::pod_y(), t);
    delegate_tasks->delegater_push(glbl);
}

/**
 * delegate a task to a pod
 */
void delegate_pod(int pod_id, task *t)
{
    // select a random tile in the pod
    int tile = fast_random() % my::num_tiles();
    tile += pod_id * my::num_tiles();
    delegate(tile, t);
}


/**
 * schedule work on this thread
 */
void schedule()
{
    task *t = nullptr;
    // 1. check delegates
    t = my_delegates_ptr->owner_pop();
    if (t) {
        t->execute();
        //delete t;
        return;
    }
    // 2. check local tasks
    t = my_tasks_ptr->owner_pop();
    if (t) {
        t->execute();
        return;
    }
    // 3. steal work
    int victim_id = fast_random() % my::num_tiles();
    auto victim_tasks = tasks_of(victim_id);
    auto stolen = victim_tasks->thief_pop();
    if (!is_null(stolen)) {
        //bsg_print_int(1000000 + victim_id*1000 + my::id());            
        execute_task(victim_id, stolen);
    }
}


/**
 * @brief check that all children have joined
 */
bool n_child_joiner::joined() const {
    return ready_->load() == children_;
}

/**
 * @brief check that all children have joined
 */
bool three_child_joiner::joined() const {
    return ready_ == 0x00ffffff;
}

/**
 * @brief join has completed
 */
bool one_child_joiner::joined() const {
    return ready();
}

/**
 * wait for a joiner to be ready
 */
void wait(joiner_base * j)
{
    //bsg_print_hexadecimal((unsigned)j);
    scheduler_loop([j](){
        return j->joined();
    });
    //bsg_print_hexadecimal((unsigned)j | 0x10000000);    
}
}
