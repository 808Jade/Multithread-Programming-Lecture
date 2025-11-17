#include <thread>
#include <iostream>
#include <vector>
#include <chrono>
#include <mutex>
#include <set>
#include <unordered_set>
#include <immintrin.h>

const int MAX_THREADS = 16;
int num_threads = 0;

class NODE {
public:
	int value;
	NODE* volatile next;
	NODE(int v) : value(v), next(nullptr) {}
};

constexpr int ST_EMPTY = 0;
constexpr int ST_WAITING = 1;
constexpr int ST_BUSY = 2;
constexpr int TIME_OUT = 100;

class LockFreeExchanger {
	std::atomic<long long> slot;
public:
	std::atomic<int> successful_exchanges{ 0 };
	LockFreeExchanger() : slot (0) { }
	int exchange(int my_item, bool* busy) {
		*busy = false;
		for (int j = 0; j < TIME_OUT; ++j) {
			long long s = slot;
			int item = (int)(s & 0xFFFFFFFF);
			int status = (int)((s >> 32) & 0x3);
			switch (status) {
				case ST_EMPTY: {
					long long new_s = ((long long)my_item & 0xFFFFFFFF) | ((long long)ST_WAITING << 32);
					if (std::atomic_compare_exchange_strong(&slot, &s, new_s)) {// 슬롯에 자신의 아이템을 넣고 상태를 WAITING으로 바꾸기를 시도
						for (int i = 0; i < TIME_OUT; ++i) { // 스핀을 하면서 다른 스레드가 교환을 끝내길 기다림
							s = slot;
							status = (int)((s >> 32) & 0x3);
							if (status == ST_BUSY) { // 교환이 완료된 후
								int their_item = (int)(s & 0xFFFFFFFF);
								slot = 0; // set to EMPTY
								successful_exchanges++;
								return their_item;
							}
						}
						if (std::atomic_compare_exchange_strong(&slot, &s, 0)) { // 다른 스레드가 나타나지 않는다면 CAS로 EMPTY로 전환
							return -2; // TIME OUT
						}
						else { // someone is BUSY
							// 대기중이던 스레드는 교환을 완료한다?
							s = slot;
							int their_item = (int)(s & 0xFFFFFFFF);
							slot = 0; // set to EMPTY
							successful_exchanges++;
							return their_item;
						}
					}
					break; // CAS 실패 시 다시 시도
				}
				case ST_WAITING: {
					long long new_s = ((long long)my_item & 0xFFFFFFFF) | ((long long)ST_BUSY << 32);
					if (std::atomic_compare_exchange_strong(&slot, &s, new_s)) {
						int their_item = item;
						successful_exchanges++;
						return their_item;
					}
					break;
				}
				case ST_BUSY: {
					*busy = true;
					break;
				}
			}
		}
		return -2; // TIME OUT
	}
};

class EliminationArray {
	int range;
	LockFreeExchanger exchanger[MAX_THREADS / 2 - 1];
public:
	EliminationArray() { range = 1; }
	~EliminationArray() {}
	int Visit(int value) {
		int slot = rand() % range;
		bool busy;
		int ret = exchanger[slot].exchange(value, &busy);
		int old_range = range;
		if ((ret == -2) && (old_range > 1))  // TIME OUT
			range = old_range - 1;
		if ((true == busy) && (old_range <= num_threads / 2 - 1))
			range = old_range + 1; // MAX RANGE is # of thread / 2
		return ret;
	}
	int total_success() const {
		int sum = 0;
		for (int i = 0; i < MAX_THREADS / 2 - 1; ++i)
			sum += exchanger[i].successful_exchanges;
		return sum;
	}
	void clear() {
		for (int i = 0; i < MAX_THREADS / 2 - 1; ++i)
			exchanger[i].successful_exchanges = 0;
	}
};

class LFEL_STACK {
	NODE* top;
	EliminationArray elim_array;

public:
	LFEL_STACK() {
		top = nullptr;
	}

	~LFEL_STACK() {
		clear();
	}

	void clear() {
		while (nullptr != top) pop();
		elim_array.clear();
	}

