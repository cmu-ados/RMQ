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
#include <stdio.h>

#include "rdma_listener.hpp"
#include "stream_engine.hpp"
#include "io_thread.hpp"
#include "session_base.hpp"
#include "config.hpp"
#include "err.hpp"
#include "ip.hpp"
#include "tcp.hpp"
#include "socket_base.hpp"
#include "rdma_engine.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#ifdef ZMQ_HAVE_VXWORKS
#include <sockLib.h>
#endif
#endif

#ifdef ZMQ_HAVE_OPENVMS
#include <ioctl.h>
#endif

zmq::rdma_listener_t::rdma_listener_t(io_thread_t *io_thread_,
                                      socket_base_t *socket_,
                                      const options_t &options_) :
    own_t(io_thread_, options_),
    io_object_t(io_thread_),
    _s(retired_fd),
    _handle(static_cast<handle_t> (NULL)),
    _socket(socket_) {
}

zmq::rdma_listener_t::~rdma_listener_t() {
  zmq_assert (_s == retired_fd);
  zmq_assert (!_handle);
}

void zmq::rdma_listener_t::process_plug() {
  //  Start polling for incoming connections.
  _handle = add_fd(_s);
  set_pollin(_handle);
}

void zmq::rdma_listener_t::process_term(int linger_) {
  rm_fd(_handle);
  _handle = static_cast<handle_t> (NULL);
  close();
  own_t::process_term(linger_);
}

void zmq::rdma_listener_t::in_event() {
  fd_t fd = accept();

  //  If connection was reset by the peer in the meantime, just ignore it.
  //  TODO: Handle specific errors like ENFILE/EMFILE etc.
  if (fd == retired_fd) {
    _socket->event_accept_failed(_endpoint, zmq_errno());
    return;
  }

  int rc = tune_tcp_socket(fd);
  rc = rc
       | tune_tcp_keepalives(
          fd, options.tcp_keepalive, options.tcp_keepalive_cnt,
          options.tcp_keepalive_idle, options.tcp_keepalive_intvl);
  rc = rc | tune_tcp_maxrt(fd, options.tcp_maxrt);
  if (rc != 0) {
    _socket->event_accept_failed(_endpoint, zmq_errno());
    return;
  }

  // FIXME: Should change to rdma_engine_t
  // Create the engine object for this connection.
  int qp_id = get_ctx()->create_queue_pair();
  ibv_qp *qp = get_ctx()->get_qp(qp_id);
  zmq_assert (qp != nullptr);
  ibv_context *ctx = get_ctx()->get_ib_res()._ctx;

  qp_info_t local_qp_info, remote_qp_info;

  local_qp_info.lid = get_ctx()->get_ib_res()._port_attr.lid;
  local_qp_info.qp_num = qp->qp_num;

  int n1, n2;
  n1 = get_qp_info(fd, &remote_qp_info);
  zmq_assert(n1 == 0);
  n2 = set_qp_info(fd, &local_qp_info);
  zmq_assert(n2 == 0);
#ifdef _DEBUG
  printf("RDMA LISTENER: send: (%d, %d) %d %d\n",
         n1,
         n2,
         local_qp_info.lid,
         local_qp_info.qp_num);
  printf("RDMA LISTENER: recv: (%d, %d) %d %d\n",
         n1,
         n2,
         remote_qp_info.lid,
         remote_qp_info.qp_num);
#endif
  int ret = set_qp_to_rts(qp,
                          remote_qp_info.qp_num,
                          remote_qp_info.lid);
#ifdef _DEBUG
  printf("\tqp[%d] <-> qp[%d]\n",
         qp->qp_num, remote_qp_info.qp_num);
#endif

  // FIXME: Should pass qp_id into rdma_engine, use tcp fd for now
  rdma_engine_t *engine =
      new(std::nothrow) rdma_engine_t(qp_id, options, _endpoint, &(get_ctx()->get_ib_res()));
  alloc_assert (engine);

  // Register Engine to ib_res
  get_ctx()->get_ib_res().add_engine(qp_id, engine);

  // wait poller to post_recv

  // FIXME: Test connection, delete it when finished
  /*int N = 1;
  for(int i = 0; i < 1; ++i) {
    get_ctx()->get_ib_res().ib_post_recv(sizeof("RDMATest"));
  }*/

  char buf[300] = {0};
  tcp_read(fd, buf, sizeof("TCP sync"));
  tcp_write(fd, "TCP ack", sizeof("TCP ack"));
#ifdef _DEBUG
  printf("RDMA LISTENER: RDMA connected\n");
#endif

  struct ibv_port_attr port_attr;
  rc = ibv_query_port(ctx, IB_PORT, &port_attr);
  assert(port_attr.state == IBV_PORT_ACTIVE);

  /*
  // FIXME: Test connection, delete it when finished
  for(int i = 0; i < N; ++i) {
    printf("\"RDMA LISTENER: Send QP_ID = %d\n",qp_id);
    char * testmsg = get_ctx()->get_ib_res().ib_reserve_send(qp_id, sizeof("RDMATest"));
    memcpy(testmsg, "RDMATest", sizeof("RDMATest"));
    get_ctx()->get_ib_res().ib_post_send(qp_id, testmsg, sizeof("RDMATest"));
  }
  char *rcv_buf[1] = {nullptr};
  uint32_t length[1] = {0};
  int qps[1] = {0};
  for(int i = 0; i < N; ++i) {
    do {
      rc = get_ctx()->get_ib_res().ib_poll_n(1, qps, rcv_buf, length);
    } while(rc == 0);
    printf("RDMA LISTENER: Message for qps %d received %s\n", qps[0], rcv_buf[0]);
  }*/


  //get_ctx()->destroy_queue_pair(qp);

  //  Choose I/O thread to run connecter in. Given that we are already
  //  running in an I/O thread, there must be at least one available.
  io_thread_t *io_thread = choose_io_thread(options.affinity);
  zmq_assert (io_thread);

  //  Create and launch a session object.
  session_base_t *session =
      session_base_t::create(io_thread, false, _socket, options, NULL);
  errno_assert (session);
  session->inc_seqnum();
  launch_child(session);
  send_attach(session, engine, false);
  _socket->event_accepted(_endpoint, fd);
}

