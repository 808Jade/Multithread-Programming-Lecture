#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <numeric>

const int MAX_THREADS = 16;

#include <queue>

class LF_NODE;

class AMR { // Atomic Markable Reference
	volatile long long ptr_and_mark;
public:
	AMR(LF_NODE* ptr = nullptr, bool mark = false) {
		long long val = reinterpret_cast<long long>(ptr);
		if (0 != (val & 1)) {  // ¼öÁ¤!!!!!
			std::cout << "ERROR"; exit(-1);
		}
		if (true == mark) val |= 1;
		ptr_and_mark = val;
	}

	LF_NODE* get_ptr() {
		long long val = ptr_and_mark;
		return reinterpret_cast<LF_NODE*>(val & 0xFFFFFFFFFFFFFFFE);
	}
	bool get_mark() {
		return (1 == (ptr_and_mark & 1));
	}
	LF_NODE* get_ptr_and_mark(bool* mark) {
		long long val = ptr_and_mark;
		*mark = (1 == (val & 1));
		return reinterpret_cast<LF_NODE*>(val & 0xFFFFFFFFFFFFFFFE);
	}

	bool attempt_mark(LF_NODE* expected_ptr, bool new_mark)
	{
		return CAS(expected_ptr, expected_ptr,
			false, new_mark);
	}

	bool CAS(LF_NODE* expected_ptr, LF_NODE* new_ptr,
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

class LF_NODE {
public:
	int value;
	AMR next;
	int epoch; // for EBR
	LF_NODE(int x) : value(x) {}
};

int num_threads = 0;

thread_local int thread_id = 0;

class EBR {
private:
	std::queue<LF_NODE*> free_list[MAX_THREADS];
	std::atomic<int> epoch_counter;
	struct THREAD_COUNTER {
		alignas(64) std::atomic<int> local_epoch;
	};
	THREAD_COUNTER thread_counter[MAX_THREADS];
public:
	EBR() {}
	~EBR() {
		recycle();
	}
	void recycle() {
		for (int i = 0; i < MAX_THREADS; ++i) {
			while (false == free_list[i].empty()) {
				auto node = free_list[i].front();
				free_list[i].pop();
				delete node;
			}
		}
	}

	void delete_node(LF_NODE* node) {
		free_list[thread_id].push(node);
	}

	LF_NODE* new_node(int x) {
		if (false == free_list[thread_id].empty()) {
			auto node = free_list[thread_id].front();
			bool can_reuse = true;
			for (int i = 0; i < num_threads; ++i) {
				if (i == thread_id) continue;
				if (thread_counter[i].local_epoch <= node->epoch)
					can_reuse = false;
				break;
			}
			if (true == can_reuse) {
				free_list[thread_id].pop();
				node->value = x;
				node->next = nullptr;
				return node;
			}
		}
		return new LF_NODE(x);
	}
};

class LF_SET_EBR {
private:
	EBR ebr;
	LF_NODE* head, * tail;
public:
	LF_SET_EBR() {
		head = new LF_NODE(std::numeric_limits<int>::min());
		tail = new LF_NODE(std::numeric_limits<int>::max());
		head->next = AMR(tail, false);
	}

	~LF_SET_EBR()
	{
		clear();
		delete head;
		delete tail;
	}

	void clear()
	{
		LF_NODE* curr = head->next.get_ptr();
		while (curr != tail) {
			LF_NODE* temp = curr;
			curr = curr->next.get_ptr();
			delete temp;
		}
		head->next = AMR(tail, false);
	}

	void find(LF_NODE*& prev, LF_NODE*& curr, int x)
	{
		while (true) {
		retry:
			prev = head;
			curr = prev->next.get_ptr();
			while (true) {
				bool curr_mark;
				auto succ = curr->next.get_ptr_and_mark(&curr_mark);
				while (true == curr_mark) {
					if (false == prev->next.CAS(curr, succ, false, false))
						goto retry;
					ebr.delete_node(curr);
					curr = succ;
					succ = curr->next.get_ptr_and_mark(&curr_mark);
				}
				if (curr->value >= x)
					return;
				prev = curr;
				curr = succ;
			}
		}
	}

	bool add(int x)
	{
		while (true) {
			LF_NODE* prev, * curr;
			find(prev, curr, x);

			if (curr->value == x) {
				return false;
			}
			else {
				auto newNode = ebr.new_node(x);
				newNode->next = AMR(curr, false);
				if (true == prev->next.CAS(curr, newNode, false, false))
					return true;
				else ebr.delete_node(newNode);
			}
		}
	}

	bool remove(int x)
	{
		while (true) {
			LF_NODE* prev, * curr;
			find(prev, curr, x);

			if (curr->value != x) {
				return false;
			}
			else {
				auto succ = curr->next.get_ptr();
				if (false == curr->next.attempt_mark(succ, true))
					continue;
				if (true == prev->next.CAS(curr, succ, false, false))
					ebr.delete_node(curr);
				return true;
			}
		}
	}

	bool contains(int x)
	{
		auto curr = head->next.get_ptr();
		while (curr->value < x)
			curr = curr->next.get_ptr();
		return (curr->value == x) && (curr->next.get_mark() == false);
	}

	void print20()
	{
		auto curr = head->next.get_ptr();
		for (int i = 0; i < 20 && curr != tail; ++i) {
			std::cout << curr->value << ", ";
			curr = curr->next.get_ptr();
		}
		std::cout << std::endl;
	}
};

LF_SET_EBR set;

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
	//std::cout << "Consistency Check\n";

	//for (num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
	//	set.clear();
	//	std::vector<std::thread> threads;
	//	for (int i = 0; i < MAX_THREADS; ++i)
	//		history[i].clear();
	//	auto start = high_resolution_clock::now();
	//	for (int i = 0; i < num_threads; ++i)
	//		threads.emplace_back(benchmark_check, num_threads, i);
	//	for (auto& th : threads)
	//		th.join();
	//	auto stop = high_resolution_clock::now();
	//	auto duration = duration_cast<milliseconds>(stop - start);
	//	std::cout << "Threads: " << num_threads
	//		<< ", Duration: " << duration.count() << " ms.\n";
	//	std::cout << "Set: "; set.print20();
	//	check_history(num_threads);
	//	//set.recycle();
	//}
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