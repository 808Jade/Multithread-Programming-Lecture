#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <atomic>

using namespace std;

const int MAX_THREADS = 16;
const int CACHE_LINE_SIZE_INT = 16;

volatile int sum = 0;

struct NUM {
	alignas(64) volatile int value;
};
NUM array_sum[MAX_THREADS] = { 0 };

mutex mtx;

void worker1(const int thread_id, const int loop)
{
	//volatile int local_sum = 0;
	for (auto i = 0; i < loop; ++i) {
		//mtx.lock();

		array_sum[thread_id].value = array_sum[thread_id].value + 2;
		//local_sum += 2;
		//mtx.unlock();
	}
	//sum += local_sum;
}

void worker()
{
	for (auto i = 0; i < 25000000; ++i) {
		mtx.lock();
		sum = sum + 2;
		mtx.unlock();
	}
}

int main()
{
	using namespace std::chrono;

	high_resolution_clock::time_point t1_start = high_resolution_clock::now();
	
	for (auto i = 0; i < 50000000; ++i) {
		sum = sum + 2;
	}
	
	cout << "Sum = " << sum << endl;

	high_resolution_clock::time_point t1_end = high_resolution_clock::now();
	high_resolution_clock::duration t1_duration = t1_end - t1_start;
	cout << "single thread : " << t1_duration.count() << " ticks" << endl;

	//high_resolution_clock::time_point t2_start = high_resolution_clock::now();

	//thread t1{ worker1 };
	//thread t2{ worker1 };

	//t1.join();
	//t2.join();

	//cout << "Sum = " << sum << endl;

	//high_resolution_clock::time_point t2_end = high_resolution_clock::now();
	//high_resolution_clock::duration t2_duration = t2_end - t2_start;
	//cout << "multi thread  : " << t2_duration.count() << " ticks" << endl;

	for (int num_threads = 1; num_threads <= 16; num_threads += 2) {
		sum = 0;
		high_resolution_clock::time_point t3_start = high_resolution_clock::now();
		vector<thread> threads;
		for (int i = 0; i < num_threads; ++i) {
			threads.emplace_back(worker1, i, 50000000 / num_threads);
		}
		for ( int i = 0; i < num_threads; ++i) {
			threads[i].join();
		}
		high_resolution_clock::time_point t3_end = high_resolution_clock::now();
		high_resolution_clock::duration t3_duration = t3_end - t3_start;
		cout << num_threads << " threads : " << "Sum = " << sum << "Duration = " << t3_duration.count() << " ticks" << endl;
	}
	
	
}