void zmq::rdma_listener_t::close() {
  zmq_assert (_s != retired_fd);
#ifdef ZMQ_HAVE_WINDOWS
  int rc = closesocket (_s);
  wsa_assert (rc != SOCKET_ERROR);
#else
  int rc = ::close(_s);
  errno_assert (rc == 0);
#endif
  _socket->event_closed(_endpoint, _s);
  _s = retired_fd;
}

int zmq::rdma_listener_t::get_address(std::string &addr_) {
  // Get the details of the TCP socket
  struct sockaddr_storage ss;
#if defined ZMQ_HAVE_HPUX || defined ZMQ_HAVE_VXWORKS
  int sl = sizeof (ss);
#else
  socklen_t sl = sizeof(ss);
#endif
  int rc = getsockname(_s, reinterpret_cast<struct sockaddr *> (&ss), &sl);

  if (rc != 0) {
    addr_.clear();
    return rc;
  }

  rdma_address_t addr(reinterpret_cast<struct sockaddr *> (&ss), sl);
  return addr.to_string(addr_);
}

int zmq::rdma_listener_t::set_address(const char *addr_) {
  //  Convert the textual address into address structure.
  int rc = _address.resolve(addr_, true, options.ipv6);
  if (rc != 0)
    return -1;

  _address.to_string(_endpoint);

  if (options.use_fd != -1) {
    _s = options.use_fd;
    _socket->event_listening(_endpoint, _s);
    return 0;
  }

  //  Create a listening socket.
  _s = open_socket(_address.family(), SOCK_STREAM, IPPROTO_TCP);

  //  IPv6 address family not supported, try automatic downgrade to IPv4.
  if (_s == zmq::retired_fd && _address.family() == AF_INET6
      && errno == EAFNOSUPPORT && options.ipv6) {
    rc = _address.resolve(addr_, true, false);
    if (rc != 0)
      return rc;
    _s = open_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  }

  if (_s == retired_fd) {
    return -1;
  }
  make_socket_noninheritable(_s);

  //  On some systems, IPv4 mapping in IPv6 sockets is disabled by default.
  //  Switch it on in such cases.
  if (_address.family() == AF_INET6)
    enable_ipv4_mapping(_s);

  // Set the IP Type-Of-Service for the underlying socket
  if (options.tos != 0)
    set_ip_type_of_service(_s, options.tos);

  // Set the socket to loopback fastpath if configured.
  if (options.loopback_fastpath)
    tcp_tune_loopback_fast_path(_s);

  // Bind the socket to a device if applicable
  if (!options.bound_device.empty())
    bind_to_device(_s, options.bound_device);

  //  Set the socket buffer limits for the underlying socket.
  if (options.sndbuf >= 0)
    set_tcp_send_buffer(_s, options.sndbuf);
  if (options.rcvbuf >= 0)
    set_tcp_receive_buffer(_s, options.rcvbuf);

  //  Allow reusing of the address.
  int flag = 1;
#ifdef ZMQ_HAVE_WINDOWS
  rc = setsockopt (_s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char *> (&flag), sizeof (int));
  wsa_assert (rc != SOCKET_ERROR);
#elif defined ZMQ_HAVE_VXWORKS
  rc =
    setsockopt (_s, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof (int));
  errno_assert (rc == 0);
#else
  rc = setsockopt(_s, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
  errno_assert (rc == 0);
#endif

  //  Bind the socket to the network interface and port.
#if defined ZMQ_HAVE_VXWORKS
  rc = bind (_s, (sockaddr *) _address.addr (), _address.addrlen ());
#else
  rc = bind(_s, _address.addr(), _address.addrlen());
#endif
#ifdef ZMQ_HAVE_WINDOWS
  if (rc == SOCKET_ERROR) {
      errno = wsa_error_to_errno (WSAGetLastError ());
      goto error;
  }
#else
  if (rc != 0)
    goto error;
#endif

  //  Listen for incoming connections.
  rc = listen(_s, options.backlog);
#ifdef ZMQ_HAVE_WINDOWS
  if (rc == SOCKET_ERROR) {
      errno = wsa_error_to_errno (WSAGetLastError ());
      goto error;
  }
#else
  if (rc != 0)
    goto error;
#endif

  _socket->event_listening(_endpoint, _s);
  return 0;

  error:
  int err = errno;
  close();
  errno = err;
  return -1;
}

zmq::fd_t zmq::rdma_listener_t::accept() {
  //  The situation where connection cannot be accepted due to insufficient
  //  resources is considered valid and treated by ignoring the connection.
  //  Accept one connection and deal with different failure modes.
  zmq_assert (_s != retired_fd);

  struct sockaddr_storage ss;
  memset(&ss, 0, sizeof(ss));
#if defined ZMQ_HAVE_HPUX || defined ZMQ_HAVE_VXWORKS
  int ss_len = sizeof (ss);
#else
  socklen_t ss_len = sizeof(ss);
#endif
#if defined ZMQ_HAVE_SOCK_CLOEXEC && defined HAVE_ACCEPT4
  fd_t sock = ::accept4 (_s, reinterpret_cast<struct sockaddr *> (&ss),
                         &ss_len, SOCK_CLOEXEC);
#else
  fd_t sock =
      ::accept(_s, reinterpret_cast<struct sockaddr *> (&ss), &ss_len);
#endif

  if (sock == retired_fd) {
#if defined ZMQ_HAVE_WINDOWS
    const int last_error = WSAGetLastError ();
    wsa_assert (last_error == WSAEWOULDBLOCK || last_error == WSAECONNRESET
                || last_error == WSAEMFILE || last_error == WSAENOBUFS);
#elif defined ZMQ_HAVE_ANDROID
    errno_assert (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
                  || errno == ECONNABORTED || errno == EPROTO
                  || errno == ENOBUFS || errno == ENOMEM || errno == EMFILE
                  || errno == ENFILE || errno == EINVAL);
#else
    errno_assert (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
                      || errno == ECONNABORTED || errno == EPROTO
                      || errno == ENOBUFS || errno == ENOMEM || errno == EMFILE
                      || errno == ENFILE);
#endif
    return retired_fd;
  }

  make_socket_noninheritable(sock);

  if (!options.tcp_accept_filters.empty()) {
    bool matched = false;
    for (options_t::tcp_accept_filters_t::size_type i = 0;
         i != options.tcp_accept_filters.size(); ++i) {
      if (options.tcp_accept_filters[i].match_address(
          reinterpret_cast<struct sockaddr *> (&ss), ss_len)) {
        matched = true;
        break;
      }
    }
    if (!matched) {
#ifdef ZMQ_HAVE_WINDOWS
      int rc = closesocket (sock);
      wsa_assert (rc != SOCKET_ERROR);
#else
      int rc = ::close(sock);
      errno_assert (rc == 0);
#endif
      return retired_fd;
    }
  }

  if (zmq::set_nosigpipe(sock)) {
#ifdef ZMQ_HAVE_WINDOWS
    int rc = closesocket (sock);
    wsa_assert (rc != SOCKET_ERROR);
#else
    int rc = ::close(sock);
    errno_assert (rc == 0);
#endif
    return retired_fd;
  }

  // Set the IP Type-Of-Service priority for this client socket
  if (options.tos != 0)
    set_ip_type_of_service(sock, options.tos);

  return sock;
}
