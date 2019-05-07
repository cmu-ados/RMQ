#ifndef __ZMQ_RDMA_POLLER_HPP_INCLUDED__
#define __ZMQ_RDMA_POLLER_HPP_INCLUDED__

#include "poller_base.hpp"

namespace zmq {

class rdma_poller_t: public poller_base_t {
 public:
  rdma_poller_t(ctx_t &ctx);
  ~rdma_poller_t();

  void start();
  void stop();

  static void worker_routine(void *arg_);
  void stop_worker();

  int get_load() const;

 private:

  std::atomic<bool> _running;
  void loop();
  ctx_t &_ctx;
  ib_res_t &_ib_res;
  //  Handle of the physical thread doing the I/O work.
  thread_t _worker;
};

}

#endif // __ZMQ_RDMA_POLLER_HPP_INCLUDED__
