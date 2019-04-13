#ifndef __ZEROMQ_IB_RES_HPP_INCLUDED__
#define __ZEROMQ_IB_RES_HPP_INCLUDED__

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
}

#endif // __ZEROMQ_IB_RES_HPP_INCLUDED__
