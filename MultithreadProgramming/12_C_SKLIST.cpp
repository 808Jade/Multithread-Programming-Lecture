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

class SKNODE {
public:
	int value;
	SKNODE* next[MAX_LEVEL + 1];
	int top_level;
	SKNODE(int x, int top) : value(x), top_level(top)
	{
		for (auto& p : next) p = nullptr;
	}
	SKNODE() : value(-1), top_level(0) {
		for (auto& p : next) p = nullptr;
	}
};

class C_SKLIST {
private:
	SKNODE* head, * tail;
	std::mutex mtx;
public:
	C_SKLIST() {
		head = new SKNODE(std::numeric_limits<int>::min(), MAX_LEVEL);
		tail = new SKNODE(std::numeric_limits<int>::max(), MAX_LEVEL);
		for (auto& p : head->next) p = tail;
	}

	~C_SKLIST()
	{
		clear();
		delete head;
		delete tail;
	}

	void clear()
	{
		SKNODE* curr = head->next[0];
		while (curr != tail) {
			SKNODE* temp = curr;
			curr = curr->next[0];
			delete temp;
		}
		for (auto& p : head->next) p = tail;
	}

	void find(SKNODE* prevs[], SKNODE* currs[], int x)
	{
		auto prev = head;
		for (int level = MAX_LEVEL; level >= 0; --level) {
			auto curr = prev->next[level];
			while (curr->value < x) {
				prev = curr;
				curr = curr->next[level];
			}
			prevs[level] = prev;
			currs[level] = curr;
		}
	}

	bool add(int x)
	{
		SKNODE* prevs[MAX_LEVEL + 1];
		SKNODE* currs[MAX_LEVEL + 1];
		mtx.lock();
		find(prevs, currs, x);

		if (currs[0]->value == x) {
			mtx.unlock();
			return false;
		}
		else {
			int node_level = 0;
			for (node_level = 0; node_level < MAX_LEVEL; ++node_level)
				if (rand() % 2 == 0) break;
			auto newNode = new SKNODE(x, node_level);

			for (int level = 0; level <= node_level; ++level) {
				newNode->next[level] = currs[level];
				prevs[level]->next[level] = newNode;
			}
			mtx.unlock();
			return true;
		}
	}

	bool remove(int x)
	{
		SKNODE* prevs[MAX_LEVEL + 1];
		SKNODE* currs[MAX_LEVEL + 1];
		mtx.lock();
		find(prevs, currs, x);

		if (currs[0]->value != x) {
			mtx.unlock();
			return false;
		}
		else {
			int node_level = currs[0]->top_level;
			for (int level = 0; level <= node_level; ++level) {
				prevs[level]->next[level] = currs[level]->next[level];
			}
			mtx.unlock();
			delete currs[0];
			return true;
		}
	}

	bool contains(int x)
	{
		SKNODE* prevs[MAX_LEVEL + 1];
		SKNODE* currs[MAX_LEVEL + 1];
		mtx.lock();
		find(prevs, currs, x);
		bool res = (currs[0]->value == x);
		mtx.unlock();
		return res;
	}

	void print20()
	{
		auto curr = head->next[0];
		for (int i = 0; i < 20 && curr != tail; ++i) {
			std::cout << curr->value << ", ";
			curr = curr->next[0];
		}
		std::cout << std::endl;
	}
};

C_SKLIST set;

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