//
// Created by Minghua Deng on 4/10/19.
//

#include "ib_res.hpp"
#include "ib_conf.hpp"

#include <cstring>
#include <cassert>
#include <malloc.h>

#define IB_PORT 1

namespace zmq {
	int setup_ib(ib_res_t &ib_res, int max_ib_qps, int max_msg_size) {
		ibv_device **dev_list = NULL;
		memset(&ib_res, 0, sizeof(ib_res_t));

		ib_res.num_qps = MAX_IB_QPS;

		dev_list =ibv_get_device_list(NULL);
		assert(dev_list != NULL);

		ib_res.ctx = ibv_open_device(*dev_list);
		assert(ib_res.ctx != NULL);

		ib_res.pd = ibv_alloc_pd(ib_res.ctx);
		assert(ib_res.pd != NULL);

		int ret = ibv_query_port(ib_res.ctx, IB_PORT, &ib_res.port_attr);
		assert(ret == 0);

		ib_res.ib_buf_size = max_ib_qps * MAX_CONCURRENT_MSG * max_msg_size;
		posix_memalign((void**)ib_res.ib_buf, 4096, ib_res.ib_buf_size);
		assert(ib_res.ib_buf != NULL);

		ib_res.mr = ibv_reg_mr (ib_res.pd, (void*)ib_res.ib_buf,
				ib_res.ib_buf_size,
				IBV_ACCESS_LOCAL_WRITE|
				IBV_ACCESS_REMOTE_READ|
				IBV_ACCESS_REMOTE_WRITE);
		assert(ib_res.mr != NULL);

		ret = ibv_query_device(ib_res.ctx, &ib_res.dev_attr);
		assert(ret == 0);

		ib_res.cq = ibv_create_cq(ib_res.ctx, ib_res.dev_attr.max_cqe,
				NULL, NULL, 0);
		assert(ib_res.cq != NULL);

		struct ibv_srq_init_attr srq_init_attr;

		srq_init_attr.attr.max_wr = ib_res.dev_attr.max_srq_wr;
		srq_init_attr.attr.max_sge = 1;


		ib_res.srq = ibv_create_srq(ib_res.pd, &srq_init_attr);

		struct ibv_qp_init_attr qp_init_attr;

		qp_init_attr.send_cq = ib_res.cq;
		qp_init_attr.recv_cq = ib_res.cq;
		qp_init_attr.srq = ib_res.srq;
		qp_init_attr.cap.max_send_wr = ib_res.dev_attr.max_qp_wr;
		qp_init_attr.cap.max_recv_wr = ib_res.dev_attr.max_qp_wr;
		qp_init_attr.cap.max_send_sge = 1;
		qp_init_attr.cap.max_recv_sge = 1;
		qp_init_attr.qp_type = IBV_QPT_RC;

		ib_res.qp = (struct ibv_qp **) calloc (ib_res.num_qps,
				sizeof(struct ibv_qp *));
		assert(ib_res.qp != NULL);

		for (int i = 0; i < ib_res.num_qps; i++) {
			ib_res.qp[i] = ibv_create_qp (ib_res.pd, & qp_init_attr);
			assert(ib_res.qp[i] != NULL);
		}

		ibv_free_device_list (dev_list);
		return 0;
	}

	void close_ib(ib_res_t &ib_res) {
		if (ib_res.qp != NULL) {
			for (int i = 0; i < ib_res.num_qps; i++) {
				if (ib_res.qp[i] != NULL) {
					ibv_destroy_qp(ib_res.qp[i]);
				}
			}
			free(ib_res.qp);
		}

		if (ib_res.srq != NULL)
			ibv_destroy_srq(ib_res.srq);
		if (ib_res.cq != NULL) {
			ibv_destroy_cq (ib_res.cq);
		}
		if (ib_res.mr != NULL) {
			ibv_dereg_mr (ib_res.mr);
		}

		if (ib_res.pd != NULL) {
			ibv_dealloc_pd (ib_res.pd);
		}
		if (ib_res.ctx != NULL) {
			ibv_close_device (ib_res.ctx);
		}
		if (ib_res.ib_buf != NULL) {
			free (ib_res.ib_buf);
		}
	}
}
