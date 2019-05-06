#include "precompiled.hpp"
#include "rdma_poller.hpp"
#include "rdma.hpp"
#include "rdma_engine.hpp"
#include <iostream>

zmq::rdma_poller_t::rdma_poller_t(zmq::ctx_t &ctx):
  worker_poller_base_t(ctx),
  _ib_res(ctx._ib_res) {
  std::cout << "rdma_poller instantiated" << std::endl;
}

zmq::rdma_poller_t::~rdma_poller_t() {
  std::cout << "rdma_poller destructed" << std::endl;
  stop_worker();
}

#define RDMA_POLL_N 100

void zmq::rdma_poller_t::loop() {
  std::cout << "entering rdma_poller event loop" << std::endl;
  char *bufs[RDMA_POLL_N];
  uint32_t lens[RDMA_POLL_N];
  int qps[RDMA_POLL_N]; // qp_nums

  while (true) {
    // TODO fuck the real shit here
    scoped_lock_t engine_lock(_ib_res._engine_mapping_sync);
    _ib_res.ib_post_recv(IB_MTU);
    int n_polled = _ib_res.ib_poll_n(RDMA_POLL_N, qps, bufs, lens);
    for (int i = 0; i < n_polled; ++i) {
      int qp_id = _ib_res._qp_num_mapping[qps[i]];
      rdma_engine_t *engine = _ib_res._engine_mapping[qp_id];
      engine->rdma_push_msg(bufs[i], lens[i]);
      engine->rdma_notify();
    }
  }
}

void zmq::rdma_poller_t::stop() {
}

