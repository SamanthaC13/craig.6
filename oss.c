/*Samantha Craig
 * CS4760
 * Assignment 6*/
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<sys/time.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<time.h>
#include<semaphore.h>
#include<stdbool.h>

#define PERM (S_IRUSR|S_IWUSR)
#define SEM_PERMS (mode_t)(S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define SEM_FLAGS (O_CREAT|O_EXCL)
#define maxLogLen 10000
#define PAGESIZE 1024
#define FRAMES 64
#define CONCURRENTTASKS 18
#define TOTALTASKS 25

typedef struct{
	unsigned int sec;
	unsigned int nsec;
}systemclock_t;

typedef struct{
	int reference:1;
	int dirty:1;
	int allocated:1;
	int procnum;
	int pgnum;
}frame_t;

typedef struct{
    int framenum;
}page_t;

typedef struct{
    int pid;
    int size;
    char* requesttype;
    int reqaddress;
    int nummemaccesses;
    int numpgfaults;
    systemclock_t starttime;
    systemclock_t termtime;
    page_t pages[32];
}processcontrolblock_t;

typedef struct{
	long mtype;
	char mtext[100];
}mymsg_t;

//global variables
pid_t childpid;
systemclock_t *sc;
processcontrolblock_t *pcb;
int scid, pcbid, msgid;
FILE *logfile;
int loglen=0;
frame_t frames[FRAMES];
struct Queue* waitq;
struct Queue* pageq;
int dirtyswap=0;
int totalnumprocesses=0;
int totalmemaccesses=0;
double totalproctime=0;
int totalpgfaults=0;
int totalsegfaults=0;
void terminateproc(int proc);
int timespec2str(char buf[], struct timespec ts)
{
	const int bufsize=31;
	struct tm tm;
	localtime_r(&ts.tv_sec, &tm);
	strftime(buf,bufsize,"%Y-%m-%d %H:%M:%S.", &tm);
	sprintf(buf,"%s%09luZ", buf, ts.tv_nsec);
}

int randomint(int lower, int upper)
{
	int num=(rand() % (upper-lower +1))+lower;
	return num;
}

void advanceclock(systemclock_t *sc,int addsec,int addnsec)
{
	sc->sec=(sc->sec+addsec)+((sc->nsec+addnsec)/1000000);
	sc->nsec=(sc->nsec+addnsec)%1000000;
}

int isPast(systemclock_t *first,systemclock_t *second)
{
	if(second->sec > first->sec)
	{
		//isPast is true
		return 1;
	}
	if((second->sec >= first->sec) && (second->nsec > first->nsec))
	{
		//isPast is true
		return 1;
	}
	//isPast is false
	return 0;
}

void addtototals(int item)
{
    double start=0.0;
    double term=0.0;
    double totaltime=0.0;
    start=pcb[item].starttime.sec+((double)pcb[item].starttime.nsec/1000000);
    term=pcb[item].termtime.sec+((double)pcb[item].termtime.nsec/1000000);
    totaltime=term-start;
    totalproctime+=totaltime;
    totalnumprocesses++;
    totalmemaccesses+=pcb[item].nummemaccesses;
    totalpgfaults+=pcb[item].numpgfaults;
}

int findnextpid()
{
    int i=0;
    for(i=0;i<CONCURRENTTASKS;i++)
    {
        if(pcb[i].pid==-1)
        {
            return i;
        }
    }
    return -1;
}

void printframes()
{
    int i=0;
	fprintf(logfile,"Current memory layout at time %d:%06d\n",sc->sec,sc->nsec);
	loglen++;
    fprintf(logfile,"           Occupied  RefByte  DirtyBit\n");
    loglen++;
    for(i=0;i<FRAMES;i++)
    {
        fprintf(logfile,"Frame %03d:",i);
        //fprintf(logfile," %01d ",frames[i].allocated);
        if(frames[i].allocated)
            fprintf(logfile," Yes");
        else
            fprintf(logfile," No ");
        fprintf(logfile,"       %03d",frames[i].reference);
        fprintf(logfile,"      %01d",frames[i].dirty);
        fprintf(logfile,"\n");
        loglen++;
    }
}

