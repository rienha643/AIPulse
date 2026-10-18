#include "../aipulse.c"
#define CHECK(condition, message) do { if(!(condition)) { fprintf(stderr,"FAIL: %s\n",message); return 1; } else printf("PASS: %s\n",message); } while(0)
static int lostOnce(Device device,Util* util) { (void)device; (void)util; return 15; }
static int unsupported(Device device,Util* util) { (void)device; (void)util; return 3; }
static void resetCounters(void) { utilQueries=memoryQueries=ramQueries=0; }
static void fields(unsigned int bits,int ramBits) { for(unsigned int i=0;i<count;i++) gpuFields[i]=bits; ramFields=ramBits; }
static void runWorkerBriefly(void) {
    ResetEvent(stopEvent); ResetEvent(wakeEvent);
    HANDLE thread=CreateThread(NULL,0,samplingThread,NULL,0,NULL);
    Sleep(150); SetEvent(stopEvent); WaitForSingleObject(thread,5000); CloseHandle(thread);
}
int main(void) {
    InitializeCriticalSection(&guard);
    stopEvent=CreateEventW(NULL,TRUE,FALSE,NULL); wakeEvent=CreateEventW(NULL,FALSE,FALSE,NULL);
    CHECK(initialize() && count==2,"real NVIDIA inventory discovers both GPUs");
    CHECK(displayedTenths(160000000)==1 && displayedTenths(162000000)==2,"redraw comparison follows displayed decimal rounding");
    fields(15,3); resetCounters(); sample();
    CHECK(utilQueries==count && memoryQueries==count && ramQueries==1,"visible default reads selected sensors once");
    CHECK(cards[0].loadOk && cards[0].memoryOk && cards[1].loadOk && cards[1].memoryOk,"real GPU sensors valid");
    fields(10,0); resetCounters(); sample();
    CHECK(utilQueries==count && !memoryQueries && !ramQueries,"load only skips VRAM and RAM");
    fields(12,0); resetCounters(); sample();
    CHECK(!utilQueries && memoryQueries==count && !ramQueries,"VRAM only skips load and RAM");
    fields(15,3); gpuFields[1]=0; resetCounters(); sample();
    CHECK(utilQueries==1 && memoryQueries==1,"hidden GPU is not polled");
    fields(9,3); resetCounters(); sample();
    CHECK(!utilQueries && !memoryQueries && ramQueries==1 && !nvml,"RAM/name-only mode unloads NVML");
    CHECK(ram.ullTotalPhys>0,"physical RAM query remains valid without NVML");
    fields(15,3); retryAt=0; resetCounters(); sample();
    CHECK(nvmlReady && cards[0].loadOk && cards[1].memoryOk,"GPU queries resume after re-enabling");
    char originalUuid[96]; strcpy(originalUuid,cards[0].uuid);
    UtilFn realUtil=getUtil; getUtil=unsupported; sample();
    CHECK(nvmlReady && !cards[0].loadOk && cards[0].memoryOk,"unsupported load does not hide valid VRAM or reconnect");
    getUtil=lostOnce; sample();
    CHECK(!nvmlReady && retryAt>GetTickCount64(),"simulated GPU loss disconnects with retry backoff");
    unsigned long long attempts=reconnects; sample();
    CHECK(reconnects==attempts,"retry backoff avoids repeated initialization");
    retryAt=0; sample();
    CHECK(nvmlReady && cards[0].loadOk && cards[0].memoryOk && !strcmp(originalUuid,cards[0].uuid),"automatic reconnect restores real sensors by UUID");
    (void)realUtil;
    InterlockedExchange(&forceReconnect,1); sample();
    CHECK(!atomicRead(&forceReconnect) && nvmlReady && cards[1].memoryOk,"resume reconnect request restores both GPUs");
    resetCounters(); InterlockedExchange(&hidden,1); runWorkerBriefly();
    CHECK(!utilQueries && !memoryQueries && !ramQueries,"hidden worker performs no sensor queries");
    InterlockedExchange(&hidden,0); InterlockedExchange(&screenOff,1); runWorkerBriefly();
    CHECK(!utilQueries && !memoryQueries && !ramQueries,"screen-off worker performs no sensor queries");
    InterlockedExchange(&screenOff,0); InterlockedExchange(&sessionLocked,1); runWorkerBriefly();
    CHECK(!utilQueries && !memoryQueries && !ramQueries,"locked worker performs no sensor queries");
    InterlockedExchange(&sessionLocked,0); InterlockedExchange(&suspended,1); runWorkerBriefly();
    CHECK(!utilQueries && !memoryQueries && !ramQueries,"suspended worker performs no sensor queries");
    InterlockedExchange(&suspended,0); runWorkerBriefly();
    CHECK(utilQueries==count && memoryQueries==count && ramQueries==1,"visible worker resumes immediately");
    resetCounters(); ResetEvent(stopEvent); ResetEvent(wakeEvent); InterlockedExchange(&hidden,1);
    HANDLE wakingThread=CreateThread(NULL,0,samplingThread,NULL,0,NULL);
    Sleep(100); InterlockedExchange(&hidden,0); SetEvent(wakeEvent); Sleep(200);
    SetEvent(stopEvent); WaitForSingleObject(wakingThread,5000); CloseHandle(wakingThread);
    CHECK(utilQueries==count && memoryQueries==count,"wake event interrupts indefinite hidden wait");
    disconnectNvml(); CloseHandle(stopEvent); CloseHandle(wakeEvent); DeleteCriticalSection(&guard);
    return 0;
}
