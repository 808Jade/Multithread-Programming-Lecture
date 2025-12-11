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
	NODE(int x) : next(nullptr), value(x) {}
};

class C_QUEUE {
private:
	NODE* head, * tail;
	std::mutex mtx;
public:
	C_QUEUE() {
		head = tail = new NODE(-1);
	}

	~C_QUEUE() {
		clear();
		delete head;
		// 보초 노드는 남아있어야 함
	}

	void clear() {
		NODE* curr = head->next;
		while (nullptr != curr) {
			NODE* next = curr->next;
			delete curr;
			curr = next;
		}
		tail = head;
		head->next = nullptr;
	}

	void enqueue(int x) {
		NODE* new_node = new NODE(x);
		mtx.lock();
		tail->next = new_node;
		tail = new_node;
		mtx.unlock();
	}

	int dequeue() {
		NODE* temp;
		mtx.lock();
		if (nullptr == head->next) {
			mtx.unlock();
			return -1;
		}
		int result = head->next->value;
		temp = head;
		head = head->next;
		mtx.unlock();
		delete temp;
		return result;	// dequeue 할 게 없을 땐? - err(-1) 리턴하고 빠져나간다 (완전 큐)
		//						 - enqueue 를 기다린다 (파샬 큐)
	}

	void print20() {
		NODE* curr = head->next;
		for (int i = 0; i < 20 && curr != nullptr; ++i) {
			std::cout << curr->value << ", ";
			curr = curr->next;
		}
		std::cout << std::endl;
	}
};

C_QUEUE my_queue;

const int LOOP = 1000'0000;

void benchmark(const int num_threads, int th_id)
{
	const int loop_count = LOOP / num_threads;

	int key = 0;
	for (int i = 0; i < loop_count; i++) {
		if ((i < 32) || (rand() % 2 == 0)) // empty queue 에 대한 dequeue 가 많이 발생하깅때문에 여유를 둔다
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
		std::vector<std::thread> threads;
		auto start = high_resolution_clock::now();
		for (int i = 0; i < num_threads; ++i)
			threads.emplace_back(benchmark, num_threads, i);
		for (auto& th : threads)
			th.join();
		auto stop = high_resolution_clock::now();
		auto duration = duration_cast<milliseconds>(stop - start);
		std::cout << "Threads: " << num_threads << ", Duration: " << duration.count() << " ms.\n";
		std::cout << "Result: "; my_queue.print20();
	}
}