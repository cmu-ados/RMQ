#ifndef __ZEROMQ_IB_RES_HPP_INCLUDED__
#define __ZEROMQ_IB_RES_HPP_INCLUDED__

#include <cstdio>
#include <infiniband/verbs.h>

namespace zmq {
// structure for IB resources
class ib_res_t {
 public:
  struct ibv_context *_ctx;
  struct ibv_pd *_pd;
  struct ibv_mr *_mr;
  struct ibv_cq *_cq;
  struct ibv_qp **_qp;
  struct ibv_srq *_srq;
  struct ibv_port_attr _port_attr;
  struct ibv_device_attr _dev_attr;
  int _num_qps;
  char *_ib_buf;
  size_t _ib_buf_size;

  mutex_t _ib_sync;
  ib_res_t() {
    memset(this, 0, sizeof(ib_res_t));
  }

  int setup(int num_qps, int buf_size) {
    ibv_device **dev_list = NULL;

    _num_qps = num_qps;

    dev_list = ibv_get_device_list(NULL);
    assert(dev_list != NULL);

    _ctx = ibv_open_device(*dev_list);
    assert(_ctx != NULL);

    _pd = ibv_alloc_pd(_ctx);
    assert(_pd != NULL);

    int ret = ibv_query_port(_ctx, 1, &_port_attr);
    assert(ret == 0);

    _ib_buf_size = buf_size;
    posix_memalign((void **) (&_ib_buf), 4096, _ib_buf_size);
    assert(_ib_buf != NULL);

    _mr = ibv_reg_mr(_pd, (void *) _ib_buf,
                     _ib_buf_size,
                     IBV_ACCESS_LOCAL_WRITE |
                         IBV_ACCESS_REMOTE_READ |
                         IBV_ACCESS_REMOTE_WRITE);
    assert(_mr != NULL);

    ret = ibv_query_device(_ctx, &_dev_attr);
    assert(ret == 0);

    _cq = ibv_create_cq(_ctx, _dev_attr.max_cqe,
                        NULL, NULL, 0);
    assert(_cq != NULL);

    struct ibv_srq_init_attr srq_init_attr;

    srq_init_attr.attr.max_wr = _dev_attr.max_srq_wr;
    srq_init_attr.attr.max_sge = 1;

    _srq = ibv_create_srq(_pd, &srq_init_attr);

    struct ibv_qp_init_attr qp_init_attr;

    qp_init_attr.send_cq = _cq;
    qp_init_attr.recv_cq = _cq;
    qp_init_attr.srq = _srq;
    qp_init_attr.cap.max_send_wr = _dev_attr.max_qp_wr;
    qp_init_attr.cap.max_recv_wr = _dev_attr.max_qp_wr;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.qp_type = IBV_QPT_RC;

    _qp = (struct ibv_qp **) calloc(_num_qps,
                                    sizeof(struct ibv_qp *));
    assert(_qp != NULL);

    for (int i = 0; i < _num_qps; i++) {
      _qp[i] = ibv_create_qp(_pd, &qp_init_attr);
      assert(_qp[i] != NULL);
    }

    ibv_free_device_list(dev_list);
    return 0;
  }
  void close() {
    if (_qp != NULL) {
      for (int i = 0; i < _num_qps; i++) {
        if (_qp[i] != NULL) {
          ibv_destroy_qp(_qp[i]);
        }
      }
      free(_qp);
    }

    if (_srq != NULL)
      ibv_destroy_srq(_srq);
    if (_cq != NULL) {
      ibv_destroy_cq(_cq);
    }
    if (_mr != NULL) {
      ibv_dereg_mr(_mr);
    }

    if (_pd != NULL) {
      ibv_dealloc_pd(_pd);
    }
    if (_ctx != NULL) {
      ibv_close_device(_ctx);
    }
    if (_ib_buf != NULL) {
      free(_ib_buf);
    }
  }
};
}

#endif // __ZEROMQ_IB_RES_HPP_INCLUDED__
