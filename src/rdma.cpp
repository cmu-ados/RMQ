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
  qp_info->rank = ntohl(qp_info_buf.rank);

  //qp_info->lid = qp_info_buf.lid;
  //qp_info->qp_num = qp_info_buf.qp_num;
  //qp_info->rank = qp_info_buf.rank;
  return 0;
}
int set_qp_info(int fd, qp_info_t *qp_info) {
  int n;
  qp_info_t qp_info_buf;
  qp_info_buf.lid = htons(qp_info->lid);
  qp_info_buf.qp_num = htonl(qp_info->qp_num);
  qp_info_buf.rank = htonl(qp_info->rank);

  //qp_info_buf.lid = qp_info->lid;
  //qp_info_buf.qp_num = qp_info->qp_num;
  //qp_info_buf.rank = qp_info->rank;

  n = tcp_write(fd, (char *) &qp_info_buf, sizeof(qp_info_t));
  if (n != sizeof(qp_info_t)) {
    return -1;
  }
  return 0;
}
} // namespace zmq
