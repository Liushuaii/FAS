#ifndef FAS_CONDITION_H
#define FAS_CONDITION_H
#include <Mutex.h>

#include <boost/noncopyable.hpp>
#include <pthread.h>

class Condition : boost::noncopyable {
public:
  Condition(Mutex& mutex);
  ~Condition() ;

  void wait();
  // returns true if time out, false otherwise.
  bool waitForSeconds(int seconds);

  void notify() ;
  void notifyAll();

private:
  Mutex& mutex_;
  pthread_cond_t cond_;
};
#endif  // FAS_CONDITION_H