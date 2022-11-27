#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "coursework.c"
#include "linkedlist.c"
#include <pthread.h>
#include <semaphore.h>

/* create Process Table Array */
struct process *processTable[SIZE_OF_PROCESS_TABLE];
struct process *currentProcess[NUMBER_OF_CPUS];

/* Allocated memory for freePidList */
int freePidData[SIZE_OF_PROCESS_TABLE];

/* Keep track of process creation & termination */
int createdProcesses = 0;
int terminatedProcesses = 0;

/* Variables to calculate process statistics */
float averageResponseTime = 0;
float averageTurnaroundTime = 0;

/* Base Time Variable */
struct timeval oBaseTime;

/* create Priority Queue Array */
struct element *firstPriorityQueue[MAX_PRIORITY];
struct element *lastPriorityQueue[MAX_PRIORITY];

/* create New Queue Variables */
struct element *firstNewQueue = NULL;
struct element *lastNewQueue = NULL;

/* create Terminated Queue Variables */
struct element *firstTerminatedQueue = NULL;
struct element *lastTerminatedQueue = NULL;

/* create FreePidList LinkedList Variables */
struct element *firstFreePidList = NULL;
struct element *lastFreePidList = NULL;

/* mutex & semaphore variables */
pthread_t thread[4];
pthread_t STS[NUMBER_OF_CPUS];

pthread_mutex_t createdProcessLock;
pthread_mutex_t terminatedProcessLock;
pthread_mutex_t freePidListLock;
pthread_mutex_t freePidDataLock;
pthread_mutex_t newQueueLock;
pthread_mutex_t priorityQueueLock;
pthread_mutex_t terminatedQueueLock;

sem_t genSem;
sem_t shortSem[NUMBER_OF_CPUS];

void printHeadersSVG()
{
	printf("SVG: <!DOCTYPE html>\n");
	printf("SVG: <html>\n");
	printf("SVG: <body>\n");
	printf("SVG: <svg width=\"10000\" height=\"10000\">\n");
}

void printProcessSVG(int iCPUId, struct process * pProcess, struct timeval oStartTime, struct timeval oEndTime) {
	int iXOffset = getDifferenceInMilliSeconds(oBaseTime, oStartTime) + 30;
	int iYOffsetPriority = (pProcess->iPriority + 1) * 16 - 12;
	int iYOffsetCPU = (iCPUId - 1 ) * (480 + 50);
	int iWidth = getDifferenceInMilliSeconds(oStartTime, oEndTime);
	printf("SVG: <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"8\" style=\"fill:rgb(%d,0,%d);stroke-width:1;stroke:rgb(255,255,255)\"/>\n", iXOffset /* x */, iYOffsetCPU + iYOffsetPriority /* y */, iWidth, *(pProcess->pPID) - 1 /* rgb */, *(pProcess->pPID) - 1 /* rgb */);
}

