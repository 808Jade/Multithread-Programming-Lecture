#include <iostream>
#include <random>
#include <print>

int 정렬방법(const void* a, const void* b)
{
	return *(char*)a - *(char*)b;
}

//---------
int main()
//---------
{

	// [문제] qsort를 사용하여 오름차순 정렬하라
	char pangram[]{ "the quick brown fox jumps over the lazy dog" };

	qsort(pangram, (sizeof(pangram) - sizeof(char)) / sizeof(char), sizeof(char), 정렬방법);

	std::cout << pangram << '\n';
}
