#ifndef __ZEROMQ_IB_RES_HPP_INCLUDED__
#define __ZEROMQ_IB_RES_HPP_INCLUDED__

#include <cstdio>
#include <vector>
#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <unordered_map>
#include "mutex.hpp"
#include "tcp.hpp"

namespace zmq {
class rdma_engine_t;

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htonll(x) ((1==htonl(1)) ? (x) : (((uint64_t)htonl((x) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
#define ntohll(x) ((1==ntohl(1)) ? (x) : (((uint64_t)ntohl((x) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll (uint64_t x) {return x; }
static inline uint64_t ntohll (uint64_t x) {return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

#define IB_PORT 1
#define IB_MTU  IBV_MTU_4096
#define IB_SL 0
#define IB_RECV_NUM 256

class qp_info_t {
public:
    uint16_t lid;
    uint32_t qp_num;
};

int get_qp_info(int fd, qp_info_t *qp_info);

int set_qp_info(int fd, qp_info_t *qp_info);

int set_qp_to_rts(ibv_qp *qp, uint32_t target_qp_num, uint16_t target_lid);

int post_send(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
        struct ibv_qp *qp, char *buf);

int post_srq_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
        struct ibv_srq *srq, char *buf);


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
  /* receive buffer, occupying the second half of the buffer */
  char *_rcv_buf_base;
  int _rcv_buf_offset;
  /* send buffer, occupying the first half of the buffer */
  char **_send_buf_base;
  int * _send_buf_offset;


  //  List of unused thread queue pairs
  typedef std::vector<int> unused_qps_t;
  unused_qps_t _unused_qps;

  // mapping from qpid to engine ptr
  mutex_t _engine_mapping_sync;
  std::unordered_map<int, rdma_engine_t *> _engine_mapping;

  // FIXME what if a qp_id is deleted and then reinserted?
  //       the poller might put outdated messages into the new engine
  void add_engine(int id, rdma_engine_t *engine) {
    scoped_lock_t lock(_engine_mapping_sync);
    _engine_mapping[id] = engine;
  }

  void remove_engine(int id) {
    scoped_lock_t lock(_engine_mapping_sync);
    _engine_mapping.erase(id);
  }

  mutex_t _ib_sync;
  bool _initalized;

  ib_res_t()
      : _ctx(nullptr),
        _pd(nullptr),
        _mr(nullptr),
        _cq(nullptr),
        _qp(nullptr),
        _srq(nullptr),
        _num_qps(0),
        _ib_buf(nullptr),
        _ib_buf_size(0),
        _rcv_buf_base(nullptr),
        _rcv_buf_offset(0),
        _send_buf_base(nullptr),
        _send_buf_offset(nullptr),
        _initalized(false) {
    memset(&_port_attr, 0, sizeof(ibv_port_attr));
    memset(&_dev_attr, 0, sizeof(ibv_device_attr));
  }

  char * ib_reserve_send(int qp_id, int size) {
    if (_send_buf_offset[qp_id] + size >= (_ib_buf_size / 2 / _num_qps))
      _send_buf_offset[qp_id] = size;
    else _send_buf_offset[qp_id] += size;
    return _send_buf_base[qp_id] + _send_buf_offset[qp_id] - size;
  }

  // Could be race here, should made send/recv critical section
  // Here buffer must owned by the qp
  int ib_post_send(int qp_id, char *buf, uint32_t size) {
    ibv_qp * qp = get_qp(qp_id);
    uint32_t req_size = size;
    uint32_t lkey = _mr->lkey;

    // Here wr_id is set to 0, change if needed
    struct ibv_send_wr *bad_send_wr;
    struct ibv_sge list;

    list.addr = (uintptr_t) buf;
    list.length = req_size;
    list.lkey = lkey;

    struct ibv_send_wr send_wr;
    send_wr.wr_id = 0;
    send_wr.sg_list = &list;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;

    int ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    assert(ret == 0);
    return ret;
  }


  int ib_post_recv(uint32_t size) {
    assert(_rcv_buf_base != nullptr);
    if (_rcv_buf_offset + size >= (_ib_buf_size / 2))
      _rcv_buf_offset = size;
    else _rcv_buf_offset += size;

    char * buf_pos = _rcv_buf_base + _rcv_buf_offset - size;
    printf ("_mr->lkey is %lld, _srq is %lld, buf_pos is %lld\n", _mr->lkey, &_srq, buf_pos);

    struct ibv_recv_wr *bad_recv_wr;

    struct ibv_sge list;

    list.addr = (uintptr_t) buf_pos;
    list.length = size;
    list.lkey = _mr->lkey;

    struct ibv_recv_wr recv_wr;
    recv_wr.wr_id = (uint64_t) buf_pos;
    recv_wr.sg_list = &list;
    recv_wr.num_sge = 1;

    int ret = ibv_post_srq_recv(_srq, &recv_wr, &bad_recv_wr);
    printf("1 Recv posted at %u \n", (uint64_t)buf_pos);
    return ret;

  }

  int ib_poll_n(int n, int* qps, char ** recv_bufs, uint32_t * length) {
    int buf_index = 0;
    ibv_cq * cq = _cq;
    struct ibv_wc wcs[IB_RECV_NUM];
    int num_wc = n;
    int n_got = ibv_poll_cq(cq, num_wc, wcs);
    for (int i = 0; i < n_got; i++) {
      if (wcs[i].status == IBV_WC_SUCCESS && wcs[i].opcode == IBV_WC_RECV) {
        recv_bufs[buf_index] = (char*)wcs[i].wr_id;
        length[buf_index] = wcs[i].byte_len;
        qps[buf_index] = wcs[i].qp_num;
        buf_index++;
        printf("1 Recv to %u completed\n", (unsigned)wcs[i].wr_id);
      }
      else printf("Bad Completion! %d %d\n", wcs[i].status, wcs[i].opcode);
    }
    return n_got;
  }

  int create_qp() {
    scoped_lock_t get_ib_sync(_ib_sync);
    assert(_initalized);
    if (_unused_qps.empty()) {
      return -1;
    }
    auto qp = _unused_qps.back();
    _unused_qps.pop_back();
    return qp;
  }

  ibv_qp* get_qp(int qp_id) {
    return _qp[qp_id];
  }



  void destroy_qp(int qp_id) {
    scoped_lock_t get_ib_sync(_ib_sync);
    assert(_initalized);
    bool flag = false;
    for (int i = 0; i < _num_qps; ++i) {
      if (_qp[i] == get_qp(qp_id)) {
        flag = true;
        break;
      }
    }
    assert(flag);
    _unused_qps.push_back(qp_id);
  }

  void setup(int num_qps, int buf_size) {
    scoped_lock_t get_ib_sync(_ib_sync);
    assert(!_initalized);

    ibv_device **dev_list = nullptr;

    _num_qps = num_qps;

    dev_list = ibv_get_device_list(nullptr);
    zmq_assert(dev_list != nullptr);

    _ctx = ibv_open_device(*dev_list);
    zmq_assert(_ctx != nullptr);

    _pd = ibv_alloc_pd(_ctx);
    zmq_assert(_pd != nullptr);

    int ret = ibv_query_port(_ctx, IB_PORT, &_port_attr);
    zmq_assert(ret == 0);

    _ib_buf_size = buf_size;
    posix_memalign((void **) (&_ib_buf), 4096, _ib_buf_size);
    _rcv_buf_base = _ib_buf + (_ib_buf_size / 2);

    _send_buf_base = (char**)malloc(_num_qps * sizeof(char*));
    _send_buf_offset = (int*) malloc(_num_qps * sizeof(int));

    int send_buf_size_per_qp = (_ib_buf_size / 2) / _num_qps;
    for (int i = 0; i < _num_qps; i++) {
      _send_buf_base[i] = _ib_buf + i * send_buf_size_per_qp;
      _send_buf_offset[i] = 0;
    }

    zmq_assert(_ib_buf != nullptr);

    _mr = ibv_reg_mr(_pd, (void *) _ib_buf,
                     _ib_buf_size,
                     IBV_ACCESS_LOCAL_WRITE |
                         IBV_ACCESS_REMOTE_READ |
                         IBV_ACCESS_REMOTE_WRITE);
    zmq_assert(_mr != nullptr);

    ret = ibv_query_device(_ctx, &_dev_attr);
    zmq_assert(ret == 0);

    _cq = ibv_create_cq(_ctx, _dev_attr.max_cqe,
                        nullptr, nullptr, 0);
    zmq_assert(_cq != nullptr);

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
    _unused_qps.reserve(_num_qps);
    zmq_assert(_qp != nullptr);

    for (int i = 0; i < _num_qps; i++) {
      _qp[i] = ibv_create_qp(_pd, &qp_init_attr);
      _unused_qps.push_back(i);
      zmq_assert(_qp[i] != nullptr);
    }
    ibv_free_device_list(dev_list);
    printf ("_mr->lkey is %lld, _srq is %lld, buf_pos is %lld\n", _mr->lkey, &_srq, _ib_buf);
    _initalized = true;
  }

  void close() {
    scoped_lock_t get_ib_sync(_ib_sync);
    if (_qp != nullptr) {
      for (int i = 0; i < _num_qps; i++) {
        if (_qp[i] != nullptr) {
          ibv_destroy_qp(_qp[i]);
        }
      }
      free(_qp);
    }

    if (_srq != nullptr)
      ibv_destroy_srq(_srq);
    if (_cq != nullptr) {
      ibv_destroy_cq(_cq);
    }
    if (_mr != nullptr) {
      ibv_dereg_mr(_mr);
    }

    if (_pd != nullptr) {
      ibv_dealloc_pd(_pd);
    }
    if (_ctx != nullptr) {
      ibv_close_device(_ctx);
    }
    if (_ib_buf != nullptr) {
      free(_ib_buf);
    }
  }

};

}

#endif // __ZEROMQ_IB_RES_HPP_INCLUDED__
