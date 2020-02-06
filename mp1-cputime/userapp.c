#include "userapp.h"

int main(int argc, char* argv[])
{
	int pid = getpid();
	FILE * fp;
	fp = fopen("/proc/mp1/status", "w");
	fprintf(fp, "%d", pid);
	fclose(fp);
	return 0;
}
