#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <atomic>
#include <queue>

using namespace std;

// queue 의 atomic한 구현..
std::atomic<queue<int>> g_queue;

const int MAX_THREADS = 2;
volatile int sum = 0;
mutex mtx;

//volatile int victim = 0;
//volatile bool flags[2] = { false , false };
atomic<int> victim = 0;
atomic<bool> flags[2] = { false , false };

void p_lock(const int thread_id) 
{
	const int other = 1 - thread_id;
	flags[thread_id] = true;
	victim = thread_id;
	//atomic_thread_fence(std::memory_order_seq_cst);
	while (flags[other] && victim == thread_id);
}

void p_unlock(const int thread_id) 
{
	flags[thread_id] = false;
}

void worker(const int thread_id, const int loop_count)
{
	for (auto i = 0; i < loop_count; ++i) {
		p_lock(thread_id);
		sum = sum + 2;
		p_unlock(thread_id);
	}
}

int main()
{
	using namespace std::chrono;

	// Single thread
	high_resolution_clock::time_point t1_start = high_resolution_clock::now();
	for (auto i = 0; i < 50000000; ++i) {
		sum = sum + 2;
	}
	high_resolution_clock::time_point t1_end = high_resolution_clock::now();
	high_resolution_clock::duration t1_duration = t1_end - t1_start;
	cout << "Single thread : Sum = " << sum << " Duration = " << t1_duration.count() << " ticks" << endl;

	for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
		sum = 0;
		high_resolution_clock::time_point t3_start = high_resolution_clock::now();
		vector<thread> threads;
		for (int i = 0; i < num_threads; ++i) {
			threads.emplace_back(worker, i, 50000000 / num_threads);
		}
		for (int i = 0; i < num_threads; ++i) {
			threads[i].join();
			//sum = sum + array_sum[i].value;
			//array_sum[i].value = 0;
		}
		high_resolution_clock::time_point t3_end = high_resolution_clock::now();
		high_resolution_clock::duration t3_duration = t3_end - t3_start;
		cout << num_threads << " thread : " << "Sum = " << sum << " Duration = " << duration_cast<milliseconds>(t3_duration).count() << " ms" << endl;
	}

}
