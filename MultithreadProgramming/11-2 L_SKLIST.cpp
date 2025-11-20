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
	SKNODE* volatile next[MAX_LEVEL + 1];
	int top_level;
	volatile bool marked;
	volatile bool fully_linked;
	std::recursive_mutex mtx;

	SKNODE(int x, int top) : value(x), top_level(top), marked(false), fully_linked(false)
	{
		for (auto& p : next) p = nullptr;
	}
	SKNODE() : value(-1), top_level(0), marked(false), fully_linked(false) {
		for (auto& p : next) p = nullptr;
	}
};


class L_SKLIST {
private:
	SKNODE* head, * tail;
public:
	L_SKLIST() {
		head = new SKNODE(std::numeric_limits<int>::min(), MAX_LEVEL);
		tail = new SKNODE(std::numeric_limits<int>::max(), MAX_LEVEL);
		for (auto& p : head->next) p = tail;
		head->fully_linked = tail->fully_linked = true;
	}

	~L_SKLIST()
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

	int find(SKNODE* prevs[], SKNODE* currs[], int x)
	{
		int max_level_found = -1;
		auto prev = head;
		for (int level = MAX_LEVEL; level >= 0; --level) {
			auto curr = prev->next[level];
			while (curr->value < x) {
				prev = curr;
				curr = curr->next[level];
			}
			if (max_level_found == -1 && curr->value == x)
				max_level_found = level;
			prevs[level] = prev;
			currs[level] = curr;
		}
		return max_level_found;
	}

	bool add(int x)
	{
		int top_level = 0;
		for (top_level = 0; top_level < MAX_LEVEL; ++top_level) {
			if (rand() % 2 == 0) break;
		}

		while (true) {
			SKNODE* prevs[MAX_LEVEL + 1];
			SKNODE* currs[MAX_LEVEL + 1];

			int f_level = find(prevs, currs, x);

			if (f_level != -1) {
				SKNODE* node_found = currs[f_level];
				if (!node_found->marked) {
					while (!node_found->fully_linked) {}
					return false;
				}
				continue;
			}

			// Locking
			int highest_locked = -1;
			bool valid = true;
			for (int level = 0; level <= top_level; ++level) {
				prevs[level]->mtx.lock();
				highest_locked = level;

				valid = !prevs[level]->marked 
					 && !currs[level]->marked 
					 && prevs[level]->next[level] == currs[level];
			}

			if (false == valid) {
				for (int i = 0; i <= highest_locked; ++i)
					prevs[i]->mtx.unlock();
				continue;
			}

			SKNODE* newNode = new SKNODE(x, top_level);
			for (int level = 0; level <= top_level; ++level)
				newNode->next[level] = currs[level];
			for (int level = 0; level <= top_level; ++level)
				prevs[level]->next[level] = newNode;
			newNode->fully_linked = true;

			// -finally
			for (int i = 0; i <= highest_locked; ++i)
				prevs[i]->mtx.unlock();
			return true;
		}
	}

	bool remove(int x)
	{
		SKNODE* prevs[MAX_LEVEL + 1];
		SKNODE* currs[MAX_LEVEL + 1];

		while (true) {
			int f_level = find(prevs, currs, x);
			if (f_level == -1) return false;
			// Marking
			SKNODE* victim = currs[f_level];
			if (victim->marked) return false;
			if (!victim->fully_linked) return false;
			if (victim->top_level != f_level) return false;

			int top_level = victim->top_level;
			victim->mtx.lock();
			if (victim->marked) {
				victim->mtx.unlock();
				return false;
			}

			victim->marked = true;
		
			bool valid = true;
			
			int highest_locked = -1;
			for (int i = 0; i <= top_level; ++i) {
				prevs[i]->mtx.lock();
				highest_locked = i;
				valid = !prevs[i]->marked && prevs[i]->next[i] == victim;
			}
			if (false == valid) {
				for (int i = 0; i <= highest_locked; ++i)
					prevs[i]->mtx.unlock();
				continue;
			}
			for (int i = top_level; i >= 0; --i) {
				prevs[i]->next[i] = victim->next[i];
			}
			for (int i = highest_locked; i >= 0; --i)
				prevs[i]->mtx.unlock();
			victim->mtx.unlock();
			// delete victim;
			return true;
		}
	}

	bool contains(int x)
	{
		SKNODE* prevs[MAX_LEVEL + 1];
		SKNODE* currs[MAX_LEVEL + 1];
		int f_level = find(prevs, currs, x);
		return (f_level != -1)
			&& (currs[f_level]->fully_linked == true)
			&& (currs[f_level]->marked == false);
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


L_SKLIST set;

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