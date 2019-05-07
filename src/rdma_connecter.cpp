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

#include "precompiled.hpp"
#include <new>
#include <string>

#include "macros.hpp"
#include "rdma_connecter.hpp"
#include "stream_engine.hpp"
#include "io_thread.hpp"
#include "random.hpp"
#include "err.hpp"
#include "ip.hpp"
#include "tcp.hpp"
#include "address.hpp"
#include "rdma_address.hpp"
#include "session_base.hpp"
#include "rdma_engine.hpp"
#include <pthread.h>

#if !defined ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#ifdef ZMQ_HAVE_VXWORKS
#include <sockLib.h>
#endif
#ifdef ZMQ_HAVE_OPENVMS
#include <ioctl.h>
#endif
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

zmq::rdma_connecter_t::rdma_connecter_t(class io_thread_t *io_thread_,
                                        class session_base_t *session_,
                                        const options_t &options_,
                                        address_t *addr_,
                                        bool delayed_start_) :
    own_t(io_thread_, options_),
    io_object_t(io_thread_),
    _addr(addr_),
    _s(retired_fd),
    _handle(static_cast<handle_t> (NULL)),
    _delayed_start(delayed_start_),
    _connect_timer_started(false),
    _reconnect_timer_started(false),
    _session(session_),
    _current_reconnect_ivl(options.reconnect_ivl),
    _socket(_session->get_socket()) {
  zmq_assert (_addr);
  zmq_assert (_addr->protocol == protocol_name::rdma);
  _addr->to_string(_endpoint);
  // TODO the return value is unused! what if it fails? if this is impossible
  // or does not matter, change such that endpoint in initialized using an
  // initializer, and make endpoint const
}

zmq::rdma_connecter_t::~rdma_connecter_t() {
  zmq_assert (!_connect_timer_started);
  zmq_assert (!_reconnect_timer_started);
  zmq_assert (!_handle);
  zmq_assert (_s == retired_fd);
}

void zmq::rdma_connecter_t::process_plug() {
  if (_delayed_start)
    add_reconnect_timer();
  else
    start_connecting();
}

void zmq::rdma_connecter_t::process_term(int linger_) {
  if (_connect_timer_started) {
    cancel_timer(connect_timer_id);
    _connect_timer_started = false;
  }

  if (_reconnect_timer_started) {
    cancel_timer(reconnect_timer_id);
    _reconnect_timer_started = false;
  }

  if (_handle) {
    rm_handle();
  }

  if (_s != retired_fd)
    close();

  own_t::process_term(linger_);
}

void zmq::rdma_connecter_t::in_event() {
  //  We are not polling for incoming data, so we are actually called
  //  because of error here. However, we can get error on out event as well
  //  on some platforms, so we'll simply handle both events in the same way.
  out_event();
}

