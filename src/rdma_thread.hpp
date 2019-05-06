#ifndef __ZMQ_RDMA_THREAD_HPP_INCLUDED__
#define __ZMQ_RDMA_THREAD_HPP_INCLUDED__

#include "stdint.hpp"
#include "object.hpp"
#include "poller.hpp"
#include "rdma_poller.hpp"
#include "mailbox.hpp"
#include "i_poll_events.hpp"


namespace zmq {
class ctx_t;

class rdma_thread_t: public object_t, public i_poll_events {
 public:
  rdma_thread_t(ctx_t *ctx_, uint32_t tid_);
  ~rdma_thread_t();

  void start();
  void stop();

  //  i_poll_events implementation.
  void in_event();
  void out_event();
  void timer_event(int id_);

  void process_stop();

  rdma_poller_t *get_poller();

 private:
  //  I/O thread accesses incoming commands via this mailbox.
  mailbox_t _mailbox;
  //  Handle associated with mailbox' file descriptor.
  poller_t::handle_t _mailbox_handle;

  // busy loop poller
  rdma_poller_t *_poller;

  rdma_thread_t(const rdma_thread_t &);
  const rdma_thread_t &operator=(const rdma_thread_t &);
};


}



#endif // __ZMQ_RDMA_THREAD_HPP_INCLUDED__
