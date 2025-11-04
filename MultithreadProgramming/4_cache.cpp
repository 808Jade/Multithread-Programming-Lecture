#include <iostream>
#include <thread>

volatile bool done = false;
volatile int* ptr = nullptr;

void update_ptr() {
	for (int i = 0; i < 2500'0000; ++i) {
		*ptr = -(1 + *ptr);
	}
	done = true;
}

void watch_ptr() {
	int error_count = 0;
	while (!done) {
		int val = *ptr;
		if (val != 0 && val != -1) {
			error_count++;
			printf("%x ", val);
		}
	}
	std::cout << "Cache Coherence Errors : " << error_count << std::endl;
}


int main()
{
	int value[32];
	long long addr = (long long)&value[31];
	addr = addr - (addr % 64); // align to cache line
	addr = addr - 2;
	ptr = (int*)addr;
	*ptr = 0;

	std::thread t1(watch_ptr);
	std::thread t2(update_ptr);
	t1.join();
	t2.join();

	return 0;
}