void printPrioritiesSVG() {
	for(int iCPU = 1; iCPU <= NUMBER_OF_CPUS;iCPU++)
	{
		for(int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
		{
			int iYOffsetPriority = (iPriority + 1) * 16 - 4;
			int iYOffsetCPU = (iCPU - 1) * (480 + 50);
			printf("SVG: <text x=\"0\" y=\"%d\" fill=\"black\">%d</text>", iYOffsetCPU + iYOffsetPriority, iPriority);
		}
	}
}
void printRasterSVG() {
	for(int iCPU = 1; iCPU <= NUMBER_OF_CPUS;iCPU++)
	{
		for(int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
		{
			int iYOffsetPriority = (iPriority + 1) * 16 - 8;
			int iYOffsetCPU = (iCPU - 1) * (480 + 50);
			printf("SVG: <line x1=\"%d\" y1=\"%d\" x2=\"10000\" y2=\"%d\" style=\"stroke:rgb(125,125,125);stroke-width:1\" />", 16, iYOffsetCPU + iYOffsetPriority, iYOffsetCPU + iYOffsetPriority);
		}
	}
}

void printFootersSVG() {
	printf("SVG: Sorry, your browser does not support inline SVG.\n");
	printf("SVG: </svg>\n");
	printf("SVG: </body>\n");
	printf("SVG: </html>\n");
}

void initialiseFreePidList() {
    for(int i = 0; i < SIZE_OF_PROCESS_TABLE; i++) {
        freePidData[i] = i;
        addLast((void *)&freePidData[i],&firstFreePidList,&lastFreePidList);
    }
}

void initialisePriorityQueues() {
    for(int i = 0; i < MAX_PRIORITY; i++) {
        firstPriorityQueue[i] = *firstPriorityQueue;
    }
    for(int i = 0; i < MAX_PRIORITY; i++) {
        lastPriorityQueue[i] = *lastPriorityQueue;
    }
}

void * generateProcesses(void *vargp) {
    printf("TXT: Generate Processes Thread Started!\n");
    while(createdProcesses < NUMBER_OF_PROCESSES) {
        sem_wait(&genSem);
        while (firstFreePidList != NULL && createdProcesses < NUMBER_OF_PROCESSES)  {
            /* Removes free PID from Free PID List, generates a new process with that PID */
            pthread_mutex_lock(&freePidListLock);
            void * data = removeFirst(&firstFreePidList, &lastFreePidList);
            pthread_mutex_unlock(&freePidListLock);
            int i = *((int *)data);
            struct process * p = generateProcess((int *) data);
            processTable[i] = p;

            /* Checks all jobs currently running in CPU, preempts if lower priority process generated */
            for(int j = 0; j < NUMBER_OF_CPUS; j++) {
                if(currentProcess[j]!= NULL) {
                    if(p->iPriority < currentProcess[j]->iPriority) {
                        preemptJob(currentProcess[j]);
                    }
                }
            }

            /* Prints Process Info, adds it to New Queue and increments createdProcesses counter*/
            printf("TXT: Generated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *((int *)p->pPID), p->iPriority, p->iPreviousBurstTime, p->iRemainingBurstTime);
            pthread_mutex_lock(&newQueueLock);
            addLast((void *) p, &firstNewQueue, &lastNewQueue);
            pthread_mutex_unlock(&newQueueLock);
            pthread_mutex_lock(&createdProcessLock);
            createdProcesses++;
            pthread_mutex_unlock(&createdProcessLock);
        }
    }
}

void * longTermScheduler(void *vargp) {
    printf("TXT: Long Term Scheduler Thread Started!\n");
    int j = 0;
    while(terminatedProcesses < NUMBER_OF_PROCESSES) {
        if (firstNewQueue != NULL) {
            while (firstNewQueue != NULL)    {
                /* Remove Processes from newQueue */
                pthread_mutex_lock(&newQueueLock);
                struct process * data = (struct process *)removeFirst(&firstNewQueue, &lastNewQueue);
                pthread_mutex_unlock(&newQueueLock);

                /* Add to appropriate Ready Queue */
                pthread_mutex_lock(&priorityQueueLock);
                addLast((void *) data,&firstPriorityQueue[data->iPriority],&lastPriorityQueue[data->iPriority]);
                pthread_mutex_unlock(&priorityQueueLock);
                printf("TXT: Admitted: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime);
                /* Wakes up 1 CPU everytime a process gets added to a Priority Queue */
                if(j > (NUMBER_OF_CPUS-1)) {
                    j = 0;
                }
                sem_post(&shortSem[j]);
            }
        j++;
        }
    /*usleep(LONG_TERM_SCHEDULER_INTERVAL);*/
    } 
}

void * shortTermScheduler(void *vargp) {
    printf("Short Term Scheduler Thread Started!\n");
    while(terminatedProcesses < NUMBER_OF_PROCESSES) {
        /* Short Term Scheduler puts itself to sleep on first time running as there nothing to simulate. CPUs are woken up by Long Term Scheduler as processes are added to Ready Queues */
        sem_wait(&shortSem[*((int *)vargp)-1]);
        int i = 0;
        while(i < MAX_PRIORITY) {
            while (firstPriorityQueue[i] != NULL && i < MAX_PRIORITY)    {
                pthread_mutex_lock(&priorityQueueLock);
                struct process * data = (struct process *)removeFirst(&firstPriorityQueue[i], &lastPriorityQueue[i]);
                pthread_mutex_unlock(&priorityQueueLock);
                if (data == NULL) {
                    i = 0;
                    break;
                }
                /* FCFS for Priority 0 - 15 */
                if (i < (MAX_PRIORITY/2)) {
                    struct timeval startTime;
                    struct timeval endTime;
                    currentProcess[*((int *)vargp)-1] = data;
                    runNonPreemptiveJob(data, &startTime, &endTime);
                    currentProcess[*((int *)vargp)-1] = NULL;
                    int burstTime = data->iRemainingBurstTime;
                    long int responseTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oFirstTimeRunning);
                    /* Finishes on the first time */
                    if(data->iInitialBurstTime == data->iPreviousBurstTime && burstTime == 0) {
                        long int turnaroundTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oLastTimeRunning);
                        printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d, Turnaround Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime, responseTime, turnaroundTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&terminatedQueueLock);
                        addLast((void *) data,&firstTerminatedQueue,&lastTerminatedQueue);
                        pthread_mutex_unlock(&terminatedQueueLock);
                    }
                    /* Finishes but not on the first time */
                    else if (burstTime == 0) {
                        long int turnaroundTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oLastTimeRunning);
                        printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime, turnaroundTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&terminatedQueueLock);
                        addLast((void *) data,&firstTerminatedQueue,&lastTerminatedQueue);
                        pthread_mutex_unlock(&terminatedQueueLock);
                    }
                    /* Process doesn't finish the first time it runs */
                    else if(data->iInitialBurstTime == data->iPreviousBurstTime && data->iRemainingBurstTime > 0) {
                        printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime, responseTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&priorityQueueLock);
                        addFirst((void *) data,&firstPriorityQueue[data->iPriority],&lastPriorityQueue[data->iPriority]);
                        pthread_mutex_unlock(&priorityQueueLock);
                    }
                    /* Process doesn't finish after the first time it runs */
                    else {
                        printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&priorityQueueLock);
                        addFirst((void *) data,&firstPriorityQueue[data->iPriority],&lastPriorityQueue[data->iPriority]);
                        pthread_mutex_unlock(&priorityQueueLock);
                    }
                        
                }
                /* RR for Priority 16 - 31 */
                else if (i < MAX_PRIORITY)    {
                    struct timeval startTime;
                    struct timeval endTime;
                    runPreemptiveJob(data, &startTime, &endTime);
                    long int responseTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oFirstTimeRunning);
                    int burstTime = data->iRemainingBurstTime;
                    /* Finishes on the first time */
                    if(data->iInitialBurstTime == data->iPreviousBurstTime && burstTime == 0) {
                        long int turnaroundTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oLastTimeRunning);
                        printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime, turnaroundTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&terminatedQueueLock);
                        addLast((void *) data,&firstTerminatedQueue,&lastTerminatedQueue);
                        pthread_mutex_unlock(&terminatedQueueLock);
                    }
                    /* Finishes but not on the first time */
                    else if (burstTime == 0) {
                        long int turnaroundTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oLastTimeRunning);
                        printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime, turnaroundTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&terminatedQueueLock);
                        addLast((void *) data,&firstTerminatedQueue,&lastTerminatedQueue);
                        pthread_mutex_unlock(&terminatedQueueLock);
                    }
                    /* Process doesn't finish the first time it runs */
                    else if(data->iInitialBurstTime == data->iPreviousBurstTime && data->iRemainingBurstTime > 0) {
                        printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime, responseTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&priorityQueueLock);
                        addLast((void *) data,&firstPriorityQueue[data->iPriority],&lastPriorityQueue[data->iPriority]);
                        pthread_mutex_unlock(&priorityQueueLock);
                    }
                    /* Process doesn't finish after the first time it runs */
                    else {
                        printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *((int *)vargp), *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime);
                        printProcessSVG(*((int *)vargp), data, startTime, endTime);
                        pthread_mutex_lock(&priorityQueueLock);
                        addLast((void *) data,&firstPriorityQueue[data->iPriority],&lastPriorityQueue[data->iPriority]);
                        pthread_mutex_unlock(&priorityQueueLock);
                    }
                }
                /* Scheduler checks priority 0 everytime a job is run and iterates the queues until it finds the lowest priority process available */
                i = 0;
            }
        /* If a priority queue has nothing in it, increment i to check next priority queue. */
        i++;
        }
    }
}

