#pragma once

#include <windows.h>
#include "headers\threadpool.h"

#define QUEUE_NOSPACE 1
#define QUEUE_ALLOCERROR 2
#define QUEUE_FINISHED 3
#define QUEUE_OPCANCELLED 4
#define QUEUE_SUCCESS 0

typedef struct tagQUEUEELEM {
	PWORKITEM work;
	struct tagQUEUEELEM *next;
} QUEUEELEM, *PQUEUEELEM;

typedef struct tagQUEUEHEAD {
	HANDLE syncSemaphore;
	HANDLE notifyLengthChangedEvent; // auto reset
	HANDLE queueStoppedEvent; // manual reset
	PQUEUEELEM first;
	PQUEUEELEM last;
	int maxLength;
	int length;
} QUEUEHEAD, *PQUEUEHEAD;

typedef void (*nodeReleaser)(PWORKITEM);

PQUEUEHEAD BlockingQueue_Create(int capacity);
int BlockingQueue_AddElem(PQUEUEHEAD pSelf, PWORKITEM work, HANDLE cancellationTokenEvent);
PWORKITEM BlockingQueue_TakeElem(PQUEUEHEAD pSelf, HANDLE cancellationTokenEvent, int *exitStatus);
BOOL BlockingQueue_IsFinished(PQUEUEHEAD pSelf);
void BlockingQueue_MarkFinished(PQUEUEHEAD pSelf);
// release function is used for resource freeing and clean-up. Called with every non-null queue element as a parameter.
void BlockingQueue_Dispose(PQUEUEHEAD pSelf, nodeReleaser releaseFunc);
