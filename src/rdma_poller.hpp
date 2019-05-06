#ifndef __ZMQ_RDMA_POLLER_HPP_INCLUDED__
#define __ZMQ_RDMA_POLLER_HPP_INCLUDED__

#include "poller_base.hpp"

namespace zmq {

class rdma_poller_t: public worker_poller_base_t {
 public:
  rdma_poller_t(ctx_t &ctx);
  ~rdma_poller_t();

  void stop();

 private:
  void loop();
  ib_res_t &_ib_res;
};

}

#endif // __ZMQ_RDMA_POLLER_HPP_INCLUDED__
