#include <iostream>
#include <thread>

using std::cout;
using std::endl;
using std::thread;

int x = 0;

void __attribute__((noinline)) use(int c) { asm(""); }

void race() {
  // going up
  for (int i = 0; i < 1000000; ++i) {
    x += 1;
    use(x);
  }

  // going down
  for (int i = 0; i < 1000000; ++i) {
    x -= 1;
    use(x);
  }
}

int main() {
  for (int i = 0; i < 500; ++i) {
    x = 0;
    thread t1{race};
    thread t2{race};
    t1.join();
    t2.join();
    cout << i << "\t" << ((x + 1000000) % 6075) << endl;
  }
}
