#include <thread>
#include <iostream>
#include <vector>
#include <chrono>
#include <mutex>
#include <windows.h>
#include <queue>

const int MAX_THREADS = 32;

thread_local std::queue<STNODE64*> free_nodes;

class STNODE64;

class alignas(16) STPTR64 {
public:
	STNODE64* volatile ptr;
	long long stamp;

	void set_ptr(STNODE64* p) {
		ptr = p;
	}
	STNODE64* get_ptr() {
		return ptr;
	}
	STNODE64* get_ptr(long long* stamp) {
		STPTR64 temp{ 0, 0 };
		STPTR64 old{ 0, 0 };
		if (false == CAS128(&temp, &old, this)) { // temp가 old면 this로 바꿔
			std::cout << "CAS128 failed in get_ptr!\n";
			exit(-1);
		}
		*stamp = temp.stamp;
		return temp.ptr;
	}

	bool CAS128(STPTR64* addr, STPTR64* expected, STPTR64* new_value)
	{
		return InterlockedCompareExchange128(
			reinterpret_cast<long long*>(addr),
			new_value->stamp,
			reinterpret_cast<long long>(new_value->ptr),
			reinterpret_cast<long long*>(expected));
	}

	bool CAS(STNODE64* old_value, STNODE64* new_value, long long old_stamp, long long new_stamp)
	{
		STPTR64 old_p{ old_value, old_stamp };
		STPTR64 new_p{ new_value, new_stamp };
		return CAS128(this, &old_p, &new_p);
	}
};

class STNODE64 {
public:
	long long value;
	STPTR64 next;
	STNODE64(long long v) : value(v) {}
};

class LFST_QUEUE64 {
	STPTR64 head, tail;
public:
	LFST_QUEUE64() {
		head.set_ptr(new STNODE64(-1));
		tail.set_ptr(head.get_ptr());
	}

	~LFST_QUEUE64() {
		clear();
		delete head.get_ptr();
	}

	void clear() {
		STNODE64* curr = head.get_ptr()->next.get_ptr();
		while (nullptr != curr) {
			STNODE64* next = curr->next.get_ptr();
			delete curr;
			curr = next;
		}
		tail.set_ptr(head.get_ptr());
		head.get_ptr()->next.set_ptr(nullptr);
	}

	void enqueue(int x)
	{
		STNODE64* new_node = new STNODE64(x);
		if (free_nodes.empty()) new_node = new STNODE64(x);
		else {
			new_node = free_nodes.front();
			
		}
		while (true) {
			long long tail_stamp = 0;
			STNODE64* old_tail = tail.get_ptr(&tail_stamp);
			long long next_stamp = 0;
			STNODE64* old_next = old_tail->next.get_ptr(&next_stamp);
			if (old_tail != tail.get_ptr())
				continue;
			if (old_next == nullptr) {
				if (true == old_tail->next.CAS(nullptr, new_node, next_stamp, next_stamp + 1)) {
					tail.CAS(old_tail, new_node, tail_stamp, tail_stamp + 1);
					return;
				}
			}
			else
				tail.CAS(old_tail, old_next, tail_stamp, tail_stamp + 1);
		}
	}

	int dequeue()
	{
		while (true) {
			long long head_stamp = 0;
			STNODE64* old_head = head.get_ptr(&head_stamp);
			long long next_stamp = 0;
			STNODE64* old_next = old_head->next.get_ptr(&next_stamp);
			long long tail_stamp = 0;
			STNODE64* old_tail = tail.get_ptr(&tail_stamp);
			if (old_head != head.get_ptr())
				continue;
			if (old_next == nullptr)
				return -1;
			if (old_tail == old_head) {
				tail.CAS(old_tail, old_next, tail_stamp, tail_stamp + 1);
				continue;
			}
			int res = old_next->value;
			if (true == head.CAS(old_head, old_next, head_stamp, head_stamp + 1)) {
				// 여기도 delete 하지 말고 free_nodes 이용..
				//delete old_head;
				return res;
			}
		}
	}

	void print20()
	{
		STNODE64* curr = head.get_ptr()->next.get_ptr();
		for (int i = 0; i < 20 && curr != nullptr; i++, curr = curr->next.get_ptr())
			std::cout << curr->value << ", ";
		std::cout << "\n";
	}
};

LFST_QUEUE64 my_queue;

const int NUM_TEST = 10000000;

void benchmark(const int num_thread, int th_id)
{
	const int loop_count = NUM_TEST / num_thread;

	int key = 0;
	for (int i = 0; i < loop_count; i++) {
		if ((i < 32) || (rand() % 2 == 0))
			my_queue.enqueue(key++);
		else
			my_queue.dequeue();
	}

}

int main()
{
	using namespace std::chrono;

	for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
		my_queue.clear();
		auto st = high_resolution_clock::now();
		std::vector<std::thread> threads;
		for (int i = 0; i < num_threads; ++i)
			threads.emplace_back(benchmark, num_threads, i);
		for (int i = 0; i < num_threads; ++i)
			threads[i].join();
		auto ed = high_resolution_clock::now();
		auto time_span = duration_cast<milliseconds>(ed - st).count();
		std::cout << "Thread Count = " << num_threads << ", Exec Time = " << time_span << "ms.\n";
		std::cout << "Result : ";  my_queue.print20();
	}
}
