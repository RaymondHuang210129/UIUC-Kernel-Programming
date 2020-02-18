#include "userapp.h"

int main(int argc, char* argv[])
{
	int pid = getpid();
	FILE * fp;
	char * line_buf = NULL;
	size_t line_buf_size = 0;
	ssize_t line_size;
	int i;
	unsigned long factorial = 1;
	fp = fopen("/proc/mp1/status", "w");
	fprintf(fp, "%d", pid);
	fclose(fp);
	unsigned long int start_time = (unsigned)time(NULL);
	for (i = 1;; i++) {
		factorial *= i;
		unsigned long int current_time = (unsigned)time(NULL);
		if (current_time - start_time >= 20) break;  
	}
	fp = fopen("/proc/mp1/status", "r");
	while(getline(&line_buf, &line_buf_size, fp) != EOF)
	{
		printf("%s", line_buf);
	}


	return 0;
}
