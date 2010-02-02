
#include "lockdep.h"

#include "include/types.h"
#include "Clock.h"
#include "BackTrace.h"

#include <ext/hash_map>

#include "config.h"

#undef dout
#define  dout(l)    if (l<=g_conf.debug_lockdep) *_dout << g_clock.now() << " " << std::hex << pthread_self() << std::dec << " lockdep: "


// global
int g_lockdep = 0;

/*
struct foobarr {
  foobarr() { cerr << "lockdep.cc init" << std::endl; }
  ~foobarr() { cerr << "lockdep.cc shutdown" << std::endl; }
} asdff;
*/

// disable lockdep when this module destructs.
struct lockdep_stopper_t {
  ~lockdep_stopper_t() {
    g_lockdep = 0;
  }
};

static lockdep_stopper_t lockdep_stopper;


pthread_mutex_t lockdep_mutex = PTHREAD_MUTEX_INITIALIZER;


#define MAX_LOCKS  100   // increase me as needed

hash_map<const char *, int> lock_ids;
map<int, const char *> lock_names;

int last_id = 0;

hash_map<pthread_t, map<int,BackTrace*> > held;
BackTrace *follows[MAX_LOCKS][MAX_LOCKS];       // follows[a][b] means b taken after a 


#define BACKTRACE_SKIP 3


int lockdep_dump_locks()
{
  pthread_mutex_lock(&lockdep_mutex);

  for (hash_map<pthread_t, map<int,BackTrace*> >::iterator p = held.begin();
       p != held.end();
       p++) {
    dout(0) << "--- thread " << p->first << " ---" << std::endl;
    for (map<int,BackTrace*>::iterator q = p->second.begin();
	 q != p->second.end();
	 q++) {
      dout(0) << "  * " << lock_names[q->first] << std::endl;
      if (q->second)
	q->second->print(*_dout);
    }
  }

  pthread_mutex_unlock(&lockdep_mutex);
  return 0;
}


int lockdep_register(const char *name)
{
  int id;

  pthread_mutex_lock(&lockdep_mutex);

  if (last_id == 0)
    for (int i=0; i<MAX_LOCKS; i++)
      for (int j=0; j<MAX_LOCKS; j++)
	follows[i][j] = NULL;
  
  hash_map<const char *, int>::iterator p = lock_ids.find(name);
  if (p == lock_ids.end()) {
    assert(last_id < MAX_LOCKS);
    id = last_id++;
    lock_ids[name] = id;
    lock_names[id] = name;
    dout(10) << "registered '" << name << "' as " << id << std::endl;
  } else {
    id = p->second;
    dout(20) << "had '" << name << "' as " << id << std::endl;
  }

  pthread_mutex_unlock(&lockdep_mutex);

  return id;
}


// does a follow b?
static bool does_follow(int a, int b)
{
  if (follows[a][b]) {
    *_dout << std::endl;
    dout(0) << "------------------------------------" << std::endl;
    dout(0) << "existing dependency " << lock_names[a] << " (" << a << ") -> " << lock_names[b] << " (" << b << ") at: " << std::endl;
    follows[a][b]->print(*_dout);
    *_dout << std::endl;
    return true;
  }

  for (int i=0; i<MAX_LOCKS; i++) {
    if (follows[a][i] &&
	does_follow(i, b)) {
      dout(0) << "existing intermediate dependency " << lock_names[a] << " (" << a << ") -> " << lock_names[i] << " (" << i << ") at:" << std::endl;
      follows[a][i]->print(*_dout);
      *_dout << std::endl;
      return true;
    }
  }

  return false;
}

int lockdep_will_lock(const char *name, int id)
{
  pthread_t p = pthread_self();
  if (id < 0) id = lockdep_register(name);

  pthread_mutex_lock(&lockdep_mutex);
  dout(20) << "_will_lock " << name << " (" << id << ")" << std::endl;

  // check dependency graph
  map<int, BackTrace *> &m = held[p];
  for (map<int, BackTrace *>::iterator p = m.begin();
       p != m.end();
       p++) {
    if (p->first == id) {
      *_dout << std::endl;
      dout(0) << "recursive lock of " << name << " (" << id << ")" << std::endl;
      BackTrace *bt = new BackTrace(BACKTRACE_SKIP);
      bt->print(*_dout);
      if (p->second) {
	*_dout << std::endl;
	dout(0) << "previously locked at" << std::endl;
	p->second->print(*_dout);
      }
      *_dout << std::endl;
      assert(0);
    }
    else if (!follows[p->first][id]) {
      // new dependency 
      
      // did we just create a cycle?
      BackTrace *bt = new BackTrace(BACKTRACE_SKIP);
      if (does_follow(id, p->first)) {
	dout(0) << "new dependency " << lock_names[p->first] << " (" << p->first << ") -> " << name << " (" << id << ")"
		<< " creates a cycle at"
		<< std::endl;
	bt->print(*_dout);
	*_dout << std::endl;

	dout(0) << "btw, i am holding these locks:" << std::endl;
	for (map<int, BackTrace *>::iterator q = m.begin();
	     q != m.end();
	     q++) {
	  dout(0) << "  " << lock_names[q->first] << " (" << q->first << ")" << std::endl;
	  if (q->second) {
	    q->second->print(*_dout);
	    *_dout << std::endl;
	  }
	}

	*_dout << std::endl;
	*_dout << std::endl;

	// don't add this dependency, or we'll get aMutex. cycle in the graph, and
	// does_follow() won't terminate.

	assert(0);  // actually, we should just die here.
      } else {
	follows[p->first][id] = bt;
	dout(10) << lock_names[p->first] << " -> " << name << " at" << std::endl;
	//bt->print(*_dout);
      }
    }
  }

  pthread_mutex_unlock(&lockdep_mutex);
  return id;
}

int lockdep_locked(const char *name, int id, bool force_backtrace)
{
  pthread_t p = pthread_self();

  if (id < 0) id = lockdep_register(name);

  pthread_mutex_lock(&lockdep_mutex);
  dout(20) << "_locked " << name << std::endl;
  if (g_lockdep >= 2 || force_backtrace)
    held[p][id] = new BackTrace(BACKTRACE_SKIP);
  else
    held[p][id] = 0;
  pthread_mutex_unlock(&lockdep_mutex);
  return id;
}

int lockdep_will_unlock(const char *name, int id)
{
  pthread_t p = pthread_self();
  
  if (id < 0) {
    //id = lockdep_register(name);
    assert(id == -1);
    return id;
  }

  pthread_mutex_lock(&lockdep_mutex);
  dout(20) << "_will_unlock " << name << std::endl;

  // don't assert.. lockdep may be enabled at any point in time
  //assert(held.count(p));
  //assert(held[p].count(id));

  delete held[p][id];
  held[p].erase(id);
  pthread_mutex_unlock(&lockdep_mutex);
  return id;
}


