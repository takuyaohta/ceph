#ifndef __MANCHORREPLY_H
#define __MANCHORREPLY_H

#include <vector>

#include "msg/Message.h"
#include "mds/AnchorTable.h"

#include "MAnchorRequest.h"


class MAnchorReply : public Message {
  int op;
  inodeno_t ino;
  vector<Anchor*> trace;

 public:
  MAnchorReply() {}
  MAnchorReply(MAnchorRequest *req) : Message(MSG_MDS_ANCHORREPLY) {
	this->op = req->get_op();
	this->ino = req->get_ino();
  }
  ~MAnchorReply() {
	for (int i=0; i<trace.size(); i++) delete trace[i];
  }
  virtual char *get_type_name() { return "arep"; }

  void set_trace(vector<Anchor*>& trace) { this->trace = trace; }

  int get_op() { return op; }
  inodeno_t get_ino() { return ino; }
  vector<Anchor*>& get_trace() { return trace; }

  virtual void decode_payload(crope& s, int& off) {
	s.copy(off, sizeof(op), (char*)&op);
	off += sizeof(op);
	s.copy(off, sizeof(ino), (char*)&ino);
	off += sizeof(ino);
	int n;
	s.copy(off, sizeof(int), (char*)&n);
	off += sizeof(int);
	for (int i=0; i<n; i++) {
	  Anchor *a = new Anchor;
	  a->_unrope(s, off);
	  trace.push_back(a);
	}
  }

  virtual void encode_payload(crope& r) {
	r.append((char*)&op, sizeof(op));
	r.append((char*)&ino, sizeof(ino));
	int n = trace.size();
	r.append((char*)&n, sizeof(int));
	for (int i=0; i<n; i++) 
	  trace[i]->_rope(r);
  }
};

#endif
