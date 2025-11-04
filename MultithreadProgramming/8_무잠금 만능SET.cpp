#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <numeric>
#include <set>
#include <queue>

const int MAX_THREADS = 32;

int num_threads = 0;
thread_local int thread_id = 0;

class DUMMY_MTX {
public:
	void lock() {}
	void unlock() {}
};

// 이 아래부터 STD_SET 코드
// 싱글 쓰레드 통합 API
enum INVO_OP { ADD = 0, REMOVE = 1, CONTAINS = 2 };
class INVOCATION { // 호출하고자 하는 원래 객체의 메소드와 그 입력값을 갖는 객체
public:
	INVO_OP op;
	int value;
	INVOCATION(INVO_OP o, int v) : op(o), value(v) {}
};

typedef bool RESPONSE; // 여러 메소드들의 결과 값의 타입을 압축한 객체

// 순자 객체 A
// 병렬화 하고자 하는 객체를 감싼 객체
// 호출 메소드를 apply하나로 통일
class SEQ_SET {
	std::set<int> m_set;
public:
	RESPONSE apply(INVOCATION inv) {
		switch (inv.op) {
		case ADD:
			return m_set.insert(inv.value).second;
		case REMOVE:
			return (m_set.erase(inv.value) > 0);
		case CONTAINS:
			return (m_set.find(inv.value) != m_set.end());
		default:
			return false;
		}
	}

	void clear() {
		m_set.clear();
	}

	void print20() {
		int count = 0;
		for (auto& v : m_set) {
			std::cout << v << ", ";
			if (++count >= 20) break;
		}
		std::cout << std::endl;
	}
};

class LNODE;

// for LFU_SET
// 합의 객체
// Node를 입력으로 받아 그 중 한 Node를 선발
// Node의 링크드 리스트
class CONSENSUS {
	LNODE* value{ nullptr };
public:
	LNODE* decide(LNODE* v)
	{
		CAS(&value, nullptr, v);
		return value;
	}
	void CAS(LNODE** addr, LNODE* expected, LNODE* update)
	{
		std::atomic_compare_exchange_strong(
			reinterpret_cast<std::atomic<LNODE*>*>(addr),
			&expected, update);
	}
	void clear()
	{
		value = nullptr;
	}
};

class LNODE {
public:
	INVOCATION m_inv;
	int	m_seq;
	LNODE* m_next;
	CONSENSUS decide_next;
	LNODE(INVOCATION inv) : m_inv(inv), m_seq(0), m_next(nullptr) {}
};

class LFU_SET {
	LNODE* head[MAX_THREADS];
	LNODE* tail;
public:
	LFU_SET() {
		tail = new LNODE(INVOCATION(CONTAINS, 0)); // dummy
		for (int i = 0; i < MAX_THREADS; ++i) {
			head[i] = tail;
		}
	}

	~LFU_SET()
	{
		while (nullptr != tail) {
			LNODE* temp = tail;
			tail = tail->m_next;
			delete temp;
		}
	}

	LNODE* max_head()
	{
		LNODE* max_node = head[0];
		for (int i = 1; i < num_threads; ++i) {
			if (max_node->m_seq < head[i]->m_seq)
				max_node = head[i];
		}
		return max_node;
	}

	RESPONSE apply(INVOCATION inv)
	{
		int i = thread_id;
		auto prefer = new LNODE(inv);
		while (prefer->m_seq == 0) {
			LNODE* before = max_head();
			LNODE* after = before->decide_next.decide(prefer);
			before->m_next = after;
			after->m_seq = before->m_seq + 1;
			head[i] = after;
		}

		SEQ_SET seq_set;
		LNODE* curr = tail->m_next;
		while (curr != prefer) {
			seq_set.apply(curr->m_inv);
			curr = curr->m_next;
		}
		return seq_set.apply(inv);
	};

	void clear()
	{
		for (int i = 0; i < num_threads; ++i) {
			head[i] = tail;
		}
		LNODE* curr = tail->m_next;
		while (nullptr != curr) {
			LNODE* temp = curr;
			curr = curr->m_next;
			delete temp;
		}
		tail->m_next = nullptr;
		tail->decide_next.clear();
	}

	void print20()
	{
		SEQ_SET seq_set;
		LNODE* curr = tail->m_next;
		while (nullptr != curr) {
			seq_set.apply(curr->m_inv);
			curr = curr->m_next;
		}
		seq_set.print20();
	}
};


// 벤치 마킹
class STD_SET {
private:
	//SEQ_SET m_set;
	LFU_SET m_set;
	DUMMY_MTX mtx;
public:
	STD_SET() {}

	~STD_SET() {}

	void clear()
	{
		m_set.clear();
	}

	bool add(int x)
	{
		mtx.lock();
		auto res = m_set.apply(INVOCATION(ADD, x));
		mtx.unlock();
		return res;
	}


	bool remove(int x)
	{
		mtx.lock();
		auto res = m_set.apply(INVOCATION(REMOVE, x));
		mtx.unlock();
		return res;
	}

	bool contains(int x)
	{
		mtx.lock();
		auto res = m_set.apply(INVOCATION(CONTAINS, x));
		mtx.unlock();
		return res;
	}

	void print20()
	{
		m_set.print20();
	}
};

STD_SET set;

const int LOOP = 1'0000;
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
	//std::cout << "\nBenchmarking\n";
	//for (num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
	//	set.clear();
	//	std::vector<std::thread> threads;
	//	auto start = high_resolution_clock::now();
	//	for (int i = 0; i < num_threads; ++i)
	//		threads.emplace_back(benchmark, num_threads, i);
	//	for (auto& th : threads)
	//		th.join();
	//	auto stop = high_resolution_clock::now();
	//	auto duration = duration_cast<milliseconds>(stop - start);
	//	std::cout << "Threads: " << num_threads
	//		<< ", Duration: " << duration.count() << " ms.\n";
	//	std::cout << "Set: "; set.print20();
	//	//set.recycle();
	//}
}