#include <windows.h>
#include <stdio.h>
#include "headers\blockingqueue.h"

// the semaphore must be the first for knowing when to release it
#define SEMAPHOREINDEX 0
#define TOKENINDEX 1
#define STOPPEDEVENTINDEX 2
#define HANDLESCOUNT 3

#define QUEUE_EMPTY -1

//#define DEBUG

PQUEUEHEAD BlockingQueue_Create(int capacity)
{
	if (capacity <= 0) return NULL;

	PQUEUEHEAD result = HeapAlloc(GetProcessHeap(), 0, sizeof(QUEUEHEAD));
	result->first = result->last = NULL;
	result->maxLength = capacity;
	result->length = 0;

	result->syncSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
	result->notifyLengthChangedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	result->queueStoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	return result;
}

int BlockingQueue_AddElemInner(PQUEUEHEAD pSelf, PWORKITEM work)
{
	if (pSelf->length >= pSelf->maxLength) {
		return QUEUE_NOSPACE;
	}

	if (!pSelf->length) {
		pSelf->last = pSelf->first = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(QUEUEELEM));
		if (!pSelf->first) {
			return QUEUE_ALLOCERROR;
		}
	} else {
		pSelf->last->next = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(QUEUEELEM));
		if (!pSelf->last->next) {
			return QUEUE_ALLOCERROR;
		}
		pSelf->last = pSelf->last->next;
	}
	(pSelf->length)++;
	pSelf->last->work = work;

	SetEvent(pSelf->notifyLengthChangedEvent);
	return QUEUE_SUCCESS;
}

// blocks if no space is available
int BlockingQueue_AddElem(PQUEUEHEAD pSelf, PWORKITEM work, HANDLE cancellationTokenEvent)
{
	if (WaitForSingleObject(pSelf->queueStoppedEvent, 0) == WAIT_OBJECT_0) {
		return QUEUE_FINISHED;
	}

	HANDLE handles[HANDLESCOUNT] = { pSelf->syncSemaphore, cancellationTokenEvent, pSelf->queueStoppedEvent };
	
	int state = 0;
	while (1) {
		DWORD waitRes = WaitForMultipleObjects(HANDLESCOUNT, handles, FALSE, INFINITE);
		DWORD err = GetLastError();
		if (waitRes - WAIT_OBJECT_0 == SEMAPHOREINDEX) {
			int result;
			switch (state) {
			case 2:
#ifdef DEBUG
				printf("ADD: length changed\n");
				fflush(stdout);
#endif
				// semaphore acquired after getting the `length changed` event, so we have to try again to insert
			case 0:
#ifdef DEBUG
				printf("ADD: semaphore ack\n");
				fflush(stdout);
#endif
				// semaphore acqiured on entry, first try to insert an element
				if ((result = BlockingQueue_AddElemInner(pSelf, work)) == QUEUE_NOSPACE) {
#ifdef DEBUG
					printf("ADD: no space\n");
					fflush(stdout);
#endif
					ReleaseSemaphore(pSelf->syncSemaphore, 1, NULL);
					handles[SEMAPHOREINDEX] = pSelf->notifyLengthChangedEvent;
					state = 1;
				} else {
#ifdef DEBUG
					printf("ADD: success, semaphore release\n");
					fflush(stdout);
#endif
					ReleaseSemaphore(pSelf->syncSemaphore, 1, NULL);
					return result;
				}
				break;
			case 1:
				// previous attempt of element insertion failed with NOSPACE, so we waited on the `length changed` event
				// now we have to wait on the semaphore again
				handles[SEMAPHOREINDEX] = pSelf->syncSemaphore;
				state = 2;
				break;
			}
		} else if (waitRes - WAIT_OBJECT_0 == STOPPEDEVENTINDEX) {
			return QUEUE_FINISHED;
		} else {
			return QUEUE_OPCANCELLED;
		}
	}
}

PWORKITEM BlockingQueue_TakeElemInner(PQUEUEHEAD pSelf, int *exitStatus)
{
	*exitStatus = QUEUE_SUCCESS;

	if (!pSelf->length) {
		*exitStatus = QUEUE_EMPTY;
		return NULL;
	}

	PQUEUEELEM temp = pSelf->first;
	pSelf->first = pSelf->first->next;
	PWORKITEM result = temp->work;
	(pSelf->length)--;

	SetEvent(pSelf->notifyLengthChangedEvent);

	if (temp) HeapFree(GetProcessHeap(), 0, temp);
	return result;
}

// blocks if no element is available
PWORKITEM BlockingQueue_TakeElem(PQUEUEHEAD pSelf, HANDLE cancellationTokenEvent, int *exitStatus)
{
	HANDLE handles[HANDLESCOUNT - 1] = { pSelf->syncSemaphore, cancellationTokenEvent };
	
	int state = 0;
	while (1) {
		DWORD waitRes = WaitForMultipleObjects(HANDLESCOUNT - 1, handles, FALSE, INFINITE);
		if (waitRes - WAIT_OBJECT_0 == SEMAPHOREINDEX) {
			int result;
			PWORKITEM workResult;
			switch (state) {
			case 2:
				// semaphore acquired after getting the `length changed` event, so we have to try again to take
#ifdef DEBUG
				printf("TAKE: length changed\n");
				fflush(stdout);
#endif
			case 0:
#ifdef DEBUG
				printf("TAKE: semaphore ack\n");
				fflush(stdout);
#endif
				// semaphore acqiured on entry, first try to take an element
				if ((workResult = BlockingQueue_TakeElemInner(pSelf, &result)) == NULL) {
					if (result == QUEUE_EMPTY) {
						if (WaitForSingleObject(pSelf->queueStoppedEvent, 0) == WAIT_OBJECT_0) {
							*exitStatus = QUEUE_FINISHED;
#ifdef DEBUG
							printf("TAKE: finished\n");
							fflush(stdout);
#endif
							return NULL;
						}
					}
#ifdef DEBUG
					printf("TAKE: semaphore release, wait for len change\n");
					fflush(stdout);
#endif
					ReleaseSemaphore(pSelf->syncSemaphore, 1, NULL);
					handles[SEMAPHOREINDEX] = pSelf->notifyLengthChangedEvent;
					state = 1;
				} else {
#ifdef DEBUG
					printf("TAKE: success, semaphore release\n");
					fflush(stdout);
#endif
					ReleaseSemaphore(pSelf->syncSemaphore, 1, NULL);
					*exitStatus = QUEUE_SUCCESS;
					return workResult;
				}
				break;
			case 1:
				// previous attempt of taking an element failed (no elements available), so we waited on the `length changed` event
				// now we have to wait on the semaphore again
				handles[SEMAPHOREINDEX] = pSelf->syncSemaphore;
				state = 2;
				break;
			}
		} else {
			*exitStatus = QUEUE_OPCANCELLED;
			return NULL;
		}
	}
}

void BlockingQueue_MarkFinished(PQUEUEHEAD pSelf)
{
	SetEvent(pSelf->queueStoppedEvent);
}

void BlockingQueue_Dispose(PQUEUEHEAD pSelf, nodeReleaser releaseFunc)
{
	CloseHandle(pSelf->syncSemaphore);
	CloseHandle(pSelf->notifyLengthChangedEvent);
	CloseHandle(pSelf->queueStoppedEvent);
	PQUEUEELEM curElem = pSelf->first;
	while (curElem != NULL) {
		PQUEUEELEM temp = curElem->next;
		releaseFunc(curElem->work);
		curElem = temp;
	}
}
