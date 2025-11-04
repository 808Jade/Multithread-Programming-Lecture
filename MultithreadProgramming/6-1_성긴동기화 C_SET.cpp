#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <numeric>

const int MAX_THREADS = 32;

class NODE {
public:
	int value;
	NODE* next;
	std::mutex mtx;
	NODE(int x) : next(nullptr), value(x) {}
	void lock() { mtx.lock(); }
	void unlock() { mtx.unlock(); }
};

class C_SET {
private:
	NODE* head, * tail;
	std::mutex mtx;
public:
	C_SET() {
		head = new NODE(std::numeric_limits<int>::min());
		tail = new NODE(std::numeric_limits<int>::max());
		head->next = tail;
	}

	~C_SET()
	{
		clear();
		delete head;
		delete tail;
	}

	void clear()
	{
		mtx.lock();
		NODE* curr = head->next;
		while (curr != tail) {
			NODE* temp = curr;
			curr = curr->next;
			delete temp;
		}
		head->next = tail;
		mtx.unlock();
	}

	bool add(int x)
	{
		mtx.lock();

		auto prev = head; // Critical section이 아니다! lock 밖으로 뺄 수 있는 건 빼는게 좋다.
		auto curr = prev->next;

		while (curr->value < x) {
			prev = curr;
			curr = curr->next;
		}

		if (curr->value == x) {
			mtx.unlock();
			return false;
		}
		else {
			auto newNode = new NODE(x);
			newNode->next = curr;
			prev->next = newNode;
			mtx.unlock();
			return true;
		}
	}

	bool remove(int x)
	{
		mtx.lock();
		auto prev = head;
		auto curr = prev->next;

		while (curr->value < x) {
			prev = curr;
			curr = curr->next;
		}
		if (curr->value == x) {
			prev->next = curr->next;
			delete curr; // 역시 Critical section이 아니다
			mtx.unlock();
			return true;
		}
		else {
			mtx.unlock();
			return false;
		}
	}

	bool contains(int x)
	{
		mtx.lock();
		auto prev = head;
		auto curr = prev->next;

		while (curr->value < x) {
			prev = curr;
			curr = curr->next;
		}
		if (curr->value == x) {
			mtx.unlock();
			return true;

		}
		else {
			mtx.unlock();
			return false;
		}

	}

	void print20()
	{
		auto curr = head->next;
		for (int i = 0; i < 20 && curr != tail; ++i) {
			std::cout << curr->value << ", ";
			curr = curr->next;
		}
		std::cout << std::endl;
	}
};

C_SET set;

void benchmark(const int num_threads)
{
	const int LOOP = 400'0000 / num_threads;
	const int RANGE = 1000;

	for (int i = 0; i < LOOP; ++i) {
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
	for (int num_thread = MAX_THREADS; num_thread >= 1; num_thread /= 2) {
		set.clear();
		std::vector<std::thread> threads;
		auto start = high_resolution_clock::now();
		for (int i = 0; i < num_thread; ++i)
			threads.emplace_back(benchmark, num_thread);
		for (auto& th : threads)
			th.join();
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<milliseconds>(stop - start);
		std::cout << "Threads: " << num_thread << ", Duration: " << duration.count() << " ms.\n";
		std::cout << "Set: "; set.print20();
	}
}