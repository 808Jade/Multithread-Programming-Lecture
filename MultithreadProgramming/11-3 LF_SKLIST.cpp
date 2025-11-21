#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <numeric>
#include <set>

const int MAX_THREADS = 16;

int num_threads = 0;
thread_local int thread_id = 0;

constexpr int MAX_LEVEL = 9;

class LFSKNODE;
class AMRSK { // Atomic Markable Reference
	volatile long long ptr_and_mark;
public:
	AMRSK(LFSKNODE* ptr = nullptr, bool mark = false) {
		long long val = reinterpret_cast<long long>(ptr);
		if (0 != (val & 1)) {  // ¼öÁ¤!!!!!
			std::cout << "ERROR"; exit(-1);
		}
		if (true == mark) val |= 1;
		ptr_and_mark = val;
	}

	LFSKNODE* get_ptr() {
		long long val = ptr_and_mark;
		return reinterpret_cast<LFSKNODE*>(val & 0xFFFFFFFFFFFFFFFE);
	}
	bool get_mark() {
		return (1 == (ptr_and_mark & 1));
	}
	LFSKNODE* get_ptr_and_mark(bool* mark) {
		long long val = ptr_and_mark;
		*mark = (1 == (val & 1));
		return reinterpret_cast<LFSKNODE*>(val & 0xFFFFFFFFFFFFFFFE);
	}

	bool attempt_mark(LFSKNODE* expected_ptr, bool new_mark)
	{
		return CAS(expected_ptr, expected_ptr,
			false, new_mark);
	}

	bool CAS(LFSKNODE* expected_ptr, LFSKNODE* new_ptr,
		bool expected_mark, bool new_mark)
	{
		long long expected_val
			= reinterpret_cast<long long>(expected_ptr);
		if (true == expected_mark) expected_val |= 1;
		long long new_val
			= reinterpret_cast<long long>(new_ptr);
		if (true == new_mark) new_val |= 1;
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic<long long> *>(&ptr_and_mark),
			&expected_val, new_val);
	}

};

class LFSKNODE {
public:
	int value;
	AMRSK next[MAX_LEVEL + 1];
	int top_level;
	LFSKNODE(int x, int top) : value(x), top_level(top)
	{
		for (auto& p : next) p = nullptr;
	}
	LFSKNODE() : value(-1), top_level(0)
	{
		for (auto& p : next) p = nullptr;
	}
};

class LF_SKLIST {
private:
	LFSKNODE* head, * tail;
public:
	LF_SKLIST() {
		head = new LFSKNODE(std::numeric_limits<int>::min(), MAX_LEVEL);
		tail = new LFSKNODE(std::numeric_limits<int>::max(), MAX_LEVEL);
		for (auto& p : head->next) p = tail;
	}

	~LF_SKLIST()
	{
		clear();
		delete head;
		delete tail;
	}

	void clear()
	{
		LFSKNODE* curr = head->next[0].get_ptr();
		while (curr != tail) {
			LFSKNODE* temp = curr;
			curr = curr->next[0].get_ptr();
			delete temp;
		}
		for (auto& p : head->next) p = tail;
	}

	bool find(LFSKNODE* prevs[], LFSKNODE* currs[], int x)
	{
	retry:
		auto prev = head;
		for (int level = MAX_LEVEL; level >= 0; --level) {
			auto curr = prev->next[level].get_ptr();
			while (true) {
				bool removed;
				auto succ = curr->next[level].get_ptr_and_mark(&removed);
				while (true == removed) {
					if (false == prev->next[level].CAS(curr, succ, false, false))
						goto retry;
					curr = succ;
					succ = curr->next[level].get_ptr_and_mark(&removed);
				}
				if (curr->value < x) {
					prev = curr;
					curr = succ;
				}
				else break;
			}
			prevs[level] = prev;
			currs[level] = curr;
		}
		return currs[0]->value == x;
	}

	bool add(int x)
	{
		LFSKNODE* prevs[MAX_LEVEL + 1];
		LFSKNODE* currs[MAX_LEVEL + 1];

		int top_level = 0;
		for (top_level = 0; top_level < MAX_LEVEL; ++top_level) {
			if (rand() % 2 == 0) break;
		}

		while (true) {
			if (find(prevs, currs, x))
				return false;

			LFSKNODE* newNode = new LFSKNODE(x, top_level);
			for (int i = 0; i <= top_level; ++i) {
				newNode->next[i] = AMRSK(currs[i], false);
			}
			newNode->next[0] = AMRSK(currs[0], false);
			if (not prevs[0]->next[0].CAS(currs[0], newNode, false, false))
				continue;
			for (int i = 1; i < top_level; ++i) {
				while (true) {
					if (prevs[i]->next[i].CAS(currs[i], newNode, false, false))
						break;
					find(prevs, currs, x);
				}
			}
			return true;
		}
	}

