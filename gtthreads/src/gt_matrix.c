#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>
//#include <bits/mathcalls.h>

#include "gt_include.h"
#include<math.h>

#define ROWS 256
#define COLS ROWS
#define SIZE COLS

#define NUM_CPUS 2
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE/NUM_GROUPS)

#define NUM_THREADS 128
#define PER_THREAD_ROWS (SIZE/NUM_THREADS)

int size_array[4] = {32, 64, 128, 256};
int credit_array[4] = {25, 50, 75, 100};

int count = 0;

unsigned long total_exec_time[128];

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
    int size;
	
}uthread_arg_t;
	
struct timeval tv1;

static void generate_matrix(matrix_t *mat, int val)
{

	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{
			mat->m[i][j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++)
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}

	return;
}

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;
    //int size;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	//start_row = ptr->start_row;
	//end_row = (ptr->start_row + PER_THREAD_ROWS);

#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = (ptr->start_col + PER_THREAD_ROWS);
#else
	start_col = 0;
	end_col = SIZE;
#endif

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif
    //size = 32;
    //int temp;
    //temp = (int)(ptr->tid/32);
    //ptr->size = size_array[temp];
    int size;
    size = ptr->size;
    //if(ptr->tid==0) printf("!!!!!!!TEMP = %d!!!!!!!!!!!!",temp);
	for(i = 0; i < size; i++)
		for(j = 0; j < size; j++)
			for(k = 0; k < size; k++) {
                if(ptr->tid == 50 && i==1 && j==1 && k==1)  //add sched_policy
                {
                    usleep(20000);
                    gt_yield();
                }
                ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];
            }
#ifdef GT_THREADS
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	gettimeofday(&tv2,NULL);
	unsigned long temp;
	temp = tv2.tv_sec*1000000 + tv2.tv_usec - (tv1.tv_sec*1000000) - (tv1.tv_usec);
	//printf("TOTAL TIME = %lu AND THREAD ID = %d", temp, ptr->tid );
	total_exec_time[ptr->tid] = temp;
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif

#undef ptr
	return 0;
}

matrix_t A, B, C[NUM_THREADS];

static void init_matrices()
{
    int i;
    generate_matrix(&A, 1);
    generate_matrix(&B, 1);
    for (i = 0; i <NUM_THREADS; ++i) {
        generate_matrix(&C[i], 0);
    }
	return;
}


uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char* argv[])
{
	uthread_arg_t *uarg;
	int inx,iny,inz;

	gtthread_app_init();

	init_matrices();

	gettimeofday(&tv1,NULL);

    sched_policy =0;
    if(argc<2)
    {
        printf("Error : Specify argument for schedule policy\n");
        return 0;
    }

    if(atoi(argv[1])==1) sched_policy=1;     //Credit based
    //else sched_policy = 1
	int number_threads = 0;
	for(inx=0;inx<4;inx++)      //size
	{
		for (iny = 0; iny < 4; iny++)   //credit
		{
			for(inz =0; inz<8; inz++)    //threads with same size and credit
			{
				uarg = &uargs[number_threads];
				uarg->_A = &A;
				uarg->_B = &B;
				uarg->_C = &C[number_threads];

				uarg->tid = number_threads;
				uarg->gid = (number_threads % NUM_GROUPS);

				uarg->start_row = (number_threads * PER_THREAD_ROWS);

				uarg->size = size_array[inx];


#ifdef GT_GROUP_SPLIT
				/* Wanted to split the columns by groups !!! */
		uarg->start_col = (uarg->gid * PER_GROUP_COLS);
#endif

				uthread_create(&utids[number_threads], uthread_mulmat, uarg, uarg->gid,credit_array[iny]);
				number_threads++;
			}
		}
	}

	/*
	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = &uargs[inx];
		uarg->_A = &A;
        uarg->_B = &B;
		uarg->_C = &C[inx];

		uarg->tid = inx;

        int temp;
        temp = (int)(uarg->tid/32);
        uarg->size = size_array[temp];
        printf("\nSIZE = %d", uarg->size);

		if(uarg->tid%8==0 && uarg->tid!=0) {
			count = count + 1;
			if(count==4)
				count=0;
		}


        if(uarg->tid < 32)
            uarg->size = 32;
        else if(uarg->tid < 64)
            uarg->size = 64;
        else if(uarg->tid < 96)
            uarg->size = 128;
        else if(uarg->tid < 128)
             uarg->size = 256;


		uarg->gid = (inx % NUM_GROUPS);

		uarg->start_row = (inx * PER_THREAD_ROWS);


#ifdef GT_GROUP_SPLIT
		 Wanted to split the columns by groups !!!
		uarg->start_col = (uarg->gid * PER_GROUP_COLS);
#endif

		uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid,credit_array[count]);
		//printf("\nMY THREAD ID=%d , TOTAL EXEC TIME=%lu, TOTAL CPU TIME=%lu \n", uarg->tid, execution_time[uarg->tid], total_exec_time[uarg->tid]);
	}
*/
	gtthread_app_exit();
	int i,j,k;
	unsigned long total_avg=0;
	unsigned long exec_avg = 0;
    unsigned long mean_tot=0;
    double std_dev_tot=0;
    unsigned long mean_exec=0;
    double std_dev_exec=0;
	int thread_id = 0;
    int temp_thread_id=0;
	for(i=0;i<4;i++)   //size
	{
		for(j=0;j<4;j++)   //credits
		{   total_avg = 0;
			exec_avg = 0;
            mean_exec=0;
            mean_tot = 0;
            std_dev_exec = 0;
            std_dev_tot = 0;
			for(k=0;k<8;k++)//threads with same size and credit
			{
				total_avg = total_avg + total_exec_time[thread_id];
				exec_avg = exec_avg + execution_time[thread_id];
				thread_id++;
			}
            mean_tot = total_avg/8;
            mean_exec = exec_avg/8;
            temp_thread_id = thread_id-8;
            for(k=0;k<8;k++)
            {
                std_dev_tot += pow(abs(total_exec_time[temp_thread_id]-mean_tot),2);
                std_dev_exec += pow(abs(execution_time[temp_thread_id]-mean_exec),2);
                temp_thread_id++;
            }
            std_dev_tot = std_dev_tot/8;
            std_dev_exec = std_dev_exec/8;
            std_dev_tot = sqrt(std_dev_tot);
            std_dev_exec = sqrt(std_dev_exec);
			printf("\nGROUP HAS SIZE = %d , CREDITS = %d , TOTAL AVG= %lu us, EXEC AVG = %lu us\n", size_array[i], credit_array[j], total_avg/8, exec_avg/8);
            printf("STD_DEV_TOTAL = %f us, STD_DEV_EXEC = %f us\n", std_dev_tot, std_dev_exec);
		}
	}


	// print_matrix(&C);
	// fprintf(stderr, "********************************");
	return(0);
}
