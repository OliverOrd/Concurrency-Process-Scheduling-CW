# Output Visualisation

![Alt text](output2.jpg?raw=true "Optional Title")

# Overview

The goal of this coursework is to make use of operating system APIs (specifically, the POSIX API in Linux) and
concurrency directives to implement a process management system like the ones you would find in common
operating systems. You will implement a scheduling algorithm like the one in Windows 10 and use multiple
queues (some of which will use the principles of bounded buffers) to model the system.

Completing all tasks will give you a good understanding of:

    • The use of operating system APIs.
    • Process / thread scheduling algorithms.
    • Evaluation criteria for process scheduling.
    • The implementation of process tables, operating system queues, and bounded buffers.
    • Critical sections, semaphores and mutual exclusion.
    • The basics of concurrent / parallel programming using an operating system’s functionalities.


# Requirements
This coursework has the following key parts:

    • The implementation of data structures (e.g. process queue, process table) like the ones you would find
    in real world operating systems (although slightly simplified here).
    • The implementation of a “process generator”, long and short-term schedulers, a priority booster, and a
    “termination daemon”.
    • The implementation of a “Windows-10-ish” scheduling algorithm with pre-emption.
    • The calculation of response and turn-around times.
    • The simulation of processes.
    • The efficient and correct synchronisation of data structures and code (where necessary

## Process Table
A process table keeps track of the process control blocks (represented by the struct process in
coursework.h) that contain information on processes currently present in the system. Process tables typically
have a fixed size and are indexed using the processes’ identifier (iPID). That is, the process control block of a
process with PID 999 will be stored at index 999 in the process table. The number of processes present in a
system can therefore not exceed the size of the process table.
To efficiently determine which PIDs are currently not in use (and hence which indexes in the process table are
available), a list of free PIDs can be maintained. A linked list can be appropriate for this (and is recommended for
this coursework). 

## Queues
An operating system maintains multiple queues internally, e.g. for processes that are in the ready state (usually
one queue per priority level) or processes that are blocked. During their lifespan, processes will transition
between the different queues. These transitions correspond to the state transitions that were discussed in the
lectures. Queues are, in the case of this coursework, implemented as linked lists. The full implementation of this
coursework will contain queues for new processes, terminated processes, and ready processes. Note that, if
processes block, they will be added to a blocked queue (not considered here).

## Process generator
Processes are added to the system by a process generator. The generator adds processes to the system as soon
as there is capacity, i.e. a free space in the process table and an unused PIDs. The process generator should be
“woken up” when a PID is successfully released (it is added to the free list, by the termination daemon), and goes
to sleep when all PIDs are in use. New processes are added to the “new-queue”.

## Schedulers
The long-term scheduler runs periodically (as a separate thread) and admits processes to the system by removing
them from the new queue and adding them to the ready queue. The short-term scheduler admits processes to
the CPU. The short-term scheduler is “called” in response to events such as a process that has used up its timeslice. The short-term scheduler should, in this case, implement a “Windows-10-ish” scheduling algorithm.


The “Windows-10-ish” scheduling algorithm implements two separate process scheduling algorithms. The higher
“real time” priority levels (0 – 15) implement a pre-emptive FCFS. That is, a lower priority FCFS job is pre-empted
when a higher priority FCFS process shows up (we recommend that you implement the non-preemptive version
first). The lower “variable” priority levels (16 – 31) implement a round robin with priority boosting. Priority
boosting helps to prevent inversion of control and ensures that all processes, even low priority ones, will
periodically run.

## Booster Daemon

Windows 10 uses priority boosting. That is, variable jobs with a low priority are boosted (to the highest level
within their class) at regular intervals to the highest RR level to ensure that low priority processes also get some
CPU time allocated. Priority boosting can be implemented by removing jobs from lower priority queues and
adding them to the end of the highest round robin priority queue, i.e. level 16 (tip: this does not mean that the
priority value of the process itself should be changed). The priority booster runs as a separate thread at regular
intervals (e.g. every 50 milli-seconds).

## Termination Daemon

A “termination daemon” is responsible for post processing process control blocks and releasing process
identifiers (PIDs) when no longer required. The daemon should run at regular intervals, e.g. every 50ms. As soon
as PIDs are successfully added to the “free-list”, the process generator is woken up to add new jobs to the system,
if any are remaining.

As a secondary task, the “termination daemon” is responsible for calculating the response and turn-around times
of the processes (once they have finished), printing them on the screen, and printing the final averages on the
screen once the simulation of all processes has terminated.

## Process Simulation

To make the simulation of the processes more realistic, a file with functions that simulate processes running on
the CPU is provided (coursework.c). The function is an “empty-ish” loop that terminates when the process’s
time slice has expired, when a process blocks (if enabled), or when a process is pre-empted. It is the short-term
scheduler that calls the simulation functions. 

Depending on the priority level of the process, either the
runNonPreemptiveProcess or runPreempitiveProcess function should be called. The number of times the
functions can be called simultaneously corresponds to the number of “CPUs” (consumers). Your code must be
able to simulate multiple “CPUs” and each “CPU” must have its own unique ID. 

Tips:

    • The short-term scheduler could run in separate threads, from which the simulation functions are called
    (representing the “CPUs”).
    • You probably want to wrap the simulation functions up in a different function that generates the
    expected output when the simulation function returns. This will simplify your code.
