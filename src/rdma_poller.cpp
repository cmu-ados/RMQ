#include "precompiled.hpp"
#include "rdma_poller.hpp"
#include "rdma.hpp"
#include "rdma_engine.hpp"
#include "ctx.hpp"
#include <iostream>

zmq::rdma_poller_t::rdma_poller_t(zmq::ctx_t &ctx):
  poller_base_t(),
  _ctx(ctx),
  _ib_res(ctx._ib_res) {
  std::cout << "rdma_poller instantiated" << std::endl;
}

zmq::rdma_poller_t::~rdma_poller_t() {
  std::cout << "rdma_poller destructed" << std::endl;
  stop_worker();
}

void zmq::rdma_poller_t::start() {
  _ctx.start_thread(_worker, worker_routine, this);
}

void zmq::rdma_poller_t::worker_routine(void *arg_) {
  (static_cast<rdma_poller_t *> (arg_))->loop();
}

void zmq::rdma_poller_t::stop_worker() {
  _worker.stop();
}

int zmq::rdma_poller_t::get_load() const {
  return 1;
}

#define RDMA_POLL_N 1024

void zmq::rdma_poller_t::loop() {
  std::cout << "entering rdma_poller event loop" << std::endl;
  int npoll = RDMA_POLL_N;
  char *bufs[IB_RECV_NUM];
  uint32_t lens[IB_RECV_NUM];
  int qps[IB_RECV_NUM]; // qp_nums
  for(int i = 0; i < npoll; ++i)
    _ib_res.ib_post_recv(in_batch_size);
  sleep(5);
  while (true) {
    // TODO fuck the real shit here
    scoped_lock_t engine_lock(_ib_res._engine_mapping_sync);
    std::pair<int,int> pii = _ib_res.ib_poll_n(RDMA_POLL_N, qps, bufs, lens);
    int n_polled = pii.first;
    int n_succ = pii.second;
    for (int i = 0; i < n_succ; ++i) {
      if(_ib_res._qp_num_mapping.find(qps[i]) == _ib_res._qp_num_mapping.end()) {
          printf("rdma_poller_t::loop() engine not found %d\n",qps[i]);
          continue;
      }
      int qp_id = _ib_res._qp_num_mapping.at(qps[i]);
      rdma_engine_t *engine = _ib_res._engine_mapping.at(qp_id);
      printf("rdma_poller_t::loop() recv %llx %u\n",(long long)bufs[i],lens[i]);
      engine->rdma_push_msg(bufs[i], lens[i]);
      printf("rdma_poller_t::loop() notify %llx!\n",(long long) engine);
      engine->rdma_notify();
    }
    for(int i = 0; i < n_polled; ++i)
      _ib_res.ib_post_recv(in_batch_size);
  }
}

void zmq::rdma_poller_t::stop() {
}