void printmemorymap()
{
    int i=0;
    int j=0;
	fprintf(logfile,"Current memory map at time %d:%06d\n",sc->sec,sc->nsec);
	loglen++;
    for(i=0;i<FRAMES;i=i+80)
    {
        if((i+79)>=FRAMES)
            fprintf(logfile,"%03d-%03d: ",i,FRAMES);
        else
            fprintf(logfile,"%03d-%03d: ",i,i+79);
        for(j=i;j<i+80 && j<FRAMES;j++)
        {
            if(frames[j].allocated)
                fprintf(logfile,"+");
            else
                fprintf(logfile,".");
        }
        fprintf(logfile,"\n");
        loglen++;
    }
}
void cleanup()
{
	int i;
	char timestr[31];
	struct timespec tpend;

	//calculate statistics
	for(i=0;i<CONCURRENTTASKS;i++)
	{
		if(pcb[i].pid!=-1)
		{
			fprintf(stderr,"Killing child: %d\n",pcb[i].pid);
			kill(pcb[i].pid,SIGTERM);
		    terminateproc(i);
		}
	}
	sleep(1);

    printmemorymap();
	fprintf(logfile,"Total number of memory accesses: %i\n",totalmemaccesses);
	fprintf(logfile,"Total amount of time in processes: %05.6f\n",(double)totalproctime);
	if(totalproctime==0)
	    fprintf(logfile,"Total number of memory accesses per second: 0.0\n");	
    else
	    fprintf(logfile,"Total number of memory accesses per second: %05.6f\n",(double)totalmemaccesses/(double)totalproctime);
	fprintf(logfile,"Number of page faults: %i\n",totalpgfaults);
	if(totalmemaccesses==0)
	    fprintf(logfile,"Total number of page faults per memory accesses: 0.0\n");	
    else
	    fprintf(logfile,"Total number of page faults per memory accesses: %05.6f\n",(double)totalpgfaults/(double)totalmemaccesses);
	if(totalmemaccesses==0)
	    fprintf(logfile,"Total average memory access speed: 0.0\n");	
    else
	    fprintf(logfile,"Total average memory access speed: %05.6f\n",(double)totalproctime/(double)totalmemaccesses);
	fprintf(logfile,"Number of segmentation faults: %i\n",totalsegfaults);
	if(totalmemaccesses==0)
		fprintf(logfile,"Total number of seg faults per memory access: 0.0\n");
	else
		fprintf(logfile,"Total number of seg faults per memory access: %05.6f\n",(double)totalsegfaults/(double)totalmemaccesses);


	fprintf(stderr,"Clean up started\n");

	if(shmdt(sc)==-1)
	{
		perror("Failed to detach from shared memory for system clock");
		exit(1);
	}
	if(shmctl(scid, IPC_RMID, NULL)==-1)
	{
		perror("Failed to remove shared memory for system clock");
		exit(1);
	}
	if(shmdt(pcb)==-1)
	{
		perror("Failed to to detach from shared memory for process control block");
		exit(1);
	}
	if(shmctl(pcbid,IPC_RMID, NULL)==-1)
	{
		perror("Failed to remove shared memory for process control block");
		exit(1);
	}
	if(msgctl(msgid, IPC_RMID, NULL)==-1)
	{
		perror("Failed to remove message queue");
		exit(1);
	}
	fprintf(stderr,"Clean up complete\n");
	if(clock_gettime(CLOCK_REALTIME, &tpend)==-1)
	{
		perror("Failed ot get ending time");
		exit(1);
	}
	timespec2str(timestr,tpend);
	fprintf(stderr,"End of program: %s\n", timestr);
	fclose(logfile);

}

static void catchctrlcinterrupt(int signo)
{
	char ctrlcmsg[]="Ctrl-c interrupt\n";
	int msglen=sizeof(ctrlcmsg);
	write(STDERR_FILENO,ctrlcmsg,msglen);
	cleanup();
	exit(1);
}

void abnormalterm()
{
    cleanup();
    exit(1);
}

static void catchtimerinterrupt(int signo)
{
	char timermsg[]="Timer interrupt after specified number of seconds\n";
	int msglen =sizeof(timermsg);
	write(STDERR_FILENO, timermsg,msglen);
	cleanup();
	exit(1);
}

