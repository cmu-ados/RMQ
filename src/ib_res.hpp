//
// Created by Minghua Deng on 4/10/19.
//

#ifndef ZEROMQ_IB_RES_HPP_INCLUDED__
#define ZEROMQ_IB_RES_HPP_INCLUDED__

#include <cstdio>
#include <infiniband/verbs.h>

namespace zmq
{
  // structure for IB resources
  struct ib_res_t {
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp **qp;
    struct ibv_srq *srq;
    struct ibv_port_attr port_attr;
    struct ibv_device_attr dev_attr;

    int num_qps;
    char *ib_buf;
    size_t ib_buf_size;
  };

  int setup_ib(ib_res_t & ib_res, int max_ib_qps, int max_msg_size);
  void close_ib(ib_res_t & ib_res);
}

#endif //ZEROMQ_IB_RES_HPP_INCLUDED__
