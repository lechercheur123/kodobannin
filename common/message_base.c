#include <stddef.h>
#include <string.h>
#include <app_util.h>
#include <nrf_soc.h>
#include "message_base.h"
#include "util.h"

#ifdef MSG_BASE_USE_HEAP
#include "heap.h"
#endif

#define POOL_OBJ_SIZE(buf_size) (buf_size + sizeof(MSG_Data_t))

#ifndef MSG_BASE_USE_HEAP
static struct{
    uint8_t pool[POOL_OBJ_SIZE(MSG_BASE_DATA_BUFFER_SIZE) * MSG_BASE_SHARED_POOL_SIZE];
}self;
#endif

static inline uint8_t decref(MSG_Data_t * obj){
    if(obj->ref){
        obj->ref -= 1;
    }
}
static inline uint8_t incref(MSG_Data_t * obj){
    obj->ref += 1;
}

uint32_t MSG_Base_FreeCount(void){
#ifdef MSG_BASE_USE_HEAP
    size_t free;
    
    CRITICAL_REGION_ENTER();
    free = xPortGetFreeHeapSize();
    CRITICAL_REGION_EXIT();
	return free;
#else
	uint32_t step_size = POOL_OBJ_SIZE(MSG_BASE_DATA_BUFFER_SIZE);
    uint32_t step_limit = MSG_BASE_SHARED_POOL_SIZE;
    uint8_t * p = self.pool;
    uint32_t ret = 0;
    for(int i = 0; i < step_limit; i++){
        MSG_Data_t * tmp = (MSG_Data_t*)(&p[i*step_size]);
        if(tmp->ref == 0){
            ret++;
        }
    }
    return ret;
#endif
}

bool MSG_Base_HasMemoryLeak(void){
#ifdef MSG_BASE_USE_HEAP
	return false; //todo stub
#else
	return MSG_Base_FreeCount() != MSG_BASE_SHARED_POOL_SIZE;
#endif
}

MSG_Data_t * INCREF MSG_Base_Dupe(MSG_Data_t * orig){
#ifdef MSG_BASE_USE_HEAP
    APP_OK(0);
#else
    MSG_Data_t * dupe = MSG_Base_AllocateDataAtomic(orig->len);
    if(dupe){
        memcpy(dupe->buf, orig->buf, orig->len);
        return dupe;
    }
#endif
    return NULL;
}
MSG_Data_t * MSG_Base_AllocateDataAtomic(size_t size){
#ifdef MSG_BASE_USE_HEAP
    void * mem;
    CRITICAL_REGION_ENTER();
    mem = pvPortMalloc(size);
    CRITICAL_REGION_EXIT();
	return (MSG_Data_t*)mem;
#else
    MSG_Data_t * ret = NULL;
    uint32_t step_size = POOL_OBJ_SIZE(MSG_BASE_DATA_BUFFER_SIZE);
    uint32_t step_limit = MSG_BASE_SHARED_POOL_SIZE;
    uint8_t * p = self.pool;
    if( size > MSG_BASE_DATA_BUFFER_SIZE ){
        return NULL;
    }
    CRITICAL_REGION_ENTER();
    for(int i = 0; i < step_limit; i++){
        MSG_Data_t * tmp = (MSG_Data_t*)(&p[i*step_size]);
        if(tmp->ref == 0){
            incref(tmp);
            ret = tmp;
            break;
        }
    }
    CRITICAL_REGION_EXIT();
    if(ret){
        ret->len = size;
        DEBUGS("+");
    }
    return ret;
#endif
}
//TODO
//this method is unsafe, switch to strncpy later
MSG_Data_t * MSG_Base_AllocateStringAtomic(const char * str){
    if(!str){
        return NULL;
    }
    return MSG_Base_AllocateObjectAtomic(str, strlen(str)+1);
}
MSG_Data_t * INCREF MSG_Base_AllocateObjectAtomic(const void * obj, size_t size){
    MSG_Data_t * ret = MSG_Base_AllocateDataAtomic(size);
    if(ret){
        if(obj){
            memcpy(ret->buf, (const uint8_t *)obj, size);
        }
    }
    return ret;
}

