/*
    Copyright (c) 2007-2016 Contributors as noted in the AUTHORS file

    This file is part of libzmq, the ZeroMQ core engine in C++.

    libzmq is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    As a special exception, the Contributors give you permission to link
    this library with independent modules to produce an executable,
    regardless of the license terms of these independent modules, and to
    copy and distribute the resulting executable under terms of your choice,
    provided that you also meet, for each linked independent module, the
    terms and conditions of the license of that module. An independent
    module is a module which is not derived from or based on this library.
    If you modify this library, you must extend this exception to your
    version of the library.

    libzmq is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __ZMQ_REAPER_HPP_INCLUDED__
#define __ZMQ_REAPER_HPP_INCLUDED__

#include "object.hpp"
#include "mailbox.hpp"
#include "poller.hpp"
#include "i_poll_events.hpp"

namespace zmq {
class ctx_t;
class socket_base_t;

class reaper_t : public object_t, public i_poll_events {
 public:
  reaper_t(zmq::ctx_t *ctx_, uint32_t tid_);
  ~reaper_t();

  mailbox_t *get_mailbox();

  void start();
  void stop();

  //  i_poll_events implementation.
  void in_event();
  void out_event();
  void timer_event(int id_);

 private:
  //  Command handlers.
  void process_stop();
  void process_reap(zmq::socket_base_t *socket_);
  void process_reaped();

  //  Reaper thread accesses incoming commands via this mailbox.
  mailbox_t _mailbox;

  //  Handle associated with mailbox' file descriptor.
  poller_t::handle_t _mailbox_handle;

  //  I/O multiplexing is performed using a poller object.
  poller_t *_poller;

  //  Number of sockets being reaped at the moment.
  int _sockets;

  //  If true, we were already asked to terminate.
  bool _terminating;

  reaper_t(const reaper_t &);
  const reaper_t &operator=(const reaper_t &);

#ifdef HAVE_FORK
  // the process that created this context. Used to detect forking.
  pid_t _pid;
#endif
};
}

#endif
