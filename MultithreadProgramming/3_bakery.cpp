#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <atomic>

const int MAX_THREADS = 16;

volatile bool flag[MAX_THREADS];
volatile int label[MAX_THREADS];
volatile int sum = 0;
//std::atomic<bool> flag[MAX_THREADS];
//std::atomic<int> label[MAX_THREADS];
//std::atomic<int> sum{ 0 };

void cas_lock(int thread_id)
{
	flag[thread_id] = true;

	int max = 0;
	for (int i = 0; i < MAX_THREADS; ++i) {
		if (label[i] > max) max = label[i];
	}
	label[thread_id] = max + 1;

	for (int i = 0; i < MAX_THREADS; ++i) {
		if (i == thread_id) continue;
		// 다른 스레드의 flag가 true이고 && ( 상대방의 label이 내 label보다 작거나 || (상대방의lbael과 내 label이 같고 && 내 thread_id가 상대방보다 더 크고) )
		while (flag[i] && (label[i] < label[thread_id] || (label[i] == label[thread_id] && i < thread_id)) ) {}
	}
}

void cas_unlock(int thread_id)
{
	flag[thread_id] = false;
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