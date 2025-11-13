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

class DUMMY_MUTEX {
public:
	void lock() {}
	void unlock() {}
};

class C_STACK {
	NODE* top;
	DUMMY_MUTEX set_lock;
public:
	C_STACK() {
		top = nullptr;
	}

	~C_STACK() {
		clear();
	}

	void clear() {
		while (nullptr != top) pop();
	}

	void push(int x)
	{
		NODE* new_node = new NODE(x);
		set_lock.lock();
		new_node->next = top;
		top = new_node;
		set_lock.unlock();
	}

	int pop()
	{
		set_lock.lock();
		if (nullptr == top) {
			set_lock.unlock();
			return -2;
		}
		int res = top->value;
		auto temp = top;
		top = top->next;
		set_lock.unlock();
		delete temp;
		return res;
	}

	void print20()
	{
		NODE* curr = top;
		for (int i = 0; i < 20 && curr != nullptr; i++, curr = curr->next)
			std::cout << curr->value << ", ";
		std::cout << "\n";
	}
};

class LF_STACK {
	NODE* volatile top;
public:
	LF_STACK() {
		top = nullptr;
	}

	~LF_STACK() {
		clear();
	}

	void clear() {
		while (nullptr != top) pop();
	}

	bool CAS(NODE* volatile* addr, NODE* expected, NODE* new_value)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic<NODE*>*>(addr),
			&expected,
			new_value);
	}

	void push(int x)
	{
		NODE* new_node = new NODE(x);
		while (true) {
			auto last = top;
			new_node->next = last;
			if (true == CAS(&top, last, new_node))
				return;
		}
	}

	int pop()
	{
		while (true) {
			auto last = top;
			if (nullptr == last) {
				return -2;
			}
			auto next = last->next;
			if (last != top) continue;
			int v = last->value;
			if (true == CAS(&top, last, next)) {
				// delete last;
				return v;
			}
		}
	}

	void print20()
	{
		NODE* curr = top;
		for (int i = 0; i < 20 && curr != nullptr; i++, curr = curr->next)
			std::cout << curr->value << ", ";
		std::cout << "\n";
	}
};

class BACKOFF{
	int min_delay;
	int max_delay;
	int limit;
public:
	BACKOFF(int min_d, int max_d)
		: min_delay(min_d), max_delay(max_d), limit(min_d) {
		if (0 == limit) {
			std::cout << "Backoff min_delay cannot be zero.\n";
			exit(-1);
		}

	}
	void backoff() {
		auto delay = rand() % limit;
		limit += limit;
		if (limit > max_delay) limit = max_delay;
		//std::this_thread::sleep_for(std::chrono::microseconds(delay));
		for (int i = 0; i < delay; i++) _mm_pause();
	}
};

class LFBO_STACK {
	NODE* volatile top;
public:
	LFBO_STACK() {
		top = nullptr;
	}

	~LFBO_STACK() {
		clear();
	}

	void clear() {
		while (nullptr != top) pop();
	}

	bool CAS(NODE* volatile* addr, NODE* expected, NODE* new_value)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic<NODE*>*>(addr),
			&expected,
			new_value);
	}

	void push(int x)
	{
		BACKOFF bo(1, num_threads);
		NODE* new_node = new NODE(x);
		while (true) {
			auto last = top;
			new_node->next = last;
			if (true == CAS(&top, last, new_node))
				return;
			bo.backoff();
		}
	}

	int pop()
	{
		BACKOFF bo(1, num_threads);
		while (true) {
			auto last = top;
			if (nullptr == last) {
				return -2;
			}
			auto next = last->next;
			if (last != top) continue;
			int v = last->value;
			if (true == CAS(&top, last, next)) {
				// delete last;
				return v;
			}
			bo.backoff();
		}
	}

	void print20()
	{
		NODE* curr = top;
		for (int i = 0; i < 20 && curr != nullptr; i++, curr = curr->next)
			std::cout << curr->value << ", ";
		std::cout << "\n";
	}
};

constexpr int EX_EMPTY = 0;
constexpr int EX_WAITING = 1;
constexpr int EX_BUSY = 2;

class LockFreeExchanger {
	alignas(64) std::atomic_llong slot;
public:
	LockFreeExchanger() {
		slot = 0;
	}
	int exchange(int value,  bool* busy) {
		*busy = false;
		while (true) {
			long long curr_slot = slot;
			int value = (int)(curr_slot & 0xFFFFFFFF);
			int state = (int)((curr_slot >> 32) & 0x3);
			switch (state) {
			case EX_EMPTY: {
				long long new_slot = ((long long)value) | ((long long)EX_WAITING << 32);
				if (std::atomic_compare_exchange_strong(&slot, &curr_slot, new_slot)) {
					auto start_t = std::chrono::high_resolution_clock::now();
					while (true) {
						curr_slot = slot;
						state = (int)((curr_slot >> 32) & 0x3);
						if (state == EX_BUSY) {
							int ret_value = (int)(curr_slot & 0xFFFFFFFF);
							slot = 0;
							return ret_value;
						}
						auto curr_t = std::chrono::high_resolution_clock::now();
						auto dur = curr_t - start_t;
						size_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
						if (ms > 10) { // TIME OUT
							long long empty_slot = 0;
							if (std::atomic_compare_exchange_strong(&slot, &curr_slot, empty_slot)) {
								*busy = false;
								return -2; // TIME OUT
							}
							else {
								curr_slot = slot;
								int ret_value = (int)(curr_slot & 0xFFFFFFFF);
								slot = 0;
								return ret_value;
							}
						}
					}
				}
				break;
		}
			case EX_WAITING: {
				long long new_slot = ((long long)value) | ((long long)EX_BUSY << 32);
				if (std::atomic_compare_exchange_strong(&slot, &curr_slot, new_slot)) {
					return value;
				} else
					*busy = true;
				break;
			}
			case EX_BUSY: {
				*busy = true;
				break;
			}
	}
}

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
		if ((true == busy) && (old_range <= num_thread / 2 - 1))
			range, old_range + 1; // MAX RANGE is # of thread / 2
		return ret;
	}
};


class LFEL_STACK {
	NODE* volatile top;
	EliminationArray el_array;
public:
	LFEL_STACK() {
		top = nullptr;
	}

	~LFEL_STACK() {
		clear();
	}

	void clear() {
		while (nullptr != top) pop();
	}

	bool CAS(NODE* volatile* addr, NODE* expected, NODE* new_value)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic<NODE*>*>(addr),
			&expected,
			new_value);
	}

	void push(int x)
	{
		BACKOFF bo(1, num_threads);
		NODE* new_node = new NODE(x);
		while (true) {
			auto last = top;
			new_node->next = last;
			if (true == CAS(&top, last, new_node))
				return;
			el_array.visit();
		}
	}

	int pop()
	{
		BACKOFF bo(1, num_threads);
		while (true) {
			auto last = top;
			if (nullptr == last) {
				return -2;
			}
			auto next = last->next;
			if (last != top) continue;
			int v = last->value;
			if (true == CAS(&top, last, next)) {
				// delete last;
				return v;
			}
			el_array_visit();
		}
	}

	void print20()
	{
		NODE* curr = top;
		for (int i = 0; i < 20 && curr != nullptr; i++, curr = curr->next)
			std::cout << curr->value << ", ";
		std::cout << "\n";
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