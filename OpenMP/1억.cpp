#include <omp.h>
#include <stdio.h>
int main()
{
	int sum = 0;
#pragma omp parallel
	{
		int nthreads = omp_get_num_threads();
		printf("Number of threads = %d\n", nthreads);
		for (int i = 0; i < 50000000 / nthreads; ++i)
#pragma omp critical
			sum = sum + 2;
	}
	printf("sum : %d", sum);
}