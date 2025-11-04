#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <atomic>

const int MAX_THREADS = 8;

volatile int sum = 0;
std::atomic<int> X{ 0 };

void cas_lock(int thread_id)
{
	int expected;
	
	while (!std::atomic_compare_exchange_strong(&X, &expected, 1)) {
		expected = 0;
	}
}

void cas_unlock(int thread_id)
{
	X= 0;
}

void worker(const int thread_id, const int loop_count)
{
	for (auto i = 0; i < loop_count; ++i) {
		cas_lock(thread_id);
		sum += 2;
		cas_unlock(thread_id);
	}
}

int main()
{
	using namespace std::chrono;

	for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
		sum = 0;
		high_resolution_clock::time_point t3_start = high_resolution_clock::now();
		std::vector<std::thread> threads;
		for (int i = 0; i < num_threads; ++i) {
			threads.emplace_back(worker, i, 500'0000 / num_threads);
		}
		for (int i = 0; i < num_threads; ++i) {
			threads[i].join();
		}
		high_resolution_clock::time_point t3_end = high_resolution_clock::now();
		high_resolution_clock::duration t3_duration = t3_end - t3_start;
		std::cout << num_threads << " thread : " << "Sum = " << sum << " Duration = " << duration_cast<milliseconds>(t3_duration).count() << " ms" << std::endl;
	}
}