	bool CAS(NODE* volatile* addr, NODE* expected, NODE* desired)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic<NODE*>*>(addr),
			&expected,
			desired);
	}

	void push(int x)
	{
		NODE* new_node = new NODE(x);
		while (true) {
			new_node->next = top;
			if (CAS(&top, new_node->next, new_node))
				return;
			int elim_res = elim_array.Visit(x);
			if (elim_res == -2) // TIME OUT
				continue;
			if (elim_res == -1) { // pop과의 교환
				return;
			}
		}

	}

	int pop()
	{
		while (true) {
			NODE* curr_top = top;
			if (nullptr == curr_top) {
				return -2;
			}

			NODE* next_node = curr_top->next;
			if (CAS(&top, curr_top, next_node)) {
				int res = curr_top->value;
				//delete curr_top;
				return res;
			}

			int elim_res = elim_array.Visit(-1);
			if (elim_res == -2)
				continue;
			if (elim_res != -1) {
				return elim_res;
			}
		}
	}

	void print20()
	{
		NODE* curr = top;
		for (int i = 0; i < 20 && curr != nullptr; i++, curr = curr->next)
			std::cout << curr->value << ", ";
		std::cout << "\n";
		std::cout << "Total Successful Eliminations: " << elim_array.total_success() << "\n";
	}
};


LFEL_STACK my_stack;

struct HISTORY {
	std::vector <int> push_values, pop_values;
};
std::atomic_int stack_size;
thread_local int thread_id;
const int NUM_TEST = 10000000;

void benchmark(const int num_thread)
{
	int key = 0;
	const int loop_count = NUM_TEST / num_thread;
	for (auto i = 0; i < loop_count; ++i) {
		if ((rand() % 2 == 0) || (i < 1000))
			my_stack.push(key++);
		else
			my_stack.pop();
	}
}

void benchmark_test(const int th_id, const int num_threads, HISTORY& h)
{
	thread_id = th_id;
	int loop_count = NUM_TEST / num_threads;
	for (int i = 0; i < loop_count; i++) {
		if ((rand() % 2) || i < 128 / num_threads) {
			h.push_values.push_back(i);
			stack_size++;
			my_stack.push(i);
		}
		else {
			volatile int curr_size = stack_size--;
			int res = my_stack.pop();
			if (res == -2) {
				stack_size++;
				if ((curr_size > num_threads * 2) && (stack_size > num_threads)) {
					std::cout << "ERROR Non_Empty Stack Returned NULL\n";
					exit(-1);
				}
			}
			else h.pop_values.push_back(res);
		}
	}
}

void check_history(std::vector <HISTORY>& h)
{
	std::unordered_multiset <int> pushed, poped, in_stack;

	for (auto& v : h)
	{
		for (auto num : v.push_values) pushed.insert(num);
		for (auto num : v.pop_values) poped.insert(num);
		while (true) {
			int num = my_stack.pop();
			if (num == -2) break;
			poped.insert(num);
		}
	}
	for (auto num : pushed) {
		if (poped.count(num) < pushed.count(num)) {
			std::cout << "Pushed Number " << num << " does not exists in the STACK.\n";
			exit(-1);
		}
		if (poped.count(num) > pushed.count(num)) {
			std::cout << "Pushed Number " << num << " is poped more than " << poped.count(num) - pushed.count(num) << " times.\n";
			exit(-1);
		}
	}
	for (auto num : poped)
		if (pushed.count(num) == 0) {
			std::multiset <int> sorted;
			for (auto num : poped)
				sorted.insert(num);
			std::cout << "There were elements in the STACK no one pushed : ";
			int count = 20;
			for (auto num : sorted)
				std::cout << num << ", ";
			std::cout << std::endl;
			exit(-1);

		}
	std::cout << "NO ERROR detectd.\n";
}

int main()
{
	using namespace std::chrono;

	for (int n = 1; n <= MAX_THREADS; n = n * 2) {
		num_threads = n;
		my_stack.clear();
		std::vector<std::thread> tv;
		std::vector<HISTORY> history;
		history.resize(n);
		stack_size = 0;
		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < n; ++i) {
			tv.emplace_back(benchmark_test, i, n, std::ref(history[i]));
		}
		for (auto& th : tv)
			th.join();
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		size_t ms = duration_cast<milliseconds>(exec_t).count();
		std::cout << n << " Threads,  " << ms << "ms. ----";
		my_stack.print20();
		check_history(history);
	}

	for (int n = 1; n <= MAX_THREADS; n *= 2) {
		num_threads = n;
		my_stack.clear();
		std::vector<std::thread> tv;
		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < n; ++i) {
			tv.emplace_back(benchmark, n);
		}
		for (auto& th : tv)
			th.join();
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		size_t ms = duration_cast<milliseconds>(exec_t).count();
		std::cout << n << " Threads,  " << ms << "ms. ----";
		my_stack.print20();
	}
}