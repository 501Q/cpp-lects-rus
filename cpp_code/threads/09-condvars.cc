#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using std::thread;
using std::unique_lock;

std::mutex gmut;
int counter {0};
std::condition_variable data_cond;

constexpr int MAXCNT = 100;

void processing() {
  int c;
  {
    std::unique_lock<std::mutex> lk{gmut};
    data_cond.wait(lk, []{ return counter > 0;});
    // here lock for gmut obtained
    c = counter;
  }
  std::cout << "p:" << c;
}

void preparation() {
  {
    std::lock_guard<std::mutex> lk{gmut};
    // here lock for gmut obtained
    counter += 1;      
    data_cond.notify_one();
  }    
  std::cout << "+";
}

int
main() {
  std::thread t1 {processing};
  std::thread t2 {preparation};
  t1.join();
  t2.join();
  std::cout << "\n";
}
