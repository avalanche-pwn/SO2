#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <semaphore>
#include <set>
#include <signal.h>
#include <thread>
#include <vector>

std::atomic_bool end = false;
std::atomic_int initialisation = 0;
std::set<int> currently_eating;
std::vector<double> times;
std::mutex set_mutex;
#include <thread>

class FIFOMutex {
public:
  void lock() {
    std::unique_lock<std::mutex> lock(mtx);
    std::thread::id id = std::this_thread::get_id();
    queue.push(id);
    if (queue.front() != id) {
      cond.wait(lock, [this, id] { return queue.front() == id; });
    }
  }

  void unlock() {
    std::unique_lock<std::mutex> lock(mtx);
    queue.pop();
    cond.notify_all();
  }

private:
  std::mutex mtx;
  std::condition_variable cond;
  std::queue<std::thread::id> queue;
};
struct Stick {
  FIFOMutex m;
  Stick() : m() {}
};
void philosoph(uint64_t id, Stick &l, Stick &r, bool is_last) {
  if (!is_last)
    r.m.lock();
  initialisation--;
  while (initialisation)
    ;

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  double eating_time = 0;
  if (is_last)
    r.m.lock();
  while (true) {
    l.m.lock();
    auto t_start = std::chrono::steady_clock::now();
    set_mutex.lock();
    currently_eating.insert(id);
    set_mutex.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    set_mutex.lock();
    currently_eating.extract(id);
    set_mutex.unlock();

    auto t_end = std::chrono::steady_clock::now();
    eating_time +=
        std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start)
            .count();

    l.m.unlock();
    r.m.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (end)
      break;
    r.m.lock();
  }
  set_mutex.lock();
  times.push_back(eating_time);
  set_mutex.unlock();
};

void finish(int s) { end = true; }

int main(int argc, char **argv) {
  signal(SIGINT, finish);
  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " count";
  }

  uint64_t phil_count = std::stoi(argv[1]);

  std::vector<Stick> sticks(phil_count);
  std::thread threads[phil_count];
  std::counting_semaphore<1> initialise(phil_count);
  initialisation = phil_count;
  int start = phil_count - 1;

  for (int i = 0; i < phil_count - 1; i++) {
    threads[i] = std::thread(philosoph, i, std::ref(sticks[start % phil_count]),
                             std::ref(sticks[(start + 1) % phil_count]), false);
    start++;
  }
  threads[phil_count - 1] = std::thread(
      philosoph, phil_count - 1, std::ref(sticks[(start + 1) % phil_count]),
      std::ref(sticks[(start) % phil_count]), true);

  while (!end) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
      std::lock_guard<std::mutex> guard(set_mutex);
      std::cout << "\033[KCurrently eating: ";
      for (auto &elem : currently_eating) {
        std::cout << elem << " ";
      }
      std::cout << "\r";
      std::cout.flush();
    }
  };
  for (int i = 0; i < phil_count; i++) {
    threads[i].join();
  }
  double avg = 0;
  for (auto &elem : times) {
    avg += elem;
  }
  avg /= phil_count;
  std::cout << "\033[KOdchylenie poszczególnych wyników od średniej:\n";
  for (auto &elem : times) {
    std::cout << avg / elem << "\n";
  }

  return 0;
}