static int setuptimer(int t)
{
	struct itimerval value;
	value.it_interval.tv_sec=t;
	value.it_interval.tv_usec=0;
	value.it_value=value.it_interval;
	return(setitimer(ITIMER_REAL,&value,NULL));
}

struct Queue{
	int front;
	int rear;
	int size;
	int array[FRAMES];
};
//function to create queue
//it initalizes the size of the queue to 0
struct Queue* createQueue(){
	struct Queue* queue=(struct Queue*)malloc(sizeof(struct Queue));
	queue->front=queue->size=0;
	queue->rear=FRAMES-1;
	return queue;
}
//check if queue is full 
int isFull(struct Queue* queue)
{
	return(queue->size==FRAMES);
}
//check if queue is empty when size is 0
int isEmpty(struct Queue* queue)
{
	return(queue->size==0);
}
//Add an item to the queue
int enqueue(struct Queue* queue, int item)
{
	if(isFull(queue))
		return -1;
	queue->rear=(queue->rear + 1) % FRAMES;
	queue->array[queue->rear]=item;
	queue->size=queue->size+1;
	return 0;
}
//take an item off the queue
int dequeue(struct Queue* queue)
{
	if(isEmpty(queue))
		return -1;
	int item= queue->array[queue->front];
	queue->front=(queue->front+1)%FRAMES;
	queue->size=queue->size-1;
	return item;
}
//function to get the front of the queue
int front(struct Queue* queue)
{
	if(isEmpty(queue))
	{
		return -1;
	}
	return queue->array[queue->front];
}
//function to get the rear of the queue
int rear(struct Queue* queue)
{
	if(isEmpty(queue))
		return -1;
	return queue->array[queue->rear];
}
//Function to get size of queue
int size(struct Queue* queue)
{
	return queue->size;
}

void printprocstats(int proc)
{
    double start=0.0;
    double term=0.0;
    double totaltime=0.0;
    start=pcb[proc].starttime.sec+((double)pcb[proc].starttime.nsec/1000000);
    term=pcb[proc].termtime.sec+((double)pcb[proc].termtime.nsec/1000000);
    totaltime=term-start;    
    fprintf(logfile,"Termination statistics for process%d(%d)\n",proc,pcb[proc].pid);
	fprintf(logfile,"   Number of memory accesses: %i\n",pcb[proc].nummemaccesses);
	fprintf(logfile,"   Time in process: %05.6f\n",totaltime);
	if(totaltime==0)
		fprintf(logfile,"   Number of memory accesses per second: 0.0\n");
	else
		fprintf(logfile,"   Number of memory accesses per second: %05.6f\n",(double)pcb[proc].nummemaccesses/(double)totaltime);
  fprintf(logfile,"   Number of page faults: %i\n",pcb[proc].numpgfaults);
	if(pcb[proc].nummemaccesses==0)
	    fprintf(logfile,"   Number of page faults per memory accesses: 0.0\n");	
    else
	    fprintf(logfile,"   Number of page faults per memory accesses: %05.6f\n",(double)pcb[proc].numpgfaults/(double)pcb[proc].nummemaccesses);
  if(pcb[proc].nummemaccesses==0)
		fprintf(logfile,"   Average memory access speed: 0.0\n");
	else
		fprintf(logfile,"   Average memory access speed: %05.6f\n",(double)totaltime/(double)pcb[proc].nummemaccesses);
}

void terminateproc(int proc)
{
    int i;
    int frame;
    pcb[proc].termtime.sec=sc->sec;
    pcb[proc].termtime.nsec=sc->nsec;
    printprocstats(proc);
    addtototals(proc);
    for(i=0;i<32;i++)
    {
        pcb[proc].pages[i].framenum=-1;
    }
    pcb[proc].pid=-1;
    pcb[proc].size=0;
    pcb[proc].requesttype="";
    pcb[proc].reqaddress=-1;
    pcb[proc].nummemaccesses=0;
    pcb[proc].numpgfaults=0;
}

int calculatepage(int addr)
{
    int pg;
    pg = addr / PAGESIZE;
    return pg;
}

