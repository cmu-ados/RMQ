/*
    Copyright (c) 2007-2017 Contributors as noted in the AUTHORS file

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

#include "testutil.hpp"
#include "testutil_unity.hpp"

void setUp() {
  setup_test_context();
}

void tearDown() {
  teardown_test_context();
}

typedef void (*extra_func_t)(void *socket_);

#ifdef ZMQ_BUILD_DRAFT
void set_sockopt_fastpath (void *socket)
{
    int value = 1;
    int rc =
      zmq_setsockopt (socket, ZMQ_LOOPBACK_FASTPATH, &value, sizeof value);
    assert (rc == 0);
}
#endif

void test_pair_tcp(extra_func_t extra_func_ = NULL) {
  void *sb = test_context_socket(ZMQ_PAIR);

  if (extra_func_)
    extra_func_(sb);

  char my_endpoint[MAX_SOCKET_STRING];
  bind_loopback_ipv4(sb, my_endpoint, sizeof my_endpoint);

  void *sc = test_context_socket(ZMQ_PAIR);
  if (extra_func_)
    extra_func_(sc);

  TEST_ASSERT_SUCCESS_ERRNO (zmq_connect(sc, my_endpoint));

  bounce(sb, sc);

  test_context_socket_close(sc);
  test_context_socket_close(sb);
}

void test_pair_tcp_regular() {
  test_pair_tcp();
}

#ifdef ZMQ_BUILD_DRAFT
void test_pair_tcp_fastpath ()
{
    test_pair_tcp (set_sockopt_fastpath);
}
#endif

int main() {
  setup_test_environment();

  UNITY_BEGIN ();
  RUN_TEST (test_pair_tcp_regular);
#ifdef ZMQ_BUILD_DRAFT
  RUN_TEST (test_pair_tcp_fastpath);
#endif

  return UNITY_END ();
}
