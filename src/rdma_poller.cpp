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

#define RDMA_POLL_N 5

void zmq::rdma_poller_t::loop() {
  std::cout << "entering rdma_poller event loop" << std::endl;
  int npoll = RDMA_POLL_N;
  char *bufs[IB_RECV_NUM];
  uint32_t lens[IB_RECV_NUM];
  int qps[IB_RECV_NUM]; // qp_nums

  while (true) {
    // TODO fuck the real shit here
    scoped_lock_t engine_lock(_ib_res._engine_mapping_sync);
    while(npoll-- > 0)
      _ib_res.ib_post_recv(in_batch_size);
    std::pair<int,int> pii = _ib_res.ib_poll_n(RDMA_POLL_N, qps, bufs, lens);
    int n_polled = pii.first;
    int n_succ = pii.second;
    for (int i = 0; i < n_succ; ++i) {
      int qp_id = _ib_res._qp_num_mapping[qps[i]];
      rdma_engine_t *engine = _ib_res._engine_mapping[qp_id];
      //engine->rdma_push_msg(bufs[i], lens[i]);
      //engine->rdma_notify();
    }
    npoll += n_polled;
  }
}

void zmq::rdma_poller_t::stop() {
}