void daemonprogram()
{
    fprintf(stderr,"Threshold met: special daemon program is running\n");
    fprintf(logfile,"Threshold met: special daemon program is running\n");
    // find number of allocated pages
    int numallocated=0;
    int i=0;
    for(i=0;i<FRAMES;i++)
    {
        if(frames[i].allocated!=0)
            numallocated++;
    }
    int oldest=numallocated*0.05;
    int item;
    // go through page queue setting oldest 5% reference bits off
    for(i=0;i<FRAMES;i++)
    {
        item=dequeue(pageq);
        if(i<=oldest)
        {
            frames[item].reference=0;
        }
        enqueue(pageq,item);
    }

}

int findnextframe()
{
    int frame=0;
    int i=0;
    // go through page queue until unallocated page is found
    frame=dequeue(pageq);
    while(frames[frame].allocated!=0 && i<FRAMES)
    {
        enqueue(pageq,frame);
        i++;
        frame=dequeue(pageq);
    }
    enqueue(pageq,frame);
    if(i==FRAMES)    // all frames are allocated
    {
        // get oldest frame with reference bit off
        frame=dequeue(pageq);
        while(frames[frame].reference!=0)
        {
            enqueue(pageq,frame);
            frame=dequeue(pageq);
        }
        enqueue(pageq,frame);
    }
    return frame;
}

int loadpage(int proc, int pg)
{
    // check to see if special daemon program should run
    int numunallocated=0;
    int i=0;
    for(i=0;i<FRAMES;i++)
    {
        if(frames[i].allocated==0)
            numunallocated++;
    }
    if(numunallocated<=(FRAMES*0.1)) // if not allocated is below 10% of TOTALTASKS
    {
        daemonprogram();
    }

    int nextframe=findnextframe();
    if(frames[nextframe].allocated!=0 && frames[nextframe].dirty!=0)
    {
        dirtyswap=1;
    }
    frames[nextframe].allocated=1;
    frames[nextframe].reference=0;
    frames[nextframe].dirty=0;
    frames[nextframe].procnum=proc;
    frames[nextframe].pgnum=pg;
    pcb[proc].pages[pg].framenum=nextframe;
	if(loglen<maxLogLen)
	{
	    fprintf(logfile,"Master: Loading P%d page %d in frame %d\n",proc,pg,nextframe);
	    loglen++;
	}	        
    return nextframe;
}

void help()
{
	printf("This program is a memory management simulator with two options h and p, h is for help and p is to specify the number of user processes the oss will spawn.");
	printf("Each user process randomly decides how much memory the processes needs then the oss grants memory requests from users processes");
}

