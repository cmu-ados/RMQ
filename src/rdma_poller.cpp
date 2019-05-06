#include "precompiled.hpp"
#include "rdma_poller.hpp"
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

void zmq::rdma_poller_t::loop() {
  std::cout << "entering rdma_poller event loop" << std::endl;
  while (true) {
    // TODO fuck the real shit here

  }
}

void zmq::rdma_poller_t::stop() {
}

