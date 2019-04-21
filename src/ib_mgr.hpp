#ifndef __ZMQ_IB_MGR_HPP_INCLUDED__
#define __ZMQ_IB_MGR_HPP_INCLUDED__
#include <mutex>
#include <unordered_map>
#include "ib_engine.hpp"
#include "mutex.hpp"

namespace zmq {

class ib_mgr_t {
 public:
  ib_engine_t *get_engine(int16_t lid) {
    scoped_lock_t locker(_mapping_sync);
    return _mapping.at(lid);
  }

  void rm_connection(int16_t lid) {
    scoped_lock_t locker(_mapping_sync);
    _mapping.erase(lid);
  }

  void add_connection(int16_t lid, ib_engine_t *engine) {
    scoped_lock_t locker(_mapping_sync);
    _mapping[lid] = engine;
  }
 private:
  mutex_t _mapping_sync;
  std::unordered_map<int16_t, ib_engine_t *> _mapping;
};

}

#endif
