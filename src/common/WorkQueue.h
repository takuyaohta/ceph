// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef __CEPH_WORKQUEUE
#define __CEPH_WORKQUEUE

#include "Mutex.h"
#include "Cond.h"
#include "Thread.h"

class WorkThreadPool {
  Mutex _lock;
  Cond _cond;
  bool _stop, _pause;
  Cond _wait_cond;

  struct _WorkQueue {
    string name;
    _WorkQueue(string n) : name(n) {}
    virtual bool _try_process() = 0;
    virtual void _clear() = 0;
  };  

public:
  template<class T>
  class WorkQueue : public _WorkQueue {
    WorkThreadPool *pool;
    
    virtual bool _enqueue(T *) = 0;
    virtual void _dequeue(T *) = 0;
    virtual T *_dequeue() = 0;
    virtual void _process(T *) = 0;
    virtual void _clear() = 0;
    
  public:
    WorkQueue(string n, WorkThreadPool *p) : _WorkQueue(n), pool(p) {
      pool->add_work_queue(this);
    }
    ~WorkQueue() {
      pool->remove_work_queue(this);
    }
    
    bool queue(T *item) {
      pool->_lock.Lock();
      bool r = _enqueue(item);
      pool->_cond.SignalOne();
      pool->_lock.Unlock();
      return r;
    }
    void dequeue(T *item) {
      pool->_lock.Lock();
      _dequeue(item);
      pool->_lock.Unlock();
    }
    void clear() {
      pool->_lock.Lock();
      _clear();
      pool->_lock.Unlock();
    }

    bool _try_process() {
      T *item = _dequeue();
      if (item) {
	pool->_lock.Unlock();
	_process(item);
	pool->_lock.Lock();
	return true;
      }
      return false;
    }

    void lock() {
      pool->lock();
    }
    void unlock() {
      pool->unlock();
    }
    void _kick() {
      pool->_kick();
    }

  };

private:
  vector<_WorkQueue*> work_queues;
  int last_work_queue;
 

  // threads
  struct WorkThread : public Thread {
    WorkThreadPool *pool;
    WorkThread(WorkThreadPool *p) : pool(p) {}
    void *entry() {
      pool->entry();
      return 0;
    }
  };
  
  set<WorkThread*> _threads;
  int processing;


  void entry() {
    _lock.Lock();
    //generic_dout(0) << "entry start" << dendl;
    while (!_stop) {
      if (!_pause && work_queues.size()) {
	_WorkQueue *wq;
	int tries = work_queues.size();
	bool did = false;
	while (tries--) {
	  last_work_queue++;
	  last_work_queue %= work_queues.size();
	  wq = work_queues[last_work_queue];
	  
	  processing++;
	  //generic_dout(0) << "entry trying wq " << wq->name << dendl;
	  did = wq->_try_process();
	  processing--;
	  //if (did) generic_dout(0) << "entry did wq " << wq->name << dendl;
	  if (did && _pause)
	    _wait_cond.Signal();
	  if (did)
	    break;
	}
	if (did)
	  continue;
      }
      //generic_dout(0) << "entry waiting" << dendl;
      _cond.Wait(_lock);
    }
    //generic_dout(0) << "entry finish" << dendl;
    _lock.Unlock();
  }

public:
  WorkThreadPool(string name, int n=1) :
    _lock((new string(name + "::lock"))->c_str()),  // deliberately leak this
    _stop(false),
    _pause(false),
    last_work_queue(0),
    processing(0) {
    set_num_threads(n);
  }
  ~WorkThreadPool() {
    for (set<WorkThread*>::iterator p = _threads.begin();
	 p != _threads.end();
	 p++)
      delete *p;
  }
  
  void add_work_queue(_WorkQueue *wq) {
    work_queues.push_back(wq);
  }
  void remove_work_queue(_WorkQueue *wq) {
    unsigned i = 0;
    while (work_queues[i] != wq)
      i++;
    for (i++; i < work_queues.size(); i++) 
      work_queues[i-1] = work_queues[i];
    assert(i == work_queues.size());
    work_queues.resize(i-1);
  }

  void set_num_threads(unsigned n) {
    while (_threads.size() < n) {
      WorkThread *t = new WorkThread(this);
      _threads.insert(t);
    }
  }

  void start() {
    for (set<WorkThread*>::iterator p = _threads.begin();
	 p != _threads.end();
	 p++)
      (*p)->create();
  }
  void stop(bool clear_after=true) {
    _lock.Lock();
    _stop = true;
    _cond.Signal();
    _lock.Unlock();
    for (set<WorkThread*>::iterator p = _threads.begin();
	 p != _threads.end();
	 p++)
      (*p)->join();
    _lock.Lock();
    for (unsigned i=0; i<work_queues.size(); i++)
      work_queues[i]->_clear();
    _lock.Unlock();    
  }
  void kick() {
    _lock.Lock();
    _cond.Signal();
    _lock.Unlock();
  }
  void _kick() {
    assert(_lock.is_locked());
    _cond.Signal();
  }
  void lock() {
    _lock.Lock();
  }
  void unlock() {
    _lock.Unlock();
  }

  void pause() {
    _lock.Lock();
    assert(!_pause);
    _pause = true;
    while (processing)
      _wait_cond.Wait(_lock);
    _lock.Unlock();
  }
  void pause_new() {
    _lock.Lock();
    assert(!_pause);
    _pause = true;
    _lock.Unlock();
  }

  void unpause() {
    _lock.Lock();
    assert(_pause);
    _pause = false;
    _cond.Signal();
    _lock.Unlock();
  }

};



#endif
