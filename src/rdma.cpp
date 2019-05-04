#include "rdma.hpp"

namespace zmq {

    int get_qp_info(int fd, qp_info_t *qp_info) {
      int n;
      qp_info_t qp_info_buf;
      n = tcp_read(fd, (char *) &qp_info_buf, sizeof(qp_info_t));
      if (n != sizeof(qp_info_t)) {
        return -1;
      }
      qp_info->lid = ntohs(qp_info_buf.lid);
      qp_info->qp_num = ntohl(qp_info_buf.qp_num);
      return 0;
    }

    int set_qp_info(int fd, qp_info_t *qp_info) {
      int n;
      qp_info_t qp_info_buf;
      qp_info_buf.lid = htons(qp_info->lid);
      n = tcp_write(fd, (char *) &qp_info_buf, sizeof(qp_info_t));
      if (n != sizeof(qp_info_t)) {
        return -1;
      }
      return 0;
    }

    int set_qp_to_rts(ibv_qp *qp, uint32_t target_qp_num, uint16_t target_lid) {
      int ret = 0;

      {
        struct ibv_qp_attr qp_attr;
        qp_attr.qp_state = IBV_QPS_INIT;
        qp_attr.pkey_index = 0;
        qp_attr.port_num = IB_PORT;
        qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                                  IBV_ACCESS_REMOTE_READ |
                                  IBV_ACCESS_REMOTE_ATOMIC |
                                  IBV_ACCESS_REMOTE_WRITE;
        ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE |
                                          IBV_QP_PKEY_INDEX |
                                          IBV_QP_PORT |
                                          IBV_QP_ACCESS_FLAGS);
        assert(ret == 0);
      }

      {
        struct ibv_qp_attr qp_attr;
        qp_attr.qp_state = IBV_QPS_RTR;
        qp_attr.path_mtu = IB_MTU;
        qp_attr.dest_qp_num = target_qp_num;
        qp_attr.rq_psn = 0;
        qp_attr.max_dest_rd_atomic = 1;
        qp_attr.min_rnr_timer = 12,
                qp_attr.ah_attr.is_global = 0;
        qp_attr.ah_attr.dlid = target_lid;
        qp_attr.ah_attr.sl = IB_SL;
        qp_attr.ah_attr.src_path_bits = 0;
        qp_attr.ah_attr.port_num = IB_PORT;

        ret = ibv_modify_qp(qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_AV |
                            IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                            IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                            IBV_QP_MIN_RNR_TIMER);
        assert(ret == 0);
      }

      {
        struct ibv_qp_attr qp_attr;
        qp_attr.qp_state = IBV_QPS_RTS,
                qp_attr.timeout = 10;
        qp_attr.retry_cnt = 0;
        qp_attr.rnr_retry = 0;
        qp_attr.sq_psn = 0;
        qp_attr.max_rd_atomic = 1;

        ret = ibv_modify_qp(qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_TIMEOUT |
                            IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                            IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
        assert(ret == 0);
      }
      return 0;
    }

    int post_send(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                  struct ibv_qp *qp, char *buf) {

      struct ibv_send_wr *bad_send_wr;
      struct ibv_sge list;

      list.addr = (uintptr_t) buf;
      list.length = req_size;
      list.lkey = lkey;

      struct ibv_send_wr send_wr;
      send_wr.wr_id = wr_id;
      send_wr.sg_list = &list;
      send_wr.num_sge = 1;
      send_wr.opcode = IBV_WR_SEND;
      send_wr.send_flags = IBV_SEND_SIGNALED;

      int ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
      assert(ret == 0);
      return ret;
    }

    int post_srq_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                      struct ibv_srq *srq, char *buf) {
      assert(srq != nullptr);
      struct ibv_recv_wr *bad_recv_wr;

      struct ibv_sge list;

      list.addr = (uintptr_t) buf;
      list.length = req_size;
      list.lkey = lkey;

      struct ibv_recv_wr recv_wr;
      recv_wr.wr_id = wr_id;
      recv_wr.sg_list = &list;
      recv_wr.num_sge = 1;

      int ret = ibv_post_srq_recv(srq, &recv_wr, &bad_recv_wr);
      return ret;
    }
} // namespace zmq
