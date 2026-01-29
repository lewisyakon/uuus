/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "Workload.hh"
#include "CSVWriter.hh"
#include "Layer.hh"
#include "astra-sim/system/MockNcclLog.h"
#include "astra-sim/system/AstraNetworkAPI.hh"
#include "astra-sim/system/RecvPacketEventHadndlerData.hh"

// ===== PP forward token (p2p) =====
namespace {
static const int PP_FWD_TAG_BASE = 600000000; // 避免与其他 tag 冲突
static const int PP_BWD_TAG_BASE = 700000000;

struct PPTokenRecvEHD : public AstraSim::RecvPacketEventHadndlerData {
  AstraSim::Workload* wl;
  PPTokenRecvEHD(AstraSim::Sys* node, AstraSim::Workload* w, int tag_id)
      : AstraSim::RecvPacketEventHadndlerData(
            node,
            AstraSim::EventType::General,
            AstraSim::ncclFlowTag(/*channel*/0, /*chunk*/-1, /*flow*/-1,
                                 /*child*/-1, /*sender*/-1, /*receiver*/-1,
                                 /*size*/0, /*pQps*/nullptr, /*tag*/tag_id,
                                 /*nvls*/false)),
        wl(w) {}
};

static void pp_send_done(void* fun_arg) {
  // send 完成不需要做任何事（也不需要释放）
  (void)fun_arg;
}

// stage>0：等待上一个 stage 的 token
static inline void post_pp_fwd_recv(AstraSim::Workload* wl, int token_id) {
  const int tp = wl->tp_size;
  const int pp = wl->pp_size;
  const int my_pp = wl->pp_rank;
  if (pp <= 1 || my_pp == 0) return;

  const int src = wl->generator->id - tp;           // 上一 stage、相同 tp_rank
  const int tag = PP_FWD_TAG_BASE + token_id;
  const uint64_t bytes = (wl->pp_commsize == 0) ? 1 : wl->pp_commsize;

  AstraSim::sim_request req;
  req.srcRank = src;
  req.dstRank = wl->generator->id;
  req.tag = tag;
  req.reqType = AstraSim::UINT8;
  req.reqCount = bytes;
  req.vnet = 0;
  req.layerNum = 0;
  req.flowTag = AstraSim::ncclFlowTag(0, -1, tag, -1, req.srcRank, req.dstRank, bytes, nullptr, tag, false);

  auto* arg = new PPTokenRecvEHD(wl->generator, wl, tag);

  wl->generator->sim_recv(
      0,
      AstraSim::Sys::dummy_data,
      bytes,
      AstraSim::UINT8,
      src,
      tag,
      &req,
      &AstraSim::Workload::pp_token_recv_done,
      arg);
}

// stage i：forward 完成后发 token 给 stage i+1
static inline void send_pp_fwd_token_to_next(AstraSim::Workload* wl, int token_id) {
  const int tp = wl->tp_size;
  const int pp = wl->pp_size;
  const int my_pp = wl->pp_rank;
  if (pp <= 1 || my_pp >= pp - 1) return;           // 最后一 stage 不发

  const int dst = wl->generator->id + tp;           // 下一 stage、相同 tp_rank
  const int tag = PP_FWD_TAG_BASE + token_id;
  const uint64_t bytes = (wl->pp_commsize == 0) ? 1 : wl->pp_commsize;

  AstraSim::sim_request req;
  req.srcRank = wl->generator->id;
  req.dstRank = dst;
  req.tag = tag;
  req.reqType = AstraSim::UINT8;
  req.reqCount = bytes;
  req.vnet = 0;
  req.layerNum = 0;
  req.flowTag = AstraSim::ncclFlowTag(0, -1, tag, -1, req.srcRank, req.dstRank, bytes, nullptr, tag, false);

  wl->generator->sim_send(
      0,
      AstraSim::Sys::dummy_data,
      bytes,
      AstraSim::UINT8,
      dst,
      tag,
      &req,
      &pp_send_done,
      nullptr);
}

static inline void post_pp_bwd_recv(AstraSim::Workload* wl, int token_id) {
  const int tp = wl->tp_size;
  const int pp = wl->pp_size;
  const int my_pp = wl->pp_rank;
  if (pp <= 1 || my_pp == pp - 1) return;     // 最后一个 stage 不需要等别人触发 backward

  const int src = wl->generator->id + tp;     // 下一 stage 给我发梯度 token
  const int tag = PP_BWD_TAG_BASE + token_id;
  const uint64_t bytes = (wl->pp_commsize == 0) ? 1 : wl->pp_commsize;

  AstraSim::sim_request req;
  req.srcRank = src;
  req.dstRank = wl->generator->id;
  req.tag = tag;
  req.reqType = AstraSim::UINT8;
  req.reqCount = bytes;
  req.vnet = 0;
  req.layerNum = 0;
  req.flowTag = AstraSim::ncclFlowTag(0, -1, tag, -1, req.srcRank, req.dstRank, bytes, nullptr, tag, false);

  auto* arg = new PPTokenRecvEHD(wl->generator, wl, tag);
  wl->generator->sim_recv(
      0, AstraSim::Sys::dummy_data, bytes, AstraSim::UINT8,
      src, tag, &req,
      &AstraSim::Workload::pp_token_recv_done, arg);
}

static inline void send_pp_bwd_token_to_prev(AstraSim::Workload* wl, int token_id) {
  const int tp = wl->tp_size;
  const int pp = wl->pp_size;
  const int my_pp = wl->pp_rank;
  if (pp <= 1 || my_pp == 0) return;          // stage0 没有前驱

  const int dst = wl->generator->id - tp;     // 前一 stage
  const int tag = PP_BWD_TAG_BASE + token_id;
  const uint64_t bytes = (wl->pp_commsize == 0) ? 1 : wl->pp_commsize;

  AstraSim::sim_request req;
  req.srcRank = wl->generator->id;
  req.dstRank = dst;
  req.tag = tag;
  req.reqType = AstraSim::UINT8;
  req.reqCount = bytes;
  req.vnet = 0;
  req.layerNum = 0;
  req.flowTag = AstraSim::ncclFlowTag(0, -1, tag, -1, req.srcRank, req.dstRank, bytes, nullptr, tag, false);

  wl->generator->sim_send(
      0, AstraSim::Sys::dummy_data, bytes, AstraSim::UINT8,
      dst, tag, &req,
      &pp_send_done, nullptr);
}

}