void zmq::rdma_connecter_t::out_event() {
  if (_connect_timer_started) {
    cancel_timer(connect_timer_id);
    _connect_timer_started = false;
  }

  rm_handle();

  const fd_t fd = connect();

  //  Handle the error condition by attempt to reconnect.
  if (fd == retired_fd || !tune_socket(fd)) {
    close();
    add_reconnect_timer();
    return;
  }

  int qp_id = get_ctx()->create_queue_pair();
  ibv_qp *qp = get_ctx()->get_qp(qp_id);
  ibv_context * ctx = get_ctx()->get_ib_res()._ctx;


  zmq_assert (qp != nullptr);
  qp_info_t local_qp_info, remote_qp_info;

  local_qp_info.lid = get_ctx()->get_ib_res()._port_attr.lid;
  local_qp_info.qp_num = qp->qp_num;

  int n1, n2;
  n2 = set_qp_info(fd, &local_qp_info);
  zmq_assert(n2 == 0);
  n1 = get_qp_info(fd, &remote_qp_info);
  zmq_assert(n1 == 0);

  printf("IB CONNECTOR: send: (%d, %d) %d %d\n",
         n1,
         n2,
         local_qp_info.lid,
         local_qp_info.qp_num);
  printf("IB CONNECTOR: recv: (%d, %d) %d %d\n",
         n1,
         n2,
         remote_qp_info.lid,
         remote_qp_info.qp_num);

  int ret = set_qp_to_rts(qp,
                          remote_qp_info.qp_num,
                          remote_qp_info.lid);
  assert(ret == 0);
  printf("\tqp[%d] <-> qp[%d]\n", qp->qp_num, remote_qp_info.qp_num);

  // FIXME: Test connection, delete it when finished
  /*for (int i = 0; i < 100; ++i) {
    get_ctx()->get_ib_res().ib_post_recv(sizeof("RDMATest"));
  }*/

  char buf[300] = {0};
  tcp_write(fd, "TCP sync", sizeof("TCP sync"));
  tcp_read(fd, buf, sizeof("TCP ack"));
  printf("RDMA CONNECTOR: RDMA Connected\n", buf);

  struct ibv_port_attr port_attr;
  int rc = ibv_query_port(ctx, IB_PORT, &port_attr);
  assert(port_attr.state == IBV_PORT_ACTIVE);

  /*
  // FIXME: Test connection, delete it when finished
  for (int i = 0; i < 1; ++i) {
    printf("\"RDMA CONNECTOR: Send QP_ID = %d\n", qp_id);
    char *testmsg = get_ctx()->get_ib_res().ib_reserve_send(qp_id, sizeof("RDMATest"));
    memcpy(testmsg, "RDMATest", sizeof("RDMATest"));
    get_ctx()->get_ib_res().ib_post_send(qp_id, testmsg, sizeof("RDMATest"));
  }


  // FIXME: Test connection, delete it when finished
  char *rcv_buf[1] = {nullptr};
  uint32_t length[1] = {0};
  int qps[1] = {0};
  for (int i = 0; i < 1; ++i) {
    do {
      rc = get_ctx()->get_ib_res().ib_poll_n(1, qps, rcv_buf, length);
    } while (rc == 0);
    printf("RDMA CONNECTOR: Message for qps %d received %s\n", qps[0], rcv_buf[0]);
  }
  */

  // get_ctx()->destroy_queue_pair(qp);

  //  Create the engine object for this connection.
  rdma_engine_t *engine =
      new(std::nothrow) rdma_engine_t(qp_id, options, _endpoint,&(get_ctx()->get_ib_res()));
  alloc_assert (engine);
  // Register Engine to ib_res
  get_ctx()->get_ib_res().add_engine(qp_id, engine);

  //  Attach the engine to the corresponding session object.
  send_attach(_session, engine);

  //  Shut the connecter down.
  terminate();
  _socket->event_connected(_endpoint, fd);
}

void zmq::rdma_connecter_t::rm_handle() {
  rm_fd(_handle);
  _handle = static_cast<handle_t> (NULL);
}

void zmq::rdma_connecter_t::timer_event(int id_) {
  zmq_assert (id_ == reconnect_timer_id || id_ == connect_timer_id);
  if (id_ == connect_timer_id) {
    _connect_timer_started = false;
    rm_handle();
    close();
    add_reconnect_timer();
  } else if (id_ == reconnect_timer_id) {
    _reconnect_timer_started = false;
    start_connecting();
  }
}

void zmq::rdma_connecter_t::start_connecting() {
  //  Open the connecting socket.
  const int rc = open();
  //  Connect may succeed in synchronous manner.
  if (rc == 0) {
    _handle = add_fd(_s);
    out_event();
  }

    //  Connection establishment may be delayed. Poll for its completion.
  else if (rc == -1 && errno == EINPROGRESS) {
    _handle = add_fd(_s);
    set_pollout(_handle);
    _socket->event_connect_delayed(_endpoint, zmq_errno());

    //  add userspace connect timeout
    add_connect_timer();
  }

    //  Handle any other error condition by eventual reconnect.
  else {
    if (_s != retired_fd)
      close();
    add_reconnect_timer();
  }
}