void * terminationDaemon(void *vargp) {
    printf("TXT: Termination Daemon Thread Started!\n");
    while(terminatedProcesses < NUMBER_OF_PROCESSES) {
        /* Only wakes up generateProcess thread after it sucessfully terminates some processes. */
        if(firstTerminatedQueue != NULL) {
            while (firstTerminatedQueue != NULL) {
                pthread_mutex_lock(&terminatedQueueLock);
                struct process * data = (struct process *)removeFirst(&firstTerminatedQueue, &lastTerminatedQueue);
                pthread_mutex_unlock(&terminatedQueueLock);
                printf("TXT: Terminated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *((int *)data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime);
                long int responseTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oFirstTimeRunning);
                long int turnaroundTime = getDifferenceInMilliSeconds(data->oTimeCreated, data->oLastTimeRunning);
                averageResponseTime = averageResponseTime + responseTime;
                averageTurnaroundTime = averageTurnaroundTime + turnaroundTime;
                int i = *((int *)data->pPID);
                pthread_mutex_lock(&freePidDataLock);
                freePidData[i] = i;
                pthread_mutex_lock(&freePidListLock);
                addLast((void *)&freePidData[i],&firstFreePidList,&lastFreePidList);
                pthread_mutex_unlock(&freePidDataLock);
                pthread_mutex_unlock(&freePidListLock);
                processTable[i] = NULL;
                free(data);
                pthread_mutex_lock(&terminatedProcessLock);
                terminatedProcesses++;
                pthread_mutex_unlock(&terminatedProcessLock);
            }
        sem_post(&genSem);
        }
    /*usleep(TERMINATION_INTERVAL);*/
    } 
/* Calculates Process Statistics once terminated thread has terminated all processes*/
printf("TXT: Total Processes Created: %d, Total Processes Terminated: %d\n", createdProcesses, terminatedProcesses);
averageResponseTime = averageResponseTime / NUMBER_OF_PROCESSES;
averageTurnaroundTime = averageTurnaroundTime / NUMBER_OF_PROCESSES;
printf("TXT: Average Response Time: %0.6f milliseconds, Average Turnaround Time: %0.6f milliseconds\n", averageResponseTime, averageTurnaroundTime); 
}