namespace AstraSim {
Workload::~Workload() {
  if (end_to_end != nullptr) {
    delete end_to_end;
  }
  if (detailed != nullptr) {
    delete detailed;
  }
  if (dimension_utilization != nullptr) {
    delete dimension_utilization;
  }
  for (int i = 0; i < SIZE; i++) {
    delete layers[i];
  }
  if (layers != nullptr) {
    delete[] layers;
  }
}
Workload::Workload(
    std::string run_name,
    Sys* generator,
    std::string name,
    int TOTAL_PASS,
    int total_rows,
    int stat_row,
    std::string path,
    bool seprate_log) {
  this->initialized = false;
  this->layers = nullptr;
  this->SIZE = 0;
  this->counter = 0;
  this->delay_loaded = false;
  this->checkpoint_initiated = false;
  this->collective_issued = false;
  this->current_state = LoopState::Forward_Pass;
  this->generator = generator;
  this->TOTAL_PASS = TOTAL_PASS;
  this->pass_counter = 0;
  this->index = 0;
  this->waiting_for_comm = 0;
  // PP local view defaults (computed in initialize_workload)
  this->global_layers = 0;
  this->tp_size = 1;
  this->pp_size = 1;
  this->dp_size = 1;
  this->tp_rank = 0;
  this->pp_rank = 0;
  this->dp_rank = 0;
  this->stage_start_layer = 0;
  this->stage_end_layer = -1;

  end_to_end = nullptr;
  detailed = nullptr;
  dimension_utilization = nullptr;
  this->path = path;
  this->stat_row = stat_row;
  this->seprate_log = seprate_log;
  this->initialized = initialize_workload(name);
  if (this->initialized == false) {
    return;
  }
  this->total_rows = total_rows;
  this->run_name = run_name;
  this->registered_for_finished_streams = false;
  #ifndef PHY_MTP
  if (generator->id == 0 && seprate_log) {
    /*ly delete*/
    /*
    std::cout << "stat path: " << path << " ,total rows: " << total_rows
              << " ,stat row: " << stat_row << std::endl;
    detailed = new CSVWriter(path, "detailed_"+std::to_string(generator->total_nodes)+".csv");
    end_to_end = new CSVWriter(path, "EndToEnd.csv");
    dimension_utilization =
        new CSVWriter(path, run_name + "_dimension_utilization_"+std::to_string(generator->npu_offset)+".csv");
    if (stat_row == 0) {
      initialize_stat_files();
    }
    */
  }
  #endif
}

void Workload::pp_token_recv_done(void* fun_arg) {
  auto* ehd = static_cast<PPTokenRecvEHD*>(fun_arg);
  Workload* wl = ehd->wl;
  delete ehd;

  wl->generator->register_event(wl, EventType::General, nullptr, 1);
}


void Workload::initialize_stat_files() {
  #ifdef NS3_MPI
  detailed->initialize_csv(SIZE * total_rows + 20, 50);
  #endif
  #ifdef NS3_MTP 
  detailed->initialize_csv(SIZE * total_rows + 20, 50);
  #endif
  end_to_end->initialize_csv(SIZE * total_rows + 20, 50);
}
void Workload::call(EventType event, CallData* data) {
  if (counter > 0) {
    if(generator->id == 0) std::cout << "counter > 0" << std::endl;
    generator->try_register_event(
        this, EventType::Workload_Wait, NULL, counter);
    return;
  }
  if (parallelismPolicy == ParallelismPolicy::Data) {
    iterate_data_parallel();
  } else if (parallelismPolicy == ParallelismPolicy::Transformer) {
    iterate_hybrid_parallel_Transformer();
  } else if (
      parallelismPolicy == ParallelismPolicy::DLRM ||
      parallelismPolicy == ParallelismPolicy::DLRMEnhanced) {
    iterate_hybrid_parallel_DLRM();
  } else if (parallelismPolicy == ParallelismPolicy::MicroBenchmark) {
    iterate_micro_benchmark();
  } else if (parallelismPolicy == ParallelismPolicy::Model) {
    iterate_model_parallel();
  } else if (parallelismPolicy == ParallelismPolicy::HybridDataModel) {
    iterate_hybrid_parallel_data_model();
  } else if (parallelismPolicy == ParallelismPolicy::HybridModelData) {
    iterate_hybrid_parallel_model_data();
  } else if (parallelismPolicy == ParallelismPolicy::DistributedInference) {
    iterate_distributed_inference();
  } else if (parallelismPolicy == ParallelismPolicy::TransformerFwdInBckwd) {
    iterate_hybrid_parallel_Transformer_fwd_in_bckwd();
  } else if (parallelismPolicy == ParallelismPolicy::HybridCustomized) {
    iterate_hybrid_parallel_customized();
  } else {
    Sys::sys_panic("No known parallelism!");
  }
}
void Workload::report() {
  double total_compute = 0;
  double total_exposed = 0;
  // #ifdef ANALYTI
  double pre_bubble_time = 0;
  double DP_comm = 0;
  double DP_EP_comm = 0;
  double Expose_TP_comm = 0;
  double Expose_EP_comm = 0;
  // #endif
  std::vector<double> total_fwd_time = {0, 0, 0};
  std::vector<double> total_wg_time = {0, 0, 0};
  std::vector<double> total_ig_time = {0, 0, 0};
  AstraSimDataAPI astraSimDataAPI;
  astraSimDataAPI.run_name = run_name;
  astraSimDataAPI.workload_finished_time = ((double)Sys::boostedTick()) / FREQ;
  std::cout<<"workload stats for the job scheduled at NPU offset: "
            <<generator->npu_offset<<std::endl;
  for (int i = 0; i < SIZE; i++) {
    #ifdef ANALYTI
    astraSimDataAPI.layers_stats.push_back(layers[i]->report(
        run_name,
        i,
        total_rows,
        stat_row,
        detailed,
        end_to_end,
        total_compute,
        total_exposed,
        pre_bubble_time,
        DP_comm,
        DP_EP_comm,
        Expose_TP_comm,
        Expose_EP_comm,
        this->seprate_log));
    #else
    astraSimDataAPI.layers_stats.push_back(layers[i]->report(
        run_name,
        i,
        total_rows,
        stat_row,
        detailed,
        end_to_end,
        total_compute,
        total_exposed,
        this->seprate_log,
        total_fwd_time,
        total_wg_time,
        total_ig_time,
        pre_bubble_time,
        DP_comm,
        DP_EP_comm,
        Expose_TP_comm,
        Expose_EP_comm));
    #endif
  }
  astraSimDataAPI.total_compute = total_compute;
  astraSimDataAPI.total_exposed_comm = total_exposed;
  astraSimDataAPI.avg_chunk_latency_per_logical_dimension =
      generator->scheduler_unit->get_average_latency_per_dimension();
  for (auto& latency :
       astraSimDataAPI.avg_chunk_latency_per_logical_dimension) {
    latency /= FREQ;
  }
  std::cout << "*************************" << std::endl;
  std::cout << "all passes finished at time: " << Sys::boostedTick()
            << ", id of first layer: " << layers[0]->id << std::endl;
  generator->NI->pass_front_end_report(astraSimDataAPI);
  #ifdef NS3_MTP 
  if (this->seprate_log) {
    std::list<std::list<std::pair<uint64_t, double>>> dims;
    for (int i = 0; i < generator->scheduler_unit->usage.size(); i++) {
      dims.push_back(
          generator->scheduler_unit->usage[i].report_percentage(10000));
    }
    dimension_utilization->finalize_csv(dims);
  }
  #endif
  #ifdef NS3_MPI 
  if (this->seprate_log) {
    std::list<std::list<std::pair<uint64_t, double>>> dims;
    for (int i = 0; i < generator->scheduler_unit->usage.size(); i++) {
      dims.push_back(
          generator->scheduler_unit->usage[i].report_percentage(10000));
    }
    dimension_utilization->finalize_csv(dims);
  }
  #endif
}
void Workload::check_for_sim_end() {
  if (pass_counter == TOTAL_PASS) {
    current_state = LoopState::Wait_For_Sim_Finish;
    if (generator->streams_finished != generator->streams_injected &&
        registered_for_finished_streams == false) {
      generator->register_for_finished_stream(this);
      registered_for_finished_streams = true;
      layers[0]->is_weight_grad_comm_finished_blocking();
      return;
    }
    if (generator->streams_finished == generator->streams_injected) {
      #ifndef PHY_MTP
      if (generator->id == 0) {
        report();
      }
      #endif
      generator->workload_finished();
      return;
    }
  }
  return;
}
void Workload::iterate_micro_benchmark() {
  assert(index >= 0);
  assert(index < SIZE);
  if (current_state != LoopState::Wait_For_Sim_Finish) {
    for (pass_counter = 0; pass_counter < TOTAL_PASS; pass_counter++) {
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::None, CollectiveBarrier::Non_Blocking);
    }
  }
  check_for_sim_end();
}
void Workload::iterate_data_parallel() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    index++;
    delay_loaded = false;
    if (index >= SIZE) {
      current_state = LoopState::Weight_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    delay_loaded = false;
    layers[index]->issue_weight_grad_comm(
        SchedulingPolicy::None, CollectiveBarrier::Non_Blocking);
    if (index == 0) {
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    delay_loaded = false;
    index--;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_customized() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_data_model() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_model_data() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_distributed_inference() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      index = 0;
      pass_counter++;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_model_parallel() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      std::vector<bool> involved_dimensions{true, true, true};
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      std::vector<bool> involved_dimensions{true, true, true};
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_Transformer() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Blocking);
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_Transformer_fwd_in_bckwd() {
  MockNcclLog* NcclLog = MockNcclLog::getInstance();
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      if(layers[index]->fwd_pass_comm_size < 4096 && layers[index]->fwd_pass_comm_size >0){
        layers[index]->fwd_pass_comm_size = 4096;
      }
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      // ===== 这里代表：本 stage 的一次 forward pass 已完成（最后一层 forward comm 也完成了）=====
      // 用 pass_counter * MicroBatches + fwd_mb 作为 token_id（按“pass”级别触发下一 stage forward）
      if(pp_size > 1 && pp_rank < pp_size-1){
        send_pp_fwd_token_to_next(this, pass_counter * MicroBatches + fwd_mb);        
      }
      fwd_mb++;//推进下一个microbatch

      if(fwd_mb < MicroBatches){//fwd还没推进完
        index = 0;
        delay_loaded = false;
        collective_issued = false;
        checkpoint_initiated = false;
        
        if(pp_size > 1 && pp_rank > 0){
          post_pp_fwd_recv(this, pass_counter * MicroBatches + fwd_mb);
          return ;
        }
        generator->register_event(this, EventType::General, NULL, 1);
        return ;
      }
      
      bwd_mb = MicroBatches - 1;//进入bwd状态
      index = SIZE - 1;
      delay_loaded = false;
      collective_issued = false;
      checkpoint_initiated = false;
      current_state = LoopState::Input_Gradient;
      
      if(pp_size > 1 && pp_rank < pp_size - 1){//只有最后一个stage可以直接开始bwd
        post_pp_bwd_recv(this, pass_counter * MicroBatches + bwd_mb);
        return ;
      }
      generator->register_event(this, EventType::General, NULL, 1);
      return ;

    }
    // NcclLog->writeLog(NcclLogLevel::DEBUG,"workload::call fwd_pass register_event EventType::General ");
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      
      if(pp_size > 1 && pp_rank > 0){
       send_pp_bwd_token_to_prev(this, pass_counter * MicroBatches + bwd_mb);
      }

      if(bwd_mb > 0){
        bwd_mb--;
        index = SIZE - 1;
        delay_loaded = false;
        collective_issued = false;
        checkpoint_initiated = false;
        current_state = LoopState::Input_Gradient;
        
        if(pp_size > 1 && pp_rank < pp_size - 1){
          post_pp_bwd_recv(this, pass_counter * MicroBatches + bwd_mb);
          return ;
        }
        generator->register_event(this, EventType::General, NULL, 1);
        return ;
      }

       // ===== 本 pass 的所有 micro-batch backward 完成，pass 结束 =====
      if (generator->id == 0) {
      std::cout << "pass: " << pass_counter
                 << " finished at time: " << Sys::boostedTick() << std::endl;
      }

      pass_counter++;
      fwd_mb = 0;
      bwd_mb = MicroBatches - 1;
      index = 0;
      delay_loaded = false;
      collective_issued = false;
      checkpoint_initiated = false;
      current_state = LoopState::Forward_Pass;
      
      if(pp_rank == 0){
        check_for_sim_end();
      }

      if(pp_size > 1 && pp_rank > 0){
        post_pp_fwd_recv(this, pass_counter * MicroBatches + fwd_mb);
        return ;
      }
      generator->register_event(this, EventType::General, NULL, 1);
      return ;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (layers[index]->needs_fwd_in_bckwd_initiation && !checkpoint_initiated) {
      int tmp = index;
      while (!layers[index--]->is_checkpoint)
        ;
      index++;
      current_state = LoopState::Forward_In_BackPass;
      checkpoint_initiated = true;
      generator->register_event(this, EventType::General, NULL, 1);
      if (generator->id == 0) {
        std::cout << "***** info, initiating fwd_in_bkwd starting from layer:"
                  << index << " to layer: " << tmp
                  << " ,at time: " << Sys::boostedTick() << std::endl;
      }
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Blocking);
      return;
    }
    checkpoint_initiated = false;
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Forward_In_BackPass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (layers[index]->needs_fwd_in_bckwd_initiation) {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_DLRM() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued &&
        layers[index]->fwd_pass_comm_type == ComType::All_to_All) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::HIGHEST, CollectiveBarrier::Non_Blocking);

    } else if (index == DLRM_LAST_BOTTOM_LAYER) {
      if (!layers[0]->is_fwd_pass_comm_finished_blocking()) {
        return;
      }
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Weight_Gradient;
      index--;
    }
    if (generator->id == 0) {
      std::cout << "*************************layer changed to: " << index
                << std::endl;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::None, CollectiveBarrier::Non_Blocking);
    }
    if (parallelismPolicy == ParallelismPolicy::DLRM &&
        !layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    if (index == 0) {
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    delay_loaded = false;
    collective_issued = false;
    generator->register_event(this, EventType::General, NULL, 1);
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (index == DLRM_LAST_BOTTOM_LAYER + 1) {
      layers[0]->issue_input_grad_comm(
          SchedulingPolicy::HIGHEST, CollectiveBarrier::Non_Blocking);
    }
    index--;
    if (generator->id == 0) {
      std::cout << "*************************layer changed to: " << index
                << " in ig" << std::endl;
    }
    current_state = LoopState::Weight_Gradient;
    collective_issued = false;
    delay_loaded = false;
    generator->register_event(this, EventType::General, NULL, 1);
  }
}
int Workload::get_layer_numbers(std::string workload_input) {
  std::ifstream inFile;
  inFile.open("workload_inputs/" + workload_input);
  if (!inFile) {
    std::cerr << "Unable to open file: " << workload_input << std::endl;
    std::cerr << "This error is fatal. Please check your path and filename."
              << std::endl;
    exit(1);
  } else {
    std::cout << "Success in opening workload file" << std::endl;
  }
  std::string dummyLine;
  std::getline(inFile, dummyLine);
  int layers;
  inFile >> layers;
  inFile.close();
  return layers;
}
ParallelismPolicy Workload::decode_parallelsim(std::string parallelism) {
  if (parallelism == "DATA")
    return ParallelismPolicy::Data;
  else if (parallelism == "HYBRID_TRANSFORMER")
    return ParallelismPolicy::Transformer;
  else if (parallelism == "HYBRID_TRANSFORMER_FWD_IN_BCKWD")
    return ParallelismPolicy::TransformerFwdInBckwd;
  else if (parallelism == "HYBRID_DLRM")
    return ParallelismPolicy::DLRM;
  else if (parallelism == "HYBRID_DLRM_ENHANCED")
    return ParallelismPolicy ::DLRMEnhanced;
  else if (parallelism == "MODEL")
    return ParallelismPolicy::Model;
  else if (parallelism == "HYBRID_DATA_MODEL")
    return ParallelismPolicy::HybridDataModel;
  else if (parallelism == "HYBRID_MODEL_DATA")
    return ParallelismPolicy::HybridModelData;
  else if (parallelism == "HYBRID_CUSTOMIZED")
    return ParallelismPolicy::HybridCustomized;
  else if (parallelism == "MICRO")
    return ParallelismPolicy::MicroBenchmark;
  else if (parallelism == "DISTRIBUTED_INFERENCE")
    return ParallelismPolicy::DistributedInference;
  else
    return ParallelismPolicy::None;
}
std::map<std::string, std::vector<bool>> Workload::decode_involved_dimensions(
    ParallelismPolicy policy,
    int model_parallel_npu_group) {
  std::map<std::string, std::vector<bool>> result;
  std::vector<bool> none{
      false, false, false, false, false, false, false, false, false, false};
  std::vector<bool> all{
      true, true, true, true, true, true, true, true, true, true};
  if (policy == ParallelismPolicy::All) {
    result["fwd"] = all;
    result["ig"] = all;
    result["wg"] = all;
  } else if (
      policy == ParallelismPolicy::Data || policy == ParallelismPolicy::DLRM ||
      policy == ParallelismPolicy::DLRMEnhanced ||
      policy == ParallelismPolicy::MicroBenchmark) {
    result["fwd"] = none;
    result["ig"] = none;
    result["wg"] = all;
  } else if (
      policy == ParallelismPolicy::Model ||
      policy == ParallelismPolicy::DistributedInference) {
    result["fwd"] = all;
    result["ig"] = all;
    result["wg"] = none;
  } else if (policy == ParallelismPolicy::HybridModelData) {
    std::vector<bool> data{
        true, false, false, false, false, false, false, false, false, false};
    std::vector<bool> model{
        false, true, true, true, true, true, true, true, true, true};
    result["fwd"] = model;
    result["ig"] = model;
    result["wg"] = data;
  } else if (policy == ParallelismPolicy::HybridDataModel) {
    std::vector<bool> model{
        true, false, false, false, false, false, false, false, false, false};
    std::vector<bool> data{
        false, true, true, true, true, true, true, true, true, true};
    result["fwd"] = model;
    result["ig"] = model;
    result["wg"] = data;
  } else if (
      policy == ParallelismPolicy::TransformerFwdInBckwd ||
      policy == ParallelismPolicy::Transformer) {
    int model_parallel_boundary =
        generator->break_dimension(model_parallel_npu_group);
    std::vector<bool> model;
    std::vector<bool> data;
    for (int i = 0; i <= model_parallel_boundary; i++) {
      model.push_back(true);
      data.push_back(false);
    }
    for (int i = model_parallel_boundary + 1; i < 10; i++) {
      model.push_back(false);
      data.push_back(true);
    }
    result["fwd"] = model;
    result["ig"] = model;
    result["wg"] = data;
  }
  return result;
}
bool Workload::initialize_workload(std::string name) {
  std::map<int, bool> chekpoints;
  std::map<int, bool> need_checkpoint_initiation;
  std::ifstream inFile;
  inFile.open(name);
  if (!inFile) {
    std::cerr << "Unable to open file: " << name << std::endl;
    std::cerr << "######### Exiting because unable to open the workload input "
                 "file #########"
              << std::endl;
    std::cerr << "This error is fatal. Please check your path and filename."
              << std::endl;
    exit(1);
  } else {
    if (generator->id == 0) {
      std::cout << "Success in opening workload file" << std::endl;
    }
  }
 std::string firstline;
  std::getline(inFile,firstline);
  // std::cout << "First line is : '" << firstline << "'" << std::endl;
  std::istringstream iss(firstline);
  std:string token;
  std::vector<std::string> tokens;
  // bool findparallesimPolcy = false;
  
  while (iss >> token) {
        tokens.push_back(token);
        // std::cout << "Token is : '" << token << "'" << std::endl;
    }



  if(!tokens.empty()){
    parallelismPolicy = decode_parallelsim(tokens[0]);
  }

  if (parallelismPolicy == ParallelismPolicy::TransformerFwdInBckwd ||
      parallelismPolicy == ParallelismPolicy::Transformer) {
        for (size_t i = 1; i < tokens.size(); i = i+1){
          if(tokens[i]=="model_parallel_NPU_group:"){
            model_parallel_npu_group = std::stoi(tokens[i+1]);
            if (generator->id == 0) {
              std::cout <<"model_parallel_NPU_group is " << model_parallel_npu_group << std::endl;
            }
          }else if(tokens[i]=="ep:"){
            expert_parallel_npu_group = std::stoi(tokens[i+1]);
          }else if(tokens[i]== "pp:"){
            pipeline_model_parallelism = std::stoi(tokens[i+1]);
          }else if(tokens[i]=="vpp:"){
            vpp = std::stoi(tokens[i+1]);
          }else if(tokens[i]=="ga:"){
            GA = std::stoi(tokens[i+1]);
          }else if(tokens[i]=="all_gpus:"){
            all_gpus = std::stoi(tokens[i+1]);
          }
        }

        if(parallelismPolicy == ParallelismPolicy::TransformerFwdInBckwd){
          if (generator->id == 0) {
            std::cout << "checkpoints layers are: ";
          }
          for(size_t i = 1; i < tokens.size(); i = i+1){
            if(tokens[i]=="checkpoints:"){
              int account = std::stoi(tokens[i+1]);
              while(account-- >0){
                int j = 2;
                int layer = std::stoi(tokens[i+j]);
                chekpoints[layer] = true;
                if (generator->id == 0) {
                  std::cout << layer << ", ";
                }
                j++;
              }
                
            }else if(tokens[i]=="checkpoint_initiates:"){
                if (generator->id == 0) {
                  std::cout << std::endl;
                  std::cout << "layers initiating fwd_in_bckwd are: ";
                }
                int account = std::stoi(tokens[i+1]);
                while(account-- >0){
                  int j = 2;
                  int layer = std::stoi(tokens[i+j]);
                  need_checkpoint_initiation[layer] = true;
                  if (generator->id == 0) {
                    std::cout << layer << ", ";
                  }
                  j++;
                }
                if (generator->id == 0) {
                  std::cout << std::endl;
                }
              }
            }
          }
      }else if(parallelismPolicy == ParallelismPolicy::DLRM ||
                parallelismPolicy == ParallelismPolicy::DLRMEnhanced){
                  for (size_t i = 1; i < tokens.size(); i = i+1){
                    if(tokens[i]=="DLRM_LAST_BOTTOM_LAYER:"){
                      DLRM_LAST_BOTTOM_LAYER = std::stoi(tokens[i+1]);
                    }
                  }
                if (generator->id == 0) {
                  std::cout
                  << "****************** info: DLRM workload last bottom layer is: "
                  << DLRM_LAST_BOTTOM_LAYER << std::endl;
                }
        }else if (parallelismPolicy == ParallelismPolicy::None) {
          #ifndef PHY_MTP
          std::cerr << "######### Exiting because unable to decode the workload "
                 "parallelization strategy #########"
                  << std::endl;
          inFile.close();
          exit(1);
          #else
          parallelismPolicy = ParallelismPolicy::TransformerFwdInBckwd;
          #endif
  }
  std::map<std::string, std::vector<bool>> general_involved_dimensions =
      decode_involved_dimensions(parallelismPolicy, model_parallel_npu_group);
  pp_commsize = 0;
  for (size_t i = 1; i < tokens.size(); i = i+1){
    if(tokens[i]=="pp_comm"||tokens[i]=="pp_comm:"){
      pp_commsize = std::stoi(tokens[i+1]);
    }
  }
  pp_commsize /= GA;
  if (generator->id == 0) {
      std::cout <<"pp_commize:"<< pp_commsize << std::endl;
  }
  if(generator->id == 0){
    if (model_parallel_npu_group == 0 || expert_parallel_npu_group == 0 || pipeline_model_parallelism == 0 
        || vpp==0 || GA == 0 || all_gpus == 0 ||(pipeline_model_parallelism !=1 && pp_commsize ==0)||(pipeline_model_parallelism == 1 && pp_commsize !=0)){
          std::cerr << "*****Warining: Input workload format mismatch. It may cause simulation error. Pleased use the latest AICB to generate.*****" << std::endl;
      }
  }        
  run_type = tokens[0];
  std::string secondline;
  std::getline(inFile,secondline);

  int lines;
  // std::cout << "Second line content: '" << secondline << "'" << std::endl;
  lines = std::stoi(secondline);

  // ===== PP stage -> local layer range (balanced partition, no empty stage when pp_size <= lines) =====
  MicroBatches = GA;
  fwd_mb = 0;
  bwd_mb = MicroBatches-1;

  global_layers = lines;
  tp_size = (model_parallel_npu_group <= 0) ? 1 : model_parallel_npu_group;
  pp_size = (pipeline_model_parallelism <= 0) ? 1 : pipeline_model_parallelism;

  const int world_gpus = (!generator->all_gpus.empty()) ? generator->all_gpus[0]
                                                        : generator->total_nodes;

  const int denom = tp_size * pp_size;
  dp_size = (denom > 0) ? (world_gpus / denom) : 1;
  if (dp_size <= 0) dp_size = 1;

  // Rank layout: rank = dp*(pp*tp) + pp*tp + tp
  tp_rank = generator->id % tp_size;
  pp_rank = (generator->id / tp_size) % pp_size;
  dp_rank = generator->id / (tp_size * pp_size);

  // Balanced partition
  stage_start_layer = 0;
  stage_end_layer = lines - 1;

  if (pp_size > 1) {
    const int base = lines / pp_size;
    const int rem  = lines % pp_size;
    const int cnt  = base + ((pp_rank < rem) ? 1 : 0);
    stage_start_layer = pp_rank * base + std::min(pp_rank, rem);
    stage_end_layer   = stage_start_layer + cnt - 1;

    // cnt can be 0 only when pp_size > lines
    if (cnt <= 0) {
      // 这里不要继续跑 workload，因为后面所有 iterate_* 都假设 SIZE>0 且会访问 layers[0]
      std::cerr << "[FATAL] Empty PP stage detected: pp_rank=" << pp_rank
                << " pp_size=" << pp_size << " global_layers=" << lines
                << ". Either reduce pp_size or change partition strategy to allow empty stages safely."
                << std::endl;
      std::abort();
    }
  }

  const int local_layers = stage_end_layer - stage_start_layer + 1;
  SIZE = local_layers;
  layers = new Layer*[SIZE];
  int local_idx = 0;

  // if (generator->id == 0) {
  //   std::cout << "PP partition: tp_size=" << tp_size
  //             << " pp_size=" << pp_size
  //             << " dp_size=" << dp_size
  //             << " rank=" << generator->id
  //             << " (dp,pp,tp)=(" << dp_rank << "," << pp_rank << "," << tp_rank << ")"
  //             << " layer_range=[" << stage_start_layer << "," << stage_end_layer << "]"
  //             << " local_layers=" << SIZE
  //             << std::endl;
  // }
  std::cout<<"id: "<<generator->id<<" tp_size="<<tp_size<<" pp_size="<<pp_size<<" dp_size="<<dp_size<< 
      " (dp,pp,tp)=("<<dp_rank<<","<<pp_rank<<","<<tp_rank<<")"<<" layer_range=["<<stage_start_layer<<","
        <<stage_end_layer<<"]" << " local_layers="<<SIZE <<std::endl;

  // SIZE = lines;
  for (int i = 0; i < lines; i++) {
    std::string id;
    inFile >> id;
    int depen;
    inFile >> depen;

    Tick fp_compute_time;
    inFile >> fp_compute_time;
    std::string fp_comm_type_s;
    inFile >> fp_comm_type_s;
    uint64_t fp_comm_size;
    inFile >> fp_comm_size;

    Tick ig_compute_time;
    inFile >> ig_compute_time;
    std::string ig_comm_type_s;
    inFile >> ig_comm_type_s;
    uint64_t ig_comm_size;
    inFile >> ig_comm_size;

    Tick wg_compute_time;
    inFile >> wg_compute_time;
    std::string wg_comm_type_s;
    inFile >> wg_comm_type_s;
    uint64_t wg_comm_size;
    inFile >> wg_comm_size;
    Tick wg_update_time;
    inFile >> wg_update_time;

    const bool keep_layer = (i >= stage_start_layer && i <= stage_end_layer);
    if(!keep_layer){
      continue;
    }

    ParallelismPolicy specific_policy = ParallelismPolicy::None;
    std::map<std::string, std::vector<bool>> selected_involved_dimensions;
    ComType fp_type = ComType::None;
    ComType ig_type = ComType::None;
    ComType wg_type = ComType::None;
    MockNccl::GroupType fp_group_type = MockNccl::GroupType::NONE;
    MockNccl::GroupType ig_group_type = MockNccl::GroupType::NONE;
    MockNccl::GroupType wg_group_type = MockNccl::GroupType::NONE;
    if (wg_comm_type_s.substr(0,9) == "ALLREDUCE") {
      wg_type = ComType::All_Reduce;
      if(wg_comm_type_s == "ALLREDUCE"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLREDUCE_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLREDUCE_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,8) == "ALLTOALL") {
      wg_type = ComType::All_to_All;
      if(wg_comm_type_s == "ALLTOALL"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLTOALL_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLTOALL_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,17) == "ALLREDUCEALLTOALL") {
      wg_type = ComType::All_Reduce_All_to_All;
      if(wg_comm_type_s == "ALLREDUCEALLTOALL"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLREDUCEALLTOALL_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLREDUCEALLTOALL_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,9) == "ALLGATHER") {
      wg_type = ComType::All_Gather;
      if(wg_comm_type_s == "ALLGATHER"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLGATHER_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLGATHER_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,13) == "REDUCESCATTER") {
      wg_type = ComType::Reduce_Scatter;
      if(wg_comm_type_s == "REDUCESCATTER"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "REDUCESCATTER_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "REDUCESCATTER_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    }

    // generate flow model

    if (ig_comm_type_s.substr(0,9) == "ALLREDUCE") {
      ig_type = ComType::All_Reduce;
      if(ig_comm_type_s == "ALLREDUCE"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLREDUCE_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLREDUCE_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,8) == "ALLTOALL") {
      ig_type = ComType::All_to_All;
      if(ig_comm_type_s == "ALLTOALL"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLTOALL_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLTOALL_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,17) == "ALLREDUCEALLTOALL") {
      ig_type = ComType::All_Reduce_All_to_All;
      if(ig_comm_type_s == "ALLREDUCEALLTOALL"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLREDUCEALLTOALL_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLREDUCEALLTOALL_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,9) == "ALLGATHER") {
      ig_type = ComType::All_Gather;
      if(ig_comm_type_s == "ALLGATHER"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLGATHER_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLGATHER_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,13) == "REDUCESCATTER") {
      ig_type = ComType::Reduce_Scatter;
      if(ig_comm_type_s == "REDUCESCATTER"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "REDUCESCATTER_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "REDUCESCATTER_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    }

    if (fp_comm_type_s.substr(0,9) == "ALLREDUCE") {
      fp_type = ComType::All_Reduce;
      if(fp_comm_type_s == "ALLREDUCE"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLREDUCE_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLREDUCE_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,8) == "ALLTOALL") {
      fp_type = ComType::All_to_All;
      if(fp_comm_type_s == "ALLTOALL"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLTOALL_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLTOALL_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,17) == "ALLREDUCEALLTOALL") {
      fp_type = ComType::All_Reduce_All_to_All;
      if(fp_comm_type_s == "ALLREDUCEALLTOALL"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLREDUCEALLTOALL_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLREDUCEALLTOALL_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,9) == "ALLGATHER") {
      fp_type = ComType::All_Gather;
      if(fp_comm_type_s == "ALLGATHER"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLGATHER_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLGATHER_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,13) == "REDUCESCATTER") {
      fp_type = ComType::Reduce_Scatter;
      if(fp_comm_type_s == "REDUCESCATTER"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "REDUCESCATTER_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "REDUCESCATTER_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    }
    if (generator->id == 0) {
      std::cout << "id: " << id << " , depen: " << depen
                << " , wg_comp_time: " << wg_compute_time << std::endl;
    }
    if (parallelismPolicy == ParallelismPolicy::HybridCustomized) {
      std::string specific_parallelsim;
      inFile >> specific_parallelsim;
      specific_policy = decode_parallelsim(specific_parallelsim);
    }
    if ((parallelismPolicy == ParallelismPolicy::DLRM ||
         parallelismPolicy == ParallelismPolicy::DLRMEnhanced) &&
        i == 0) {
      specific_policy = ParallelismPolicy::All;
    }
    if (specific_policy != ParallelismPolicy::None) {
      selected_involved_dimensions =
          decode_involved_dimensions(specific_policy, model_parallel_npu_group);
    } else {
      selected_involved_dimensions = general_involved_dimensions;
    }
    Layer* l = new Layer(
        id,
        i,
        generator,
        this,
        fp_compute_time * generator->compute_scale,
        fp_type,
        fp_group_type,
        fp_comm_size * generator->comm_scale / GA,
        selected_involved_dimensions["fwd"],
        ig_compute_time * generator->compute_scale,
        ig_type,
        ig_group_type,
        ig_comm_size * generator->comm_scale / GA,
        selected_involved_dimensions["ig"],
        wg_compute_time * generator->compute_scale,
        wg_type,
        wg_group_type,
        wg_comm_size * generator->comm_scale / GA,
        selected_involved_dimensions["wg"],
        wg_update_time,
        specific_policy);
    if (chekpoints.find(i) != chekpoints.end()) {
      l->is_checkpoint = true;
    }
    if (need_checkpoint_initiation.find(i) !=
        need_checkpoint_initiation.end()) {
      l->needs_fwd_in_bckwd_initiation = true;
    }
    layers[local_idx++] = l;
  }
  SIZE = local_idx;
  if (generator->id == 0) {
    std::cout << "type: " << run_type << " ,num passes: " << TOTAL_PASS
              << " ,lines: " << lines
              << " compute scale: " << generator->compute_scale
              << " ,comm scale: " << generator->comm_scale << std::endl;
  }
  inFile.close();
  return true;
}
void Workload::fire() {
  if(pp_size > 1 && pp_rank > 0){
    post_pp_fwd_recv(this, 0);
    return ;
  }
  call(EventType::General, NULL);
}

} // namespace AstraSim
