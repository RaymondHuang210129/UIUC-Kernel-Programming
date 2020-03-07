#include "userapp.h"

int main(int argc, char* argv[]) 
{
    int period = atoi(argv[1]);
    int comp_time = atoi(argv[2]);
    
    int pid = getpid();
	FILE * fp;
	char * line_buf = NULL;
    char expected_buf[50];
	size_t line_buf_size = 0;
	ssize_t line_size;
    ssize_t expected_size = 0;
    int is_registered = 0;
    struct timeval start_time, end_time;
	int round, i;
	unsigned long factorial = 1;
	
    /* section: register the process */
    fp = fopen("/proc/mp2/status", "w");
	fprintf(fp, "R, %d, %d, %d", pid, period, comp_time);
    printf("R, %d, %d, %d", pid, period, comp_time);
	fclose(fp);

    /* section: check whether is success or not */
    fp = fopen("/proc/mp2/status", "r");
    if (fp == NULL) 
    {
        printf("errno: %d", errno);
        return 0;
    }
	while (getline(&line_buf, &line_buf_size, fp) != EOF)
	{
		expected_size = sprintf(expected_buf, "%d %d %d", pid, period, comp_time);
        if (strncmp(line_buf, expected_buf, expected_size) == 0)
        {
            is_registered = 1;
            break;
        }
	}
    fclose(fp);
    if (!is_registered)
    {
        printf("unable to register this process.\n");
        return 0;
    }

    /* section: yield */
    printf("go to sleep\n");

    fp = fopen("/proc/mp2/status", "w");
	fprintf(fp, "Y, %d", pid);
    fclose(fp);

    printf("wake up -------------- %d\n", pid);
    system("cat /proc/uptime");

    /* section: job */
    for (round = 0; round < 5; round++) 
    {
        /* section: start factorial computation */
        gettimeofday(&start_time, NULL);
        factorial = 1;
        
        for (i = 1; i < 50000 * comp_time; i++) 
        {
            factorial *= i;
            gettimeofday(&end_time, NULL);
            //if ((end_time.tv_sec * 1000000 + end_time.tv_usec) - (start_time.tv_sec * 1000000 + start_time.tv_usec) > (comp_time * 1000))
            //{
            //    printf("round %d factorial: %d!\n", round, i);
            //    //printf("process has waken for %f second\n", (end_time.tv_sec + end_time.tv_usec * 0.000001) - (start_time.tv_sec+ start_time.tv_usec * 0.000001));
            //    break;
            //}
        }

        /* section: yield */
        system("cat /proc/uptime");
        printf("go to sleep ------------------ %d\n", pid);

        fp = fopen("/proc/mp2/status", "w");
	    fprintf(fp, "Y, %d", pid);
        fclose(fp);

        printf("wake up ------------------ %d\n", pid);
        system("cat /proc/uptime");
    }

    fp = fopen("/proc/mp2/status", "w");
	fprintf(fp, "D, %d", pid);
	
    return 0;
}