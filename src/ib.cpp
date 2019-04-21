#include "ib.hpp"

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

int set_qp_to_rts (ibv_qp *qp, uint32_t target_qp_num, uint16_t target_lid) {

}
} // namespace zmq
