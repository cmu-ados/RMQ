#ifndef __ZMQ_IB_POLLER_HPP_INCLUDED__
#define __ZMQ_IB_POLLER_HPP_INCLUDED__

#include "poller_base.hpp"
#include "ib_mgr.hpp"

namespace zmq {

class ib_poller_t : public worker_poller_base_t {
 public:
  explicit ib_poller_t(const thread_ctx_t &ctx_): worker_poller_base_t(ctx_), ib_mgr_(nullptr) {}
  ~ib_poller_t() { stop_worker(); }

  void set_ib_mgr(ib_mgr_t *_ib_mgr) { this->ib_mgr_ = _ib_mgr; }

 private:
  void loop() {
    // TODO call polling events in ib_mgr
  }
 private:
  ib_mgr_t *ib_mgr_;
};

}


#endif