MSG_Status MSG_Base_AcquireDataAtomic(MSG_Data_t * d){
    if(d){
        CRITICAL_REGION_ENTER();
        incref(d);
        CRITICAL_REGION_EXIT();
        DEBUGS("+");
        return SUCCESS;
    }
    PRINTS("AcquireData FAILED! Cannot acquire NULL\r\n");
    //APP_ASSERT(0);
    return FAIL;
}


MSG_Status MSG_Base_ReleaseDataAtomic(MSG_Data_t * d){

#ifdef MSG_BASE_USE_HEAP
    CRITICAL_REGION_ENTER();
	vPortFree(d);
    CRITICAL_REGION_EXIT();	
#else
    if(d){
        CRITICAL_REGION_ENTER();
        decref(d);
#ifndef MSG_BASE_DATA_BUFFER_SIZE
        //here we free the buffer if dynamically allocated
#endif
        CRITICAL_REGION_EXIT();
        if(d->ref == 0){
            d->context = 0;
            DEBUGS("~");
        }else{
            DEBUGS("-");
        };
        return SUCCESS;
    }else{
        PRINTS("Release FAILED! Cannot free NULL\r\n");
        //APP_ASSERT(0);
        return FAIL;
    }
#endif
}
static void
_inspect(MSG_Data_t * o){
    PRINTS("O: ");
    PRINT_HEX(&o, sizeof(MSG_Data_t*));
    PRINTS(" R:");
    PRINT_HEX(&o->ref,sizeof(o->ref));
    PRINTS("\r\n");

}
MSG_Status
MSG_Base_BufferTest(void){
    MSG_Data_t * objbig = NULL;
    MSG_Data_t * obj = NULL;
    for(int i = 0; i < MSG_BASE_SHARED_POOL_SIZE; i++){
        MSG_Data_t * o = MSG_Base_AllocateDataAtomic(1); 
        if(!o)goto fail;
        //_inspect(o);
    }
    PRINTS("B");
    obj = MSG_Base_AllocateDataAtomic(1); 
    if(obj || objbig){
        PRINTS("Failed Test\r\n");
        return FAIL;
    }else{
        PRINTS("All Pass\r\n");
    }
    return SUCCESS;
fail:
    PRINTS("Failed\r\n");
    return FAIL;
}

MSG_Queue_t * MSG_Base_InitQueue(void * mem, uint32_t size){
    MSG_Queue_t * ret = NULL;
    if(size < (sizeof(MSG_Queue_t) + sizeof(MSG_Data_t*))){
        //minimum of one element is required
        ret = NULL;
    }else{
        memset(mem, 0, size);
        ret = (MSG_Queue_t *)mem;
        ret->capacity = (size - sizeof(MSG_Queue_t)) / sizeof(MSG_Data_t *);
        ret->elements = 0;
        ret->rdx = 0;
        ret->wdx = 0;
        PRINTS("Initialize size to: ");
        PRINT_HEX(&ret->capacity, 1);
        PRINTS(" elements\r\n");
    }
    return ret;
}
MSG_Status MSG_Base_QueueAtomic(MSG_Queue_t * queue, MSG_Data_t * obj){
    MSG_Status ret = FAIL;
    if(queue && obj){
        CRITICAL_REGION_ENTER();
        if(queue->elements == queue->capacity){
            ret = OOM;
        }else{
            incref(obj);
            queue->q[queue->wdx] = obj;
            queue->wdx = (queue->wdx + 1)%queue->capacity;
            queue->elements += 1;
            ret = SUCCESS;
        }
        CRITICAL_REGION_EXIT();
    }
    return ret;
}
MSG_Data_t * MSG_Base_DequeueAtomic(MSG_Queue_t * queue){
    MSG_Data_t * ret = NULL;
    if(queue){
        CRITICAL_REGION_ENTER();
        if(queue->elements){
            ret = queue->q[queue->rdx];
            queue->elements -= 1;
            queue->rdx = (queue->rdx + 1)%queue->capacity;
            decref(ret);
        }
        CRITICAL_REGION_EXIT();
    }
    return ret;
}
