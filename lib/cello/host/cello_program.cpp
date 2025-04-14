#include <cello/host/program.hpp>
#include <bsg_manycore_loader.h>
namespace cello
{
program::program() {}

int program::init(int argc, char **argv) {
    char *program = argv[1];
    bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    BSG_CUDA_CALL(hb_mc_device_init(&this->mc, "cello_program", HB_MC_DEVICE_ID));
    bsg_global_pointer::the_device = &this->mc;
    jobs_in.resize(this->mc.num_pods);
    jobs_out.resize(this->mc.num_pods);
    cfgs.resize(this->mc.num_pods);

    //hb_mc_dimension_t tg = mc.mc->config.pod_shape;
    hb_mc_pod_id_t pod_id;
    hb_mc_device_foreach_pod_id(&this->mc, pod_id)
    {
        BSG_CUDA_CALL(hb_mc_device_pod_program_init(&this->mc, pod_id, program));
    }
    return 0;
}

int program::input() {
    hb_mc_pod_id_t pod_id;
    bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    hb_mc_device_foreach_pod_id(&mc, pod_id)
    {
        hb_mc_coordinate_t pod = hb_mc_index_to_coordinate(pod_id, this->mc.mc->config.pods);

        this->cfgs[pod_id].dram_buffer_size() = 512*1024*1024;
        BSG_CUDA_CALL(hb_mc_device_pod_malloc(&this->mc, pod_id, this->cfgs[pod_id].dram_buffer_size(), &this->cfgs[pod_id].dram_buffer()));
        this->cfgs[pod_id].pod_x() = pod.x;
        this->cfgs[pod_id].pod_y() = pod.y;

        BSG_CUDA_CALL(hb_mc_device_pod_malloc(&this->mc, pod_id, sizeof(cello::config), &this->cfg_ptr));

        jobs_in[pod_id].push_back({cfg_ptr, (void*)&this->cfgs[pod_id], sizeof(cello::config)});
    }
    return 0;
}

int program::sync_input() {
    hb_mc_pod_id_t pod_id;
    bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    hb_mc_device_foreach_pod_id(&this->mc, pod_id)
    {
        BSG_CUDA_CALL(hb_mc_device_set_default_pod(&this->mc, pod_id));
        BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&this->mc, jobs_in[pod_id].data(), jobs_in[pod_id].size()));
        jobs_in[pod_id].clear();
    }    
    return 0;
}

int program::run() {
    hb_mc_pod_id_t pod_id;
    std::vector<uint32_t> argv = {cfg_ptr};
    bsg_pr_test_info("%s: enqueing setup\n", __PRETTY_FUNCTION__);
    hb_mc_device_foreach_pod_id(&this->mc, pod_id)
    {
        BSG_CUDA_CALL(hb_mc_device_pod_kernel_enqueue(&this->mc, pod_id,
                                                      {1,1}, this->tg,
                                                      "cello_setup", argv.size(), argv.data()));
    }
    bsg_pr_test_info("%s: executing setup\n", __PRETTY_FUNCTION__);
    BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&this->mc));
    bsg_pr_test_info("%s: enqueing main\n", __PRETTY_FUNCTION__);
    hb_mc_device_foreach_pod_id(&this->mc, pod_id)
    {
        BSG_CUDA_CALL(hb_mc_device_pod_kernel_enqueue(&this->mc, pod_id,
                                                      {1,1}, this->tg,
                                                      "cello_start", argv.size(), argv.data()));
    }
    bsg_pr_test_info("%s: executing main\n", __PRETTY_FUNCTION__);
    BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&this->mc));
    bsg_pr_test_info("%s: done\n", __PRETTY_FUNCTION__);
    return 0;
}

int program::output() {
  bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

int program::sync_output() {
    hb_mc_pod_id_t pod_id;
    bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    hb_mc_device_foreach_pod_id(&this->mc, pod_id)
    {
        BSG_CUDA_CALL(hb_mc_device_set_default_pod(&this->mc, pod_id));
        BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&this->mc, jobs_out[pod_id].data(), jobs_out[pod_id].size()));
        jobs_out[pod_id].clear();
    }
    return 0;
}

int program::check_output() {
  bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

int program::fini() {
    hb_mc_pod_id_t pod_id;
    bsg_pr_test_info("%s\n", __PRETTY_FUNCTION__);
    hb_mc_device_foreach_pod_id(&this->mc, pod_id)
    {
        BSG_CUDA_CALL(hb_mc_device_pod_program_finish(&this->mc, pod_id));
    }
    BSG_CUDA_CALL(hb_mc_device_finish(&this->mc));
    return 0;
}

hb_mc_eva_t program::find(const char*symbol) {
    hb_mc_program_t *prog = mc.pods[0].program;
    hb_mc_eva_t eva;
    BSG_CUDA_CALL(hb_mc_loader_symbol_to_eva(prog->bin, prog->bin_size, symbol, &eva));
    return eva;
}

}

/**
 * @brief create a new program
 */
__attribute__((weak))
cello::program * make_program() {
    return new cello::program;
}