	bool remove(int x)
	{
		LFSKNODE* prevs[MAX_LEVEL + 1];
		LFSKNODE* currs[MAX_LEVEL + 1];

		if (false == find(prevs, currs, x))
			return false;
		auto victim = currs[0];
		int top_level = victim->top_level;

		for (int level = top_level; level >= 1; --level) {
			bool removed = false;
			auto succ = victim->next[level].get_ptr_and_mark(&removed);
			while (false == removed) {
				victim->next[level].CAS(succ, succ, false, true);
				succ = victim->next[level].get_ptr_and_mark(&removed);
			}
		}
		bool removed = false;
		auto succ = victim->next[0].get_ptr_and_mark(&removed);
		while (true) {
			bool i_marked_it = victim->next[0].CAS(succ, succ, false, true);
			succ = victim->next[0].get_ptr_and_mark(&removed);
			if (i_marked_it) {
				find(prevs, currs, x);
				return true;
			}
			else if (removed) {
				return false;
			}
		}
	}

	bool contains(int x)
	{
		LFSKNODE* prev = head;
		LFSKNODE* curr = nullptr;
		bool removed;

		for (int i = MAX_LEVEL; i >= 0; --i) {
			curr = prev->next[i].get_ptr();
			while (true) {
				auto succ = curr->next[i].get_ptr_and_mark(&removed);
				while (true == removed) {
					curr = curr->next[i].get_ptr();
					succ = curr->next[i].get_ptr_and_mark(&removed);
				}
				if (curr->value < x) {
					prev = curr;
					curr = succ;
				}
				else break;
			}
		}
		return curr->value == x;
	}

	void print20()
	{
		auto curr = head->next[0].get_ptr();
		for (int i = 0; i < 20 && curr != tail; ++i) {
			std::cout << curr->value << ", ";
			curr = curr->next[0].get_ptr();
		}
		std::cout << std::endl;
	}
};


LF_SKLIST set;

const int LOOP = 400'0000;
const int RANGE = 1000;

#include <array>

class HISTORY {
public:
	int op;
	int i_value;
	bool o_value;
	HISTORY(int o, int i, bool re) : op(o), i_value(i), o_value(re) {}
};

std::array<std::vector<HISTORY>, MAX_THREADS> history;

void check_history(int num_threads)
{
	std::array <int, RANGE> survive = {};
	std::cout << "Checking Consistency : ";
	if (history[0].size() == 0) {
		std::cout << "No history.\n";
		return;
	}
	for (int i = 0; i < num_threads; ++i) {
		for (auto& op : history[i]) {
			if (false == op.o_value) continue;
			if (op.op == 3) continue;
			if (op.op == 0) survive[op.i_value]++;
			if (op.op == 1) survive[op.i_value]--;
		}
	}
	for (int i = 0; i < RANGE; ++i) {
		int val = survive[i];
		if (val < 0) {
			std::cout << "ERROR. The value " << i << " removed while it is not in the set.\n";
			exit(-1);
		}
		else if (val > 1) {
			std::cout << "ERROR. The value " << i << " is added while the set already have it.\n";
			exit(-1);
		}
		else if (val == 0) {
			if (set.contains(i)) {
				std::cout << "ERROR. The value " << i << " should not exists.\n";
				exit(-1);
			}
		}
		else if (val == 1) {
			if (false == set.contains(i)) {
				std::cout << "ERROR. The value " << i << " shoud exists.\n";
				exit(-1);
			}
		}
	}
	std::cout << " OK\n";
}

void benchmark_check(int num_threads, int th_id)
{
	thread_id = th_id;
	for (int i = 0; i < LOOP / num_threads; ++i) {
		int op = rand() % 3;
		switch (op) {
		case 0: {
			int v = rand() % RANGE;
			history[th_id].emplace_back(0, v, set.add(v));
			break;
		}
		case 1: {
			int v = rand() % RANGE;
			history[th_id].emplace_back(1, v, set.remove(v));
			break;
		}
		case 2: {
			int v = rand() % RANGE;
			history[th_id].emplace_back(2, v, set.contains(v));
			break;
		}
		}
	}
}
void benchmark(const int num_threads, int th_id)
{
	thread_id = th_id;
	for (int i = 0; i < LOOP / num_threads; ++i) {
		int value = rand() % RANGE;
		int op = rand() % 3;
		if (op == 0) set.add(value);
		else if (op == 1) set.remove(value);
		else set.contains(value);
	}
}

int main()
{
	using namespace std::chrono;
	// Consistency check
	std::cout << "Consistency Check\n";
	for (num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
		set.clear();
		std::vector<std::thread> threads;
		for (int i = 0; i < MAX_THREADS; ++i)
			history[i].clear();
		auto start = high_resolution_clock::now();
		for (int i = 0; i < num_threads; ++i)
			threads.emplace_back(benchmark_check, num_threads, i);
		for (auto& th : threads)
			th.join();
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<milliseconds>(stop - start);
		std::cout << "Threads: " << num_threads
			<< ", Duration: " << duration.count() << " ms.\n";
		std::cout << "Set: "; set.print20();
		check_history(num_threads);
		//set.recycle();
	}
	std::cout << "\nBenchmarking\n";
	for (num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
		set.clear();
		std::vector<std::thread> threads;
		auto start = high_resolution_clock::now();
		for (int i = 0; i < num_threads; ++i)
			threads.emplace_back(benchmark, num_threads, i);
		for (auto& th : threads)
			th.join();
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<milliseconds>(stop - start);
		std::cout << "Threads: " << num_threads
			<< ", Duration: " << duration.count() << " ms.\n";
		std::cout << "Set: "; set.print20();
		//set.recycle();
	}
}