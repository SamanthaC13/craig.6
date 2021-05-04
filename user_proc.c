#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<time.h>
#include<signal.h>
#include<sys/time.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<math.h>
#include<semaphore.h>
#define PERM (S_IRUSR|S_IWUSR)
#define TERMINATIONPERCENT 10
#define WRITEPERCENT 33
#define SEGFAULTPERCENT 5
#define PAGESIZE 1024


typedef struct{
	unsigned int sec;
	unsigned int nsec;
}systemclock_t;

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

typedef struct {
	long mtype;
	char mtext[100];
}mymsg_t;

int randomint(int lower,int upper)
{
	int num=(rand()%(upper-lower+1))+lower;
	return num;
}

void advanceclock(systemclock_t *sc, int addsec,int addnsec)
{
	sc->sec=(sc->sec+addsec)+((sc->nsec+addnsec)/1000000);
	sc->nsec=(sc->nsec+addnsec)%1000000;
}

int main(int argc,char**argv)
{
	systemclock_t *sc;
	processcontrolblock_t *pcb;
	int scid,pcbid;
	struct timespec tpchild;
	int id;
	int i;
	int logicalpid=atoi(argv[2]);
	int numprocesses=atoi(argv[1]);

    char* requesttype;
    char requeststring[100];
    int address;
    int numreferences;
    int terminterval;
	fprintf(stderr, "Hello from user process! My number is %d(%d)\n",logicalpid,getpid());
	
	//get shared memory
	key_t sckey;
	if((sckey=ftok(".",13))==-1)
	{
		perror("User process failed to get system clock key");
		return 1;
	}
	if((scid=shmget(sckey,sizeof(systemclock_t),PERM))==-1)
	{
		perror("User process failed to get shared memory segment for system clock");
		return 1;
	}
	if((sc=(systemclock_t *)shmat(scid,NULL,0))==(void*)-1)
	{
		perror("User process failed to attach shared memory segment for system clock");
		if(shmctl(scid,IPC_RMID,NULL)==-1)
			perror("User process failed to remove memory segment for system clock");
		return 1;
	}
	//fprintf(stderr,"USER: Current system time is %d:%06d\n",sc->sec,sc->nsec);

	key_t pcbkey;
	if((pcbkey=ftok(".",27))==-1)
	{
		perror("User process failed to get process control block key");
		return 1;
	}
	if((pcbid=shmget(pcbkey,sizeof(processcontrolblock_t)*numprocesses,PERM))==-1)
	{
		perror("User process failed to get shared memory segment for process control block");
		return 1;
	}
	if((pcb=(processcontrolblock_t *)shmat(pcbid,NULL,0))==(void*)-1)
	{
		perror("User process failed to attach shared memory segment for process control block");
		if(shmctl(pcbid,IPC_RMID,NULL)==-1)
			perror("User process failed to remove memory segment for process control block");
		return 1;
	}
	fprintf(stderr, "User %d: I have %dK memory\n",logicalpid,pcb[logicalpid].size);
	
	srand(getpid());
	terminterval=randomint(900,1100);

	//get access to message queue
	key_t msgkey;
	int msgid;
	if((msgkey=ftok(".",32))==-1)
	{
		perror("Failed to return message queue key");
		return 1;
	}
	if((msgid=msgget(msgkey, PERM))==-1)
	{
		perror("Failed to get message queue");
		return 1;
	}

	int size;
	int sec=0;
	int nsec=0;
	mymsg_t mymsg;
	int segfault=0;
	int notdone=1;
	while(notdone)
	{
	    
		//Determine if process will generate a segmentation fault
		if(randomint(1,100)<=SEGFAULTPERCENT)
		{	
		    segfault=1;
		}
		else
		{
		    segfault=0;
		}
	
    	if(numreferences>=terminterval)
        {
    		//Determine if process will terminate
    		if(randomint(1,100)<=TERMINATIONPERCENT)
    		{	
    			notdone=0;
    		}
    		numreferences=0;
        }
		
        if(notdone==0)
        {
            // terminating
            requesttype="terminate";
            address=-1;
        }
        else if(randomint(1,100)<=WRITEPERCENT)
        {
            // request a write
            requesttype="write";
            if(segfault==1)
            {
                address=randomint(pcb[logicalpid].size*PAGESIZE,pcb[logicalpid].size*PAGESIZE+PAGESIZE);
            }
            else
            {
                address=randomint(0,pcb[logicalpid].size*PAGESIZE-1);
            }
        }
        else
        {
            // request a read
            requesttype="read";
            if(segfault==1)
            {
                address=randomint(pcb[logicalpid].size*PAGESIZE,pcb[logicalpid].size*PAGESIZE+PAGESIZE);
            }
            else
            {
                address=randomint(0,pcb[logicalpid].size*PAGESIZE-1);
            }
        }
        sprintf(requeststring,"%s,%d,%d",requesttype,logicalpid,address);
        
		//send message
		memcpy(mymsg.mtext,requeststring,100);
		mymsg.mtype=getppid();
		if(msgsnd(msgid,&mymsg,100,0)==-1)
		{
			perror("User process failed to send message");
			return 1;
		}
		fprintf(stderr,"USER %d: Message sent: %s\n",logicalpid,mymsg.mtext);
		
		//receive message
		memcpy(mymsg.mtext,"",100);
		if((size=msgrcv(msgid,&mymsg,100,getpid(),0))==-1)
		{
			perror("User process failed to receive message");
			return 1;
		}
		//fprintf(stderr,"USER %d: Message received: %s\n",logicalpid,mymsg.mtext);

        numreferences++;
	}

	fprintf(stderr,"User process %d is completing\n",logicalpid);
	return 0;
}
