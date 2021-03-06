// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

/* Local headers */
#include <semaphores.h>
#include <lib.h>
#include <memoryManager.h>
#include <resourceNamerADT.h>
#include <scheduler.h>
#include <string.h>
#include <waitQueueADT.h>

typedef struct {
    uint8_t value;
    TLock lock;
    uint8_t linkedProcesses;
    const char* name;
    TWaitQueue waitingProcesses; 
} TSemaphore;

static TSemaphore* semaphores[MAX_SEMAPHORES] = {NULL};
static TResourceNamer namer;
static TLock generalLock;

extern int _spin_lock(TLock* lock);
extern void _unlock(TLock* lock);
static int sem_free(TSem sem);
static int isValidSemId(TSem sem);
static int adquireSem(TSem sem);

static int sem_free(TSem sem) {
    int value = rnm_unnameResource(namer, semaphores[sem]->name) == NULL;
    value += wq_free(semaphores[sem]->waitingProcesses);
    value += mm_free(semaphores[sem]);
    semaphores[sem] = NULL;
    if (value != 0)
        return SEM_FAILED;
    return SEM_SUCCES;
}

static int isValidSemId(TSem sem) {
    return sem > 0 && sem < MAX_SEMAPHORES && semaphores[sem] != NULL;
}

static int adquireSem(TSem sem) {
    _spin_lock(&generalLock);

    if (!isValidSemId(sem)) {
        _unlock(&generalLock);
        return SEM_NOTEXISTS;
    }

    _spin_lock(&(semaphores[sem]->lock));
    _unlock(&generalLock);
    return SEM_SUCCES;
}

int sem_init() {
    namer = rnm_new();
    generalLock = 0;
    if (namer != 0)
        return SEM_FAILED;
    return 0;
}

TSem sem_open(const char* name, uint8_t initialValue) {

    _spin_lock(&generalLock);

    TSem sem = (TSem)(int64_t)rnm_getResource(namer, name);

    if (sem != 0) {
        semaphores[sem]->linkedProcesses += 1;
        _unlock(&generalLock);
        return sem;
    }

    int i;
    for (i = 1; i < MAX_SEMAPHORES && semaphores[i]; ++i)
        ;

    if (i == MAX_SEMAPHORES) {
        _unlock(&generalLock);
        return SEM_FAILED;
    }

    semaphores[i] = mm_malloc(sizeof(TSemaphore));
    if (semaphores[i] == NULL) {
        _unlock(&generalLock);
        return SEM_FAILED;
    }
    semaphores[i]->value = initialValue;
    _unlock(&semaphores[i]->lock);
    semaphores[i]->linkedProcesses = 1;
    semaphores[i]->waitingProcesses = wq_new();

    if (semaphores[i]->waitingProcesses == NULL) {
        mm_free(semaphores[i]);
        _unlock(&generalLock);
        return SEM_FAILED;
    }

    if (rnm_nameResource(namer, (void*)(int64_t)i, name, &(semaphores[i]->name)) != 0) {
        wq_free(semaphores[i]->waitingProcesses);
        mm_free(semaphores[i]);
        semaphores[i] = NULL;
        _unlock(&generalLock);
        return SEM_FAILED;
    }

    _unlock(&generalLock);
    return (TSem)i;
}

int sem_close(TSem sem) {

    if (adquireSem(sem) == SEM_NOTEXISTS) {
        return SEM_NOTEXISTS;
    }

    if (semaphores[sem]->linkedProcesses == 1) {
        return sem_free(sem); // The semaphore is destroyed, so we do not care about its lock
    }

    semaphores[sem]->linkedProcesses -= 1;
    _unlock(&semaphores[sem]->lock);
    return SEM_SUCCES;
}

int sem_post(TSem sem) {

    if (adquireSem(sem) == SEM_NOTEXISTS) {
        return SEM_NOTEXISTS;
    }

    semaphores[sem]->value++;
    wq_unblockSingle(semaphores[sem]->waitingProcesses);

    _unlock(&semaphores[sem]->lock);
    return SEM_SUCCES;
}

int sem_wait(TSem sem) {

    if (adquireSem(sem) == SEM_NOTEXISTS) {
        return SEM_NOTEXISTS;
    }

    TPid cpid = sch_getCurrentPID();

    while (semaphores[sem]->value == 0) {
        wq_add(semaphores[sem]->waitingProcesses, cpid);
        _unlock(&semaphores[sem]->lock);
        sch_blockProcess(cpid);
        sch_yieldProcess();
        _spin_lock(&(semaphores[sem]->lock));
    }

    semaphores[sem]->value--;
    _unlock(&semaphores[sem]->lock);
    return SEM_SUCCES;
}

int sem_listSemaphores(TSemaphoreInfo* array, int maxSemaphores) {
    _spin_lock(&generalLock);
    int semCounter = 0;

    for (int i = 1; i < MAX_SEMAPHORES && semCounter < maxSemaphores; ++i) {
        TSemaphore* sem = semaphores[i];
        if (sem != NULL) {
            TSemaphoreInfo* info = &array[semCounter++];
            info->value = sem->value;
            info->linkedProcesses = sem->linkedProcesses;

            if (sem->name == NULL)
                info->name[0] = '\0';
            else
                strncpy(info->name, sem->name, MAX_NAME_LENGTH);

            int waitingPids = wq_getPids(sem->waitingProcesses, info->waitingProcesses, wq_count(sem->waitingProcesses));
            info->waitingProcesses[waitingPids] = -1;
        }
    }
    _unlock(&generalLock);
    return semCounter;
}