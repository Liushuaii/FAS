#include <EventLoop.h>
#include <Timestamp.h>
#include <Thread.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <Log.h>

#include <boost/bind.hpp>
#include <boost/core/ignore_unused.hpp>

#include <sys/eventfd.h>

int EventLoop::count_ = 0;

EventLoop::EventLoop() :
  poll_(NULL),
  pollDelayTime_(10000),
  revents_(),
  handles_(),
  updates_(),
  mutex_(),
  cond_(mutex_),
  tid_(gettid()),
  wakeUpFd_(createEventfd()),
  wakeUpHandle_(new Handle(this, Events(wakeUpFd_, kReadEvent))),
  functors_(),
  runningFunctors_(false),
  quit_(false) {

  poll_.reset(new Poller);
  assert(poll_);
  count_++;

  wakeUpHandle_->setHandleRead(boost::bind(&EventLoop::handWakeUp, this, _1, _2));
  addHandle(wakeUpHandle_);
}

int EventLoop::getCount() const {
  return count_;
}

bool EventLoop::updateHandle(SHandlePtr handle) {
  assert(handle->getLoop() == this);
  MutexLocker lock(mutex_);(void)lock;
  updates_.insert({handle->getEvent()->getFd(), handle});
  return true;
}

bool EventLoop::addHandle(HandlePtr handle) {
  assert(handle->getState() == Handle::state::STATE_NEW);
  handle->setState(Handle::state::STATE_ADD);
  return updateHandle(shared_ptr<Handle>(handle));
}

//FIXME:mod by fd
bool EventLoop::modHandle(HandlePtr handle) {
  assert(handle->getState() != Handle::state::STATE_DEL);
  handle->setState(Handle::state::STATE_MOD);
  return updateHandle(handles_.find(handle->getEvent()->getFd())->second);
}

//FIXME:del by fd
bool EventLoop::delHandle(HandlePtr handle) {
  handle->setState(Handle::state::STATE_DEL);
  return updateHandle(handles_.find(handle->getEvent()->getFd())->second);
}

bool EventLoop::updateHandles() {
  MutexLocker lock(mutex_);(void)lock;
  for(auto cur = updates_.begin(); cur != updates_.end(); cur++) {
    SHandlePtr handle = cur->second;
    Events* event = handle->getEvent();


    int n = 0;


    switch(handle->getState()) {
    case Handle::state::STATE_ADD:
      poll_->events_add_(event);
      handles_[cur->first] = handle;
      handle->setState(Handle::state::STATE_LOOP);
      break;
    case Handle::state::STATE_MOD:
      poll_->events_mod_(event);
      //mod
      handles_[cur->first] = handle;
      handle->setState(Handle::state::STATE_LOOP);
      break;
    case Handle::state::STATE_DEL:
      poll_->events_del_(event);
        //del
      n = handles_.erase(handle->getEvent()->getFd());
      ::close(handle->getEvent()->getFd());
      cout << "close fd! n = "  << n << endl;
      break;
    default :
      assert(false);
    }
  }
  return true;
}

bool EventLoop::isInLoopOwnerThread() {
  return (gettid() == tid_);
}

void EventLoop::assertInOwner() {
  assert(isInLoopOwnerThread());
}

void EventLoop::wakeUp() {
  uint64_t one = 1;
  ssize_t n = ::write(wakeUpFd_, &one, sizeof one);
  if (n != sizeof one) {
    //cout << "EventLoop::wakeup() writes " << n << " bytes instead of 8" << endl;
  }

}

void EventLoop::handWakeUp(Events event, Timestamp time) {
  boost::ignore_unused(event, time);
  uint64_t one = 1;
  ssize_t n = ::read(wakeUpFd_, &one, sizeof one);
  if (n != sizeof one){
    cout << "EventLoop::handleRead() reads " << n << " bytes instead of 8" <<endl;
  }
}

int createEventfd() {
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    LOG_SYSERR("Failed in eventfd");
    ::abort();
  }
  return evtfd;
}


void EventLoop::runInLoop(const Functor& cb) {
  if (isInLoopOwnerThread()) {
    cb();
  } else {
    queueInLoop(cb);
  }
}

void EventLoop::queueInLoop(const Functor& cb) {
  {
    MutexLocker lock(mutex_);
    functors_.push_back(cb);
  }

  if (!isInLoopOwnerThread() || runningFunctors_) {
    wakeUp();
  }
}

void EventLoop::runFunctors() {
  std::vector<Functor> functors;
  runningFunctors_ = true;

  {
    MutexLocker lock(mutex_);
    functors.swap(functors_);
  }

  for (size_t i = 0; i < functors.size(); ++i) {
    functors[i]();
  }
  runningFunctors_ = false;
}

void EventLoop::quit() {
  MutexLocker lock(mutex_);
  quit_ = true;
  wakeUp();
  boost::ignore_unused(lock);
}

bool EventLoop::loop() {
  assertInOwner();
  Timestamp looptime;

  while (!quit_) {
    if (!updates_.empty()) {
      updateHandles();
      cout << "updateHandles" << endl;
    }

    updates_.clear();
    revents_.clear();



    //cout << "tid : " << gettid() << " handles num " << 0 << endl;

    looptime = poll_->loop_(revents_, pollDelayTime_);

    for(std::vector<Events>::iterator iter = revents_.begin();
        iter != revents_.end(); iter++) {
      //handle will decreament reference after for end!
      map<int, SHandlePtr>::iterator finditer = handles_.find((*iter).getFd());
      if (finditer == handles_.end()) {
        continue;
      }
      SHandlePtr handle = finditer->second;
      if (handle->getState() != Handle::state::STATE_LOOP) {
        LOG_DEBUG("After Loop have handle with state STATE_LOOP! it is unnormal!");
        continue;
      }
      //handle revents
      handle->handleEvent(*iter, looptime);
    } //for

    assert(!runningFunctors_);
    runFunctors();
  } //while
  return true;
}

EventLoop::~EventLoop() {
}
