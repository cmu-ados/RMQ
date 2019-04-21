#ifndef __ZMQ_RDMA_POLLER_HPP_INCLUDED__
#define __ZMQ_RDMA_POLLER_HPP_INCLUDED__

#include "poller_base.hpp"

namespace zmq {

class ib_poller_t : public worker_poller_base_t {
 public:
  explicit ib_poller_t(const thread_ctx_t &ctx_): worker_poller_base_t(ctx_) {}
  ~ib_poller_t() { stop_worker(); }
};

}


#endif
