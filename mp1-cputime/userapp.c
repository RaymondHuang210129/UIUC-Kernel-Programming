#include "userapp.h"

int main(int argc, char* argv[])
{
	int pid = getpid();
	FILE * fp;
	char * line_buf = NULL;
	size_t line_buf_size = 0;
	ssize_t line_size;
	fp = fopen("/proc/mp1/status", "w");
	fprintf(fp, "%d", pid);
	fclose(fp);
	unsigned long int start_time = (unsigned)time(NULL);
	while(1) 
	{
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