void zmq::rdma_connecter_t::add_connect_timer() {
  if (options.connect_timeout > 0) {
    add_timer(options.connect_timeout, connect_timer_id);
    _connect_timer_started = true;
  }
}

void zmq::rdma_connecter_t::add_reconnect_timer() {
  if (options.reconnect_ivl != -1) {
    const int interval = get_new_reconnect_ivl();
    add_timer(interval, reconnect_timer_id);
    _socket->event_connect_retried(_endpoint, interval);
    _reconnect_timer_started = true;
  }
}

int zmq::rdma_connecter_t::get_new_reconnect_ivl() {
  //  The new interval is the current interval + random value.
  const int interval =
      _current_reconnect_ivl + generate_random() % options.reconnect_ivl;

  //  Only change the current reconnect interval  if the maximum reconnect
  //  interval was set and if it's larger than the reconnect interval.
  if (options.reconnect_ivl_max > 0
      && options.reconnect_ivl_max > options.reconnect_ivl)
    //  Calculate the next interval
    _current_reconnect_ivl =
        std::min(_current_reconnect_ivl * 2, options.reconnect_ivl_max);
  return interval;
}

int zmq::rdma_connecter_t::open() {
  zmq_assert (_s == retired_fd);

  //  Resolve the address
  if (_addr->resolved.rdma_addr != NULL) {
    LIBZMQ_DELETE (_addr->resolved.rdma_addr);
  }

  _addr->resolved.rdma_addr = new(std::nothrow) rdma_address_t();
  alloc_assert (_addr->resolved.rdma_addr);
  int rc = _addr->resolved.rdma_addr->resolve(_addr->address.c_str(), false,
                                              options.ipv6);
  if (rc != 0) {
    LIBZMQ_DELETE (_addr->resolved.rdma_addr);
    return -1;
  }
  zmq_assert (_addr->resolved.rdma_addr != NULL);
  const rdma_address_t *const rdma_addr = _addr->resolved.rdma_addr;

  //  Create the socket.
  _s = open_socket(rdma_addr->family(), SOCK_STREAM, IPPROTO_TCP);

  //  IPv6 address family not supported, try automatic downgrade to IPv4.
  if (_s == zmq::retired_fd && rdma_addr->family() == AF_INET6
      && errno == EAFNOSUPPORT && options.ipv6) {
    rc = _addr->resolved.rdma_addr->resolve(_addr->address.c_str(), false,
                                            false);
    if (rc != 0) {
      LIBZMQ_DELETE (_addr->resolved.rdma_addr);
      return -1;
    }
    _s = open_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  }

  if (_s == retired_fd) {
    return -1;
  }

  //  On some systems, IPv4 mapping in IPv6 sockets is disabled by default.
  //  Switch it on in such cases.
  if (rdma_addr->family() == AF_INET6)
    enable_ipv4_mapping(_s);

  // Set the IP Type-Of-Service priority for this socket
  if (options.tos != 0)
    set_ip_type_of_service(_s, options.tos);

  // Bind the socket to a device if applicable
  if (!options.bound_device.empty())
    bind_to_device(_s, options.bound_device);

  // FIXME: The TCP socket is set to blocking!!!
  // Set the socket to non-blocking mode so that we get async connect().
  // unblock_socket(_s);

  // Set the socket to loopback fastpath if configured.
  if (options.loopback_fastpath)
    tcp_tune_loopback_fast_path(_s);

  //  Set the socket buffer limits for the underlying socket.
  if (options.sndbuf >= 0)
    set_tcp_send_buffer(_s, options.sndbuf);
  if (options.rcvbuf >= 0)
    set_tcp_receive_buffer(_s, options.rcvbuf);

  // Set the IP Type-Of-Service for the underlying socket
  if (options.tos != 0)
    set_ip_type_of_service(_s, options.tos);

  // Set a source address for conversations
  if (rdma_addr->has_src_addr()) {
    //  Allow reusing of the address, to connect to different servers
    //  using the same source port on the client.
    int flag = 1;
#ifdef ZMQ_HAVE_WINDOWS
    rc = setsockopt (_s, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *> (&flag), sizeof (int));
    wsa_assert (rc != SOCKET_ERROR);
#elif defined ZMQ_HAVE_VXWORKS
    rc = setsockopt (_s, SOL_SOCKET, SO_REUSEADDR, (char *) &flag,
                     sizeof (int));
    errno_assert (rc == 0);
#else
    rc = setsockopt(_s, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
    errno_assert (rc == 0);
#endif

#if defined ZMQ_HAVE_VXWORKS
    rc = ::bind (_s, (sockaddr *) rdma_addr->src_addr (),
                 rdma_addr->src_addrlen ());
#else
    rc = ::bind(_s, rdma_addr->src_addr(), rdma_addr->src_addrlen());
#endif
    if (rc == -1)
      return -1;
  }

  //  Connect to the remote peer.
#if defined ZMQ_HAVE_VXWORKS
  rc = ::connect (_s, (sockaddr *) tcp_addr->addr (), tcp_addr->addrlen ());
#else
  rc = ::connect(_s, rdma_addr->addr(), rdma_addr->addrlen());
#endif
  //  Connect was successful immediately.
  if (rc == 0) {
    return 0;
  }

  //  Translate error codes indicating asynchronous connect has been
  //  launched to a uniform EINPROGRESS.
#ifdef ZMQ_HAVE_WINDOWS
  const int last_error = WSAGetLastError ();
  if (last_error == WSAEINPROGRESS || last_error == WSAEWOULDBLOCK)
      errno = EINPROGRESS;
  else
      errno = wsa_error_to_errno (last_error);
#else
  if (errno == EINTR)
    errno = EINPROGRESS;
#endif
  return -1;
}

zmq::fd_t zmq::rdma_connecter_t::connect() {
  //  Async connect has finished. Check whether an error occurred
  int err = 0;
#if defined ZMQ_HAVE_HPUX || defined ZMQ_HAVE_VXWORKS
  int len = sizeof err;
#else
  socklen_t len = sizeof err;
#endif

  const int rc = getsockopt(_s, SOL_SOCKET, SO_ERROR,
                            reinterpret_cast<char *> (&err), &len);

  //  Assert if the error was caused by 0MQ bug.
  //  Networking problems are OK. No need to assert.
#ifdef ZMQ_HAVE_WINDOWS
  zmq_assert (rc == 0);
  if (err != 0) {
      if (err == WSAEBADF || err == WSAENOPROTOOPT || err == WSAENOTSOCK
          || err == WSAENOBUFS) {
          wsa_assert_no (err);
      }
      return retired_fd;
  }
#else
  //  Following code should handle both Berkeley-derived socket
  //  implementations and Solaris.
  if (rc == -1)
    err = errno;
  if (err != 0) {
    errno = err;
#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE
    errno_assert (errno != EBADF && errno != ENOPROTOOPT
                      && errno != ENOTSOCK && errno != ENOBUFS);
#else
    errno_assert (errno != ENOPROTOOPT && errno != ENOTSOCK
                  && errno != ENOBUFS);
#endif
    return retired_fd;
  }
#endif

  //  Return the newly connected socket.
  const fd_t result = _s;
  _s = retired_fd;
  return result;
}

bool zmq::rdma_connecter_t::tune_socket(const fd_t fd_) {
  const int rc = tune_tcp_socket(fd_)
      | tune_tcp_keepalives(
          fd_, options.tcp_keepalive, options.tcp_keepalive_cnt,
          options.tcp_keepalive_idle, options.tcp_keepalive_intvl)
      | tune_tcp_maxrt(fd_, options.tcp_maxrt);
  return rc == 0;
}

void zmq::rdma_connecter_t::close() {
  zmq_assert (_s != retired_fd);
#ifdef ZMQ_HAVE_WINDOWS
  const int rc = closesocket (_s);
  wsa_assert (rc != SOCKET_ERROR);
#else
  const int rc = ::close(_s);
  errno_assert (rc == 0);
#endif
  _socket->event_closed(_endpoint, _s);
  _s = retired_fd;
}
