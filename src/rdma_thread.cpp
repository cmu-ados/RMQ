#include "precompiled.hpp"
#include "rdma_thread.hpp"
#include "rdma_poller.hpp"
#include "err.hpp"
#include <iostream>

zmq::rdma_thread_t::rdma_thread_t(zmq::ctx_t *ctx_, uint32_t tid_) :
  object_t(ctx_, tid_) {
  _poller = new zmq::rdma_poller_t(*ctx_);
}

zmq::rdma_thread_t::~rdma_thread_t() {
  zmq_assert(_poller);
  delete _poller;
}

void zmq::rdma_thread_t::start() {
  //  Start the underlying I/O thread.
  _poller->start();
}

void zmq::rdma_thread_t::stop() {
  _poller->stop();
}

void zmq::rdma_thread_t::in_event() {
  command_t cmd;
  int rc = _mailbox.recv(&cmd, 0);

  while (rc == 0 || errno == EINTR) {
    if (rc == 0)
      cmd.destination->process_command(cmd);
    rc = _mailbox.recv(&cmd, 0);
  }

  errno_assert (rc != 0 && errno == EAGAIN);
}

void zmq::rdma_thread_t::out_event() {
  zmq_assert (false);
}

void zmq::rdma_thread_t::timer_event(int) {
  zmq_assert (false);
}

zmq::rdma_poller_t* zmq::rdma_thread_t::get_poller() {
  zmq_assert(_poller);
  return _poller;
}

void zmq::rdma_thread_t::process_stop() {
  zmq_assert (_mailbox_handle);
  // TODO what's the expected behavior of rdma_poller here?
  _poller->stop();
}
