// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#pragma once

#include <ostream>

#include "common/ceph_context.h"
#include "osd/scheduler/OpSchedulerItem.h"

namespace ceph::osd::scheduler {

using client = uint64_t;

/**
 * Base interface for classes responsible for choosing
 * op processing order in the OSD.
 */
class OpScheduler {
public:
  // Enqueue op for scheduling
  virtual void enqueue(OpSchedulerItem &&item) = 0;

  // Enqueue op for processing as though it were enqueued prior
  // to other items already scheduled.
  virtual void enqueue_front(OpSchedulerItem &&item) = 0;

  // Returns true iff there are no ops scheduled
  virtual bool empty() const = 0;

  // Return next op to be processed
  virtual OpSchedulerItem dequeue() = 0;

  // Dump formatted representation for the queue
  virtual void dump(ceph::Formatter &f) const = 0;

  // Print human readable brief description with relevant parameters
  virtual void print(std::ostream &out) const = 0;

  // Destructor
  virtual ~OpScheduler() {};
};

std::ostream &operator<<(std::ostream &lhs, const OpScheduler &);
using OpSchedulerRef = std::unique_ptr<OpScheduler>;

OpSchedulerRef make_scheduler(CephContext *cct);

/**
 * Implements OpScheduler in terms of OpQueue
 *
 * Templated on queue type to avoid dynamic dispatch, T should implement
 * OpQueue<OpSchedulerItem, client>.  This adapter is mainly responsible for
 * the boilerplate priority cutoff/strict concept which is needed for
 * OpQueue based implementations.
 */
template <typename T>
class ClassedOpQueueScheduler final : public OpScheduler {
  unsigned cutoff;
/** comment by hy 2020-04-07
 * # WeightedPriorityQueue
 */
  T queue;

  static unsigned int get_io_prio_cut(CephContext *cct) {
    if (cct->_conf->osd_op_queue_cut_off == "debug_random") {
      srand(time(NULL));
      return (rand() % 2 < 1) ? CEPH_MSG_PRIO_HIGH : CEPH_MSG_PRIO_LOW;
    } else if (cct->_conf->osd_op_queue_cut_off == "high") {
      return CEPH_MSG_PRIO_HIGH;
    } else {
      // default / catch-all is 'low'
      return CEPH_MSG_PRIO_LOW;
    }
  }
public:
  template <typename... Args>
  ClassedOpQueueScheduler(CephContext *cct, Args&&... args) :
    cutoff(get_io_prio_cut(cct)),
    queue(std::forward<Args>(args)...)
  {}

  void enqueue(OpSchedulerItem &&item) final {
    unsigned priority = item.get_priority();
    unsigned cost = item.get_cost();
/** comment by hy 2020-04-07
 * # 高优先级别限定
 */
    if (priority >= cutoff)
/** comment by hy 2020-04-07
 * # 在 pg 处理上 queue = WeightedPriorityQueue
     即 strict 队列
 */
      queue.enqueue_strict(
	item.get_owner(), priority, std::move(item));
    else
/** comment by hy 2020-04-07
 * # 普通优先级,其中的 owner = 消息的源头
     WeightedPriorityQueue::enqueue
     即 normal 队列
 */
      queue.enqueue(
	item.get_owner(), priority, cost, std::move(item));
  }

  void enqueue_front(OpSchedulerItem &&item) final {
    unsigned priority = item.get_priority();
    unsigned cost = item.get_cost();
    if (priority >= cutoff)
/** comment by hy 2020-04-07
 * # WeightedPriorityQueue::enqueue_strict_front
     即 strict 队列
 */
      queue.enqueue_strict_front(
	item.get_owner(),
	priority, std::move(item));
    else
/** comment by hy 2020-04-07
 * #  普通优先级,其中的 owner = 消息的源头
     WeightedPriorityQueue::enqueue_front
     即 normal 队列
 */
      queue.enqueue_front(
	item.get_owner(),
	priority, cost, std::move(item));
  }

  bool empty() const final {
    return queue.empty();
  }

  OpSchedulerItem dequeue() final {
/** comment by hy 2020-04-07
 * # WeightedPriorityQueue::dequeue
     先从高优先级 strict 队列
     后从低优先级 normal 队列
 */
    return queue.dequeue();
  }

  void dump(ceph::Formatter &f) const final {
/** comment by hy 2020-04-07
 * # WeightedPriorityQueue::dump
 */
    return queue.dump(&f);
  }

  void print(std::ostream &out) const final {
    out << "ClassedOpQueueScheduler(queue=";
    queue.print(out);
    out << ", cutoff=" << cutoff << ")";
  }

  ~ClassedOpQueueScheduler() final {};
};

}
