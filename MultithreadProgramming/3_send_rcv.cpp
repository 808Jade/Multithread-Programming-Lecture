#include <iostream>
#include <thread>
#include <mutex>

volatile int g_data = 0;
volatile bool g_ready = false;
std::mutex sr_lock;

void recv()
{
	while (!g_ready);
	std::cout << "data: " << g_data << std::endl;
}

void send()
{
	int temp = 0;
	std::cin >> temp;
	g_data = temp;
	g_ready = true;
}

int main()
{
	std::thread t1{ recv };
	std::thread t2{ send };

	t1.join();
	t2.join();
}