void * boosterDaemon(void *vargp) {
    printf("TXT: Booaster Daemon Thread Started!\n");
    while(terminatedProcesses < NUMBER_OF_PROCESSES) {
        /* Starts at priority 17 and boosts all processes to queue 16, then iterates through all lower priorities and does the same */
        int i = ((MAX_PRIORITY/2) + 1);
        while(i < MAX_PRIORITY && terminatedProcesses < NUMBER_OF_PROCESSES) {
            while(firstPriorityQueue[i] != NULL)    {
                pthread_mutex_lock(&priorityQueueLock);
                struct process * data = (struct process *)removeFirst(&firstPriorityQueue[i], &lastPriorityQueue[i]);
                pthread_mutex_unlock(&priorityQueueLock);
                printf("TXT: Boost: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *((int *) data->pPID), data->iPriority, data->iPreviousBurstTime, data->iRemainingBurstTime);
                pthread_mutex_lock(&priorityQueueLock);
                addLast((void *) data,&firstPriorityQueue[(MAX_PRIORITY/2)],&lastPriorityQueue[(MAX_PRIORITY/2)]);
                pthread_mutex_unlock(&priorityQueueLock);
            }
            i++;
        }
    usleep(BOOST_INTERVAL);
    } 
}

void freeFreePidList() {
    struct element * local = firstFreePidList;
    while(local != NULL) {
        firstFreePidList = local;
        local = firstFreePidList->pNext;
        free(firstFreePidList);
    }
}

int main() {
    /* Updates oBaseTime with the time the program started */
    gettimeofday(&oBaseTime,NULL);

    sem_init(&genSem, 0, 1);
    for(int i = 0; i < NUMBER_OF_CPUS; i++) {
        sem_init(&shortSem[i], 0, 0);
    }

    pthread_mutex_init(&createdProcessLock,NULL);
    pthread_mutex_init(&terminatedProcessLock,NULL);
    pthread_mutex_init(&freePidListLock,NULL);
    pthread_mutex_init(&freePidDataLock,NULL);
    pthread_mutex_init(&newQueueLock,NULL);
    pthread_mutex_init(&priorityQueueLock,NULL);
    pthread_mutex_init(&terminatedQueueLock,NULL);

    initialiseFreePidList();
    initialisePriorityQueues();

    printHeadersSVG();
    printPrioritiesSVG();
    printRasterSVG();

    pthread_create(&thread[0], NULL, generateProcesses, NULL);
    pthread_create(&thread[1], NULL, longTermScheduler, NULL);
    pthread_create(&thread[2], NULL, boosterDaemon, NULL);
    pthread_create(&thread[3], NULL, terminationDaemon, NULL);
    
    /* Creates predefined number of Short Term Scheduler Threads and passes in CPUID */
    int arr[NUMBER_OF_CPUS];
    for(int i = 0; i < NUMBER_OF_CPUS;i++) {
        arr[i] = i+1;
        pthread_create(&STS[i], NULL, shortTermScheduler, &arr[i]);
    }
    

    /* Joins all threads before printing SVG Footers */
    pthread_join(thread[3],NULL);
    pthread_join(thread[1],NULL);
    pthread_join(thread[2],NULL);
    pthread_join(thread[0],NULL);

    /* Wakes up any sleeping semaphores */
    for(int i = 0; i < NUMBER_OF_CPUS; i++) {
        int temp = 0;
        sem_getvalue(&shortSem[i], &temp);
        for(int j = 1; j > temp; j--) {
            sem_post(&shortSem[i]);
        }
    }

    /* Joins all CPU threads */
    for(int i = 0; i < NUMBER_OF_CPUS; i++) {
        pthread_join(STS[i],NULL);
    }
    
    printFootersSVG();

    /* Free's all allocated memory for freePidList */
    freeFreePidList();

   return 0;
}