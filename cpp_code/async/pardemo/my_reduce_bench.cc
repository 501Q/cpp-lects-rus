//------------------------------------------------------------------------------
//
// Reduce benchmark: parallel reduce versus regular versus homebrew
//
//------------------------------------------------------------------------------
//
// This file is licensed after GNU GPL v3
//
//------------------------------------------------------------------------------

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <execution>
#include <iostream>
#include <random>
#include <string>
#include <thread>

using std::chrono::duration_cast;
using chrc = std::chrono::high_resolution_clock;
using cms = std::chrono::milliseconds;

struct config_t {
  int policy;
  int num;
  int ncycles;
  void dump(std::ostream &os) const {
    os << policy << " " << num << " " << ncycles;
  }
};

std::ostream &operator<<(std::ostream &os, const config_t &cfg) {
  cfg.dump(os);
  return os;
}

void print_usage(const char *pname) {
  std::cout << "Usage: '" << pname << " <POLICY> <NUM> <NREP = 1>'\n";
  std::cout << "\twhere policy is 0 for seq, 1 for par, 2 for par_unseq, 3 for "
               "self\n";
  std::cout << "\t      num is number of elements to sort (less then 2^31)\n";
  std::cout << "\t      nrep is number of cycles to repeat (less then 2^31)\n";
}

config_t read_config(int argc, char **argv) {
  config_t cfg;
  if ((argc < 3) || (argc > 4)) {
    print_usage(argv[0]);
    exit(0);
  }

  cfg.policy = std::stoi(argv[1]);
  cfg.num = std::stoi(argv[2]);
  cfg.ncycles = 1;
  if (argc > 3)
    cfg.ncycles = std::stoi(argv[3]);

  if ((cfg.policy < 0) || (cfg.policy > 3) || (cfg.num < 1) ||
      (cfg.ncycles < 1)) {
    print_usage(argv[0]);
    exit(0);
  }

#if DEBUG
  std::cout << "Config: " << cfg << std::endl;
#endif
  return cfg;
}

template <typename It> void random_fill(It start, It fin) {
  using VT = typename std::iterator_traits<It>::value_type;
  static std::random_device rnd_device;
  static std::mt19937 mersenne_engine{rnd_device()};
  static std::uniform_int_distribution<VT> dist;
  static auto gen = [] { return dist(mersenne_engine); };
  generate(start, fin, gen);
}

template <typename It> void dump_range(It start, It fin) {
  for (auto it = start; it != fin; ++it)
    std::cout << *it << " ";
  std::cout << std::endl;
}

constexpr int MINLEN = 25;

unsigned determine_threads(unsigned length) {
  const unsigned long min_per_thread = MINLEN;
  unsigned long max_threads = length / min_per_thread;
  unsigned long hardware_conc = std::thread::hardware_concurrency();
  return std::min(hardware_conc != 0 ? hardware_conc : 2, max_threads);
}

template <typename Iterator,
          typename T = typename std::iterator_traits<Iterator>::value_type>
T parallel_accumulate(Iterator first, Iterator last, T init = 0) {
  long length = distance(first, last);
  if (0 == length)
    return init;
  if (length <= MINLEN)
    return std::accumulate(first, last, init);

  static unsigned nthreads = determine_threads(length);
  long bsize = length / nthreads;

  std::vector<std::thread> threads(nthreads);
  std::vector<T> results(nthreads + 1);

  auto accumulate_block = [](Iterator first, Iterator last, T &result) {
    result = std::accumulate(first, last, result);
  };

  unsigned tidx = 0;

  for (; length >= bsize * (tidx + 1); first += bsize, tidx += 1)
    threads[tidx] = std::thread(accumulate_block, first, first + bsize,
                                std::ref(results[tidx]));

  auto remainder = length - bsize * tidx;

  if (remainder > 0) {
    assert(tidx == nthreads);
    accumulate_block(first, first + remainder, std::ref(results[tidx]));
  }

  for (auto &&t : threads)
    t.join();

  return std::accumulate(results.begin(), results.end(), init);
}

struct myseq_t {};

myseq_t myseq;

template <typename T, typename It> auto do_bench(T policy, It start, It fin) {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  if constexpr (std::is_same_v<T, myseq_t>) {
    return parallel_accumulate(start, fin);
  } else {
    return std::reduce(policy, start, fin);
  }
};

int main(int argc, char **argv) {
  unsigned val;
  auto cfg = read_config(argc, argv);
  std::vector<unsigned> v(cfg.num);
  random_fill(v.begin(), v.end());

#if DEBUG
  dump_range(v.begin(), v.end());
#endif

  auto tstart = chrc::now();
  switch (cfg.policy) {
  case 0:
    for (int i = 0; i < cfg.ncycles; ++i)
      val = do_bench(std::execution::seq, v.begin(), v.end());
    break;
  case 1:
    for (int i = 0; i < cfg.ncycles; ++i)
      val = do_bench(std::execution::par, v.begin(), v.end());
    break;
  case 2:
    for (int i = 0; i < cfg.ncycles; ++i)
      val = do_bench(std::execution::par_unseq, v.begin(), v.end());
    break;
  case 3:
    for (int i = 0; i < cfg.ncycles; ++i)
      val = do_bench(myseq, v.begin(), v.end());
    break;
  default:
    std::cerr << "Incorrect policy value" << std::endl;
    exit(0);
  }
  auto tfin = chrc::now();
  std::cout << "Wall time: " << duration_cast<cms>(tfin - tstart).count()
            << std::endl;
  return val;
}