int main(int argc,char**argv)
{
	//variable declaration
	int i=0;
	int j=0;
	int option;
	int p=TOTALTASKS;
	int s=100;
	char* logfilename="logfile.log";
	struct timespec tpstart;
	char timestr[31];
	if(clock_gettime(CLOCK_REALTIME,&tpstart)==-1)
	{
		perror("Failed to get starting time");
		return 1;
	}
	timespec2str(timestr,tpstart);
	fprintf(stderr,"Beginning the program: %s\n", timestr);

	//get options from command line
	while((option=getopt(argc,argv,"hp:"))!=-1)
	{
		switch(option)
		{
			case 'h':
				help();
				break;
      case 'p':
        p = atoi(optarg);
        break;
		  default:
				perror("Usage: Unknown option ");
				exit(EXIT_FAILURE);
		}
	}
	fprintf(stderr, "Usage: %s [-p %d] \n",argv[0],p);
	
	if(setuptimer(s) ==-1)
	{
		perror("Failed to set up the timer");
		return 1;
	}
	signal(SIGALRM,catchtimerinterrupt);
	signal(SIGINT,catchctrlcinterrupt);

	if((logfile=fopen(logfilename,"w"))==NULL)
	{
		perror("Failed to open logfile");
		return 1;
	}

	//get shared memory
	key_t sckey;
	if((sckey=ftok(".",13))==-1)
	{
		perror("Failed to return system clock key");
		abnormalterm();
	}
	if((scid=shmget(sckey,sizeof(systemclock_t),PERM |IPC_CREAT))==-1)
	{
		perror("Failed to create shared memory segment for system clock");
		abnormalterm();
	}
	if((sc=(systemclock_t*)shmat(scid,NULL, 0))==(void*)-1)
	{	
		perror("Failed to attach shared memory segment for system clock");
		if(shmctl(scid,IPC_RMID,NULL)==-1)
			perror("Failed to remove shared memory for system clock");
		abnormalterm();
	}
	key_t pcbkey;
	if((pcbkey=ftok(".",27))==-1)
	{
		perror("Failed to return process control block key");
		abnormalterm();
	}
	if((pcbid=shmget(pcbkey,sizeof(processcontrolblock_t)*CONCURRENTTASKS,PERM |IPC_CREAT))==-1)
	{
		perror("Failed to create shared memory segment for process control block");
		abnormalterm();
	}
	if((pcb=(processcontrolblock_t*)shmat(pcbid,NULL, 0))==(void*)-1)
	{	
		perror("Failed to attach shared memory segment for process control block");
		if(shmctl(pcbid,IPC_RMID,NULL)==-1)
			perror("Failed to remove shared memory for process control block");
		abnormalterm();
	}
	srand(time(0));
	sc->sec=0;
	sc->nsec=0;
	fprintf(stderr,"Master: Current system clock time is %d:%06d\n",sc->sec,sc->nsec);
	loglen++;
	for(i=0;i<CONCURRENTTASKS;i++)
	{
	    pcb[i].pid=-1;
	    pcb[i].size=0;
	    pcb[i].nummemaccesses=0;
	    pcb[i].numpgfaults=0;
        pcb[i].requesttype="";
        pcb[i].reqaddress=-1;
        for(j=0;j<32;j++)
	    {
	        pcb[i].pages[j].framenum=-1;
	    }
	}
	
	// set up frames 
	pageq = createQueue();
	for(i=0;i<FRAMES;i++)
	{
	    frames[i].reference=0;
	    frames[i].dirty=0;
	    frames[i].allocated=0;
	    frames[i].procnum=-1;
	    frames[i].pgnum=-1;
	    enqueue(pageq,i);
	}
	printmemorymap();

	//get message queue
	key_t msgkey;
	if((msgkey=ftok(".",32))==-1)
	{
		perror("Failed to return massage queue key");
		abnormalterm();
	}
	if((msgid=msgget(msgkey, PERM|IPC_CREAT))==-1)
	{
		perror("Failed to create message queue");
		abnormalterm();
	}
	
	//set up wait queue
	waitq = createQueue();

	mymsg_t mymsg;
	char *msg="Done";
	int msglen=strlen(msg);
	int numProcesses=0;
	int totalProcesses=0;
	int nextpid=0;
	int next=0;
	int item=0;
	int notdone=1;
	int loop=0;
	i=0;
	int sec=0;
	int nsec=0;
	int x=0;
	char numString[10];
	char numString2[10];
	char* token;
	char* requesttype;
	int reqproc;
	int reqpage;
	int reqaddress;
	int reqframe;
	int sendmessage;
	int numnomsg=0;
	int killedproc=-1;
    systemclock_t nextprocesstime;
    nextprocesstime.sec = sc->sec;
    nextprocesstime.nsec = sc->nsec;
    systemclock_t nextdisktime;
    nextdisktime.sec = sc->sec;
    nextdisktime.nsec = sc->nsec;
    
	//start cycling
	while(notdone)
	{
		if( ((isPast(&nextprocesstime,sc))==1) &&
		    numProcesses<CONCURRENTTASKS && totalProcesses<TOTALTASKS)
		{
			//start porcess
			nextpid=findnextpid();
			numProcesses++;
			totalProcesses++;
			childpid=fork();
			if(childpid==-1)
			{
				perror("Failed to fork user process");
				abnormalterm();
			}
			if(childpid==0)
			{
				sprintf(numString,"%d",CONCURRENTTASKS);
				sprintf(numString2,"%d",nextpid);
				execl("./user_proc","user_proc",numString,numString2,NULL);
				perror("Failed to exec to user");
				abnormalterm();
			}
			//parent code
			if(loglen<maxLogLen)
			{
				fprintf(logfile,"Master has created process %d(%d)\n",nextpid,childpid);
				loglen++;
			}
			pcb[nextpid].pid=childpid;
			pcb[nextpid].size=randomint(1,32);
			pcb[nextpid].starttime.sec=sc->sec;
			pcb[nextpid].starttime.nsec=sc->nsec;
			advanceclock(&nextprocesstime,0,randomint(1,50));
			sleep(1);
		}

        if((isPast(&nextdisktime,sc))==1)
        {
    		if(!isEmpty(waitq))
    		{
				reqproc=dequeue(waitq);
                fprintf(logfile,"Processing request from P%d from wait queue\n",reqproc);
				reqpage=calculatepage(pcb[reqproc].reqaddress);
				dirtyswap=0;
    			reqframe=loadpage(reqproc,reqpage);
    			requesttype=pcb[reqproc].requesttype;
    			pcb[reqproc].nummemaccesses++;
    			if(strcmp(requesttype,"read")==0)
    			{
        			if(loglen<maxLogLen)
        			{
        			    fprintf(logfile,"Master: Address %d in frame %d, giving data to P%d at time %d:%06d\n",reqaddress,reqframe,reqproc,sc->sec,sc->nsec);
        			    loglen++;
        			}
    			} 
    			else
    			{
        			if(loglen<maxLogLen)
        			{
        			    fprintf(logfile,"Master: Address %d in frame %d, writing data to frame at time %d:%06d\n",reqaddress,reqframe,sc->sec,sc->nsec);
        			    loglen++;
        			}
        			frames[reqframe].dirty=1;
    			}
    			frames[reqframe].reference=1;
    			pcb[reqproc].nummemaccesses++;
        		memcpy(mymsg.mtext,msg,msglen);
        		mymsg.mtype=pcb[reqproc].pid;
        		if (msgsnd(msgid,&mymsg,msglen,0)==-1)
        		{
        			perror("Failed to send message");
        			return 1;
        		}
    			if(loglen<maxLogLen)
    			{    		
    			    fprintf(logfile,"Master sent message to process %d(%d)\n",reqproc,pcb[reqproc].pid);
    			    loglen++;
    			}
    			pcb[reqproc].requesttype="";
    			pcb[reqproc].reqaddress=-1;
    		}
    		if(dirtyswap==0)
			    advanceclock(&nextdisktime,0,14);
			else
			    advanceclock(&nextdisktime,0,28);
		}
        
		//wait for message from user process
        sendmessage=1;
        requesttype="";
        reqproc=-1;
        reqaddress=-1;
		int msgsize;
		if((msgsize=msgrcv(msgid,&mymsg,100,getpid(),IPC_NOWAIT))==-1)
		//if((msgsize=msgrcv(msgid,&mymsg,100,getpid(),0))==-1)
		{
		    if(errno==ENOMSG)
		    {
		        sendmessage=0;
		        numnomsg++;
		    }
		    else
		    {
			    perror("Failed to recieve message");
			    return 1;
		    }
		}
		else
		{
		    numnomsg=0;
            requesttype = strtok(mymsg.mtext, ",");
            reqproc = atoi(strtok(0, ","));
            reqaddress = atoi(strtok(0, ","));
		}

		if(strcmp(requesttype,"read")==0)
		{
			if(loglen<maxLogLen)
			{
			    fprintf(logfile,"Master: P%d requesting %s of address %d at time %d:%06d\n",reqproc,requesttype,reqaddress,sc->sec,sc->nsec);
			    loglen++;
			}
			reqpage=calculatepage(reqaddress);
			if(reqpage>=pcb[reqproc].size)
			{
			    fprintf(logfile,"Master: P%d generating segmentation fault, killing process time %d:%06d\n",reqproc,sc->sec,sc->nsec);
			    kill(pcb[reqproc].pid,SIGTERM);
		        terminateproc(reqproc);
		        sendmessage=0;
		        totalsegfaults++;
		        numProcesses--;
			}
			else
			{
    			if(pcb[reqproc].pages[reqpage].framenum==-1)
    			{
        			if(loglen<maxLogLen)
        			{
        			    fprintf(logfile,"Master: P%d address %d is not in a frame, pagefault\n",reqproc,reqaddress);
        			    loglen++;
        			}
        			pcb[reqproc].requesttype=requesttype;
        			pcb[reqproc].reqaddress=reqaddress;
        			enqueue(waitq,reqproc);
        			pcb[reqproc].numpgfaults++;
        			sendmessage=0;
    			}
    			else
    			{
        			reqframe=pcb[reqproc].pages[reqpage].framenum;
        			if(loglen<maxLogLen)
        			{
        			    fprintf(logfile,"Master: Address %d in frame %d, giving data to P%d at time %d:%06d\n",reqaddress,reqframe,reqproc,sc->sec,sc->nsec);
        			    loglen++;
        			}    			    
        			pcb[reqproc].nummemaccesses++;
        			frames[reqframe].reference=1;
    			}
			}	
		}
		else if(strcmp(requesttype,"write")==0)
		{
			if(loglen<maxLogLen)
			{
			    fprintf(logfile,"Master: P%d requesting %s of address %d at time %d:%06d\n",reqproc,requesttype,reqaddress,sc->sec,sc->nsec);
    			loglen++;
			}
			reqpage=calculatepage(reqaddress);
			if(reqpage>=pcb[reqproc].size)
			{
			    fprintf(logfile,"Master: P%d generating segmentation fault, killing process time %d:%06d\n",reqproc,sc->sec,sc->nsec);
			    kill(pcb[reqproc].pid,SIGTERM);
		        terminateproc(reqproc);
		        sendmessage=0;
		        totalsegfaults++;
		        numProcesses--;
			}
			else
			{
    			if(pcb[reqproc].pages[reqpage].framenum==-1)
    			{
        			if(loglen<maxLogLen)
        			{
        			    fprintf(logfile,"Master: P%d address %d is not in a frame, pagefault\n",reqproc,reqaddress);
        			    loglen++;
        			}
        			pcb[reqproc].requesttype=requesttype;
        			pcb[reqproc].reqaddress=reqaddress;
        			enqueue(waitq,reqproc);
        			pcb[reqproc].numpgfaults++;
        			sendmessage=0;
    			}
    			else
    			{
        			reqframe=pcb[reqproc].pages[reqpage].framenum;
        			if(loglen<maxLogLen)
        			{
        			    fprintf(logfile,"Master: Address %d in frame %d, writing data to frame at time %d:%06d\n",reqaddress,reqframe,sc->sec,sc->nsec);
        			    loglen++;
        			}
        			frames[reqframe].dirty=1;
        			frames[reqframe].reference=1;
        			pcb[reqproc].nummemaccesses++;
    			} 
			}
		}
		else if(strcmp(requesttype,"terminate")==0)
		{
			if(loglen<maxLogLen)
			{			
			    fprintf(logfile,"Master: P%d is indicating termination at time %d:%06d\n",reqproc,sc->sec,sc->nsec);
			    loglen++;
			}
			terminateproc(reqproc);
			numProcesses--;
			sendmessage=0;
		}
		else if(strcmp(requesttype,"")==0)
		{
		    //fprintf(stderr,"Master has not received a message\n",reqproc);
		    sleep(1);
    	}
		else
		{
			perror("Master has detected an unknown request.");
			abnormalterm();
		}
        
        // send message back to user
        if(sendmessage==1)
        {
    		memcpy(mymsg.mtext,msg,msglen);
    		mymsg.mtype=pcb[reqproc].pid;
    		if (msgsnd(msgid,&mymsg,msglen,0)==-1)
    		{
    			perror("Failed to send message");
    			return 1;
    		}
			if(loglen<maxLogLen)
			{    		
			    fprintf(logfile,"Master sent message to process %d(%d)\n",reqproc,pcb[reqproc].pid);
			    loglen++;
			}
        }
        
		//advance clock for cycle
		sec=0;
		nsec=10;
		advanceclock(sc,sec,nsec);

		if(loglen<maxLogLen)
		{
			fprintf(logfile,"Master: Current system clock time is %d:%06d\n",sc->sec,sc->nsec);
			loglen++;
		}

		if(totalProcesses>=p)
		//if(loop>25)
		{
			notdone=0;
		}
		loop++;
	}

	cleanup();
	return 0;
}

