#pragma once
#include "ompt-profiler.h"

//---------------------------//
//   DataPool and PoolData   //
//---------------------------//  

extern "C" double omp_get_wtime();

static int pagesize{0};

// Data structure to provide a threadsafe pool of reusable objects.
// DataPool<Type of objects>
template <typename T> struct DataPool {
  static __thread DataPool<T> *ThreadDataPool;
  std::mutex DPMutex;
  
  // store unused objects
  std::vector<T *> DataPointer{};
  std::vector<T *> RemoteDataPointer{};

  // store all allocated memory to finally release
  std::list<void *> memory;
  
  // count remotely returned data (RemoteDataPointer.size())
  std::atomic<int> remote{0};
  
  // totally allocated data objects in pool
  int total{0};
#ifdef DEBUG_DATA
  int remoteReturn{0};
  int localReturn{0};

  int getRemote() { return remoteReturn + remote; }
  int getLocal() { return localReturn; }
#endif
  int getTotal() { return total; }
  int getMissing() {
    return total - DataPointer.size() - RemoteDataPointer.size();
  }

  // fill the pool by allocating a page of memory
  void newDatas() {
    if (remote > 0) {
      const std::lock_guard<std::mutex> lock(DPMutex);
      // DataPointer is empty, so just swap the vectors
      DataPointer.swap(RemoteDataPointer);
      remote = 0;
      return;
    }
    // calculate size of an object including padding to cacheline size
    size_t elemSize = sizeof(T);
    size_t paddedSize = (((elemSize - 1) / 64) + 1) * 64;
    // number of padded elements to allocate
    int ndatas = pagesize / paddedSize;
    char *datas = (char *)malloc(ndatas * paddedSize);
    memory.push_back(datas);
    for (int i = 0; i < ndatas; i++) {
      DataPointer.push_back(new (datas + i * paddedSize) T(this));
    }
    total += ndatas;
  }

  // get data from the pool
  T *getData() {
    T *ret;
    if (DataPointer.empty())
      newDatas();
    ret = DataPointer.back();
    DataPointer.pop_back();
    return ret;
  }

   // accesses to the thread-local datapool don't need locks
  void returnOwnData(T *data) {
    DataPointer.emplace_back(data);
#ifdef DEBUG_DATA
    localReturn++;
#endif
  }

  // returning to a remote datapool using lock
  void returnData(T *data) {
    const std::lock_guard<std::mutex> lock(DPMutex);
    RemoteDataPointer.emplace_back(data);
    remote++;
#ifdef DEBUG_DATA
    remoteReturn++;
#endif
  }

  ~DataPool() {
    // we assume all memory is returned when the thread finished / destructor is
    // called
    for (auto i : DataPointer)
      if (i)
        i->~T();
    for (auto i : RemoteDataPointer)
      if (i)
        i->~T();
    for (auto i : memory)
      if (i)
        free(i);
  }
};

template <typename T> struct PoolData {
  DataPool<T> *owner;

  ompt_data_t client_data{0};
  
  static T *New() { return DataPool<T>::ThreadDataPool->getData(); }

  void Delete() {
    static_cast<T *>(this)->Reset();
    if (owner == DataPool<T>::ThreadDataPool)
      owner->returnOwnData(static_cast<T *>(this));
    else
      owner->returnData(static_cast<T *>(this));
  }

  PoolData(DataPool<T> *dp) : owner(dp) {}
};


//--------------//
// ParallelData //
//--------------//

struct ParallelData;
typedef DataPool<ParallelData> ParallelDataPool;
template <>
__thread ParallelDataPool *ParallelDataPool::ThreadDataPool = nullptr;


/// Data structure to store additional information for parallel regions.
struct ParallelData final : PoolData<ParallelData> {
  // The stored codePtr can be used by implicit tasks to get access to their RegionData.
  const void *codePtr;
  TaskData* parentTask{nullptr};

  ParallelData *Init(const void *codeptr, TaskData* parenttask) 
  { codePtr = codeptr; parentTask=parenttask;  return this;  }

  void Reset() {client_data=ompt_data_none;}

  static ParallelData *New(const void *codeptr, TaskData* parenttask) {
    return PoolData<ParallelData>::New()->Init(codeptr, parenttask);
  }

  ParallelData(DataPool<ParallelData> *dp) : PoolData<ParallelData>(dp) {}
};

static inline ParallelData *ToParallelData(ompt_data_t *parallel_data) {
  return reinterpret_cast<ParallelData *>(parallel_data->ptr);
}

//--------------//
//   TaskData   //
//--------------//

struct TaskData;
typedef DataPool<TaskData> TaskDataPool;
template <> __thread TaskDataPool *TaskDataPool::ThreadDataPool = nullptr;

/// Data structure to store additional information for tasks.
struct TaskData final : PoolData<TaskData> {
  /// task type.
  int Type{0}; // explicit or implicit or initial (ompt_task_initial,... bitmasks, enum ompt_task_flag)
  const void* codePtr{nullptr};
  TaskData* parentTask{nullptr};
  bool isStarted{false};
  bool inBarrier{false};
  bool profilingActive{true};

  RegionData<threadDataItem,threadDataTime>* rData{nullptr};

  bool Included() {return Type & ompt_task_undeferred;}

  // initial task
  TaskData *Init(int type, RegionData<threadDataItem, threadDataTime> *data) {
        Type=type;
        rData=data;
        
        return this;
      }

  // undeferred task, no profiling
  TaskData *Init(int type) {
        Type=type;
        profilingActive=false;
        return this;
      }

  // explicit task, (thread-local) rData is bound on start of execution
  TaskData *Init(int type, const void* codeptr, TaskData* parenttask) {
        Type=type;
        codePtr=codeptr;
        parentTask = parenttask;
        profilingActive=gData->profiler_flags->profilingActive;
        
        return this;
      }

  // implicit task, use codeptr from parallel region, bind rData on creation
  TaskData *Init(ParallelData *Team, int type) {
        Type=type;
        codePtr=Team->codePtr;
        parentTask=Team->parentTask;
        profilingActive=gData->profiler_flags->profilingActive;
        rData=getThreadRegionData(Team->codePtr, (parentTask)?parentTask->rData:nullptr, ImplTaskRegion);
        
        return this;
      }
        
  void Reset() {
    Type = 0;
    codePtr=nullptr;
    isStarted = false;
    parentTask = nullptr;
    profilingActive = true;
    rData = nullptr;
    client_data = ompt_data_none;
  }
  
  // initial task
  static TaskData *New(int type, RegionData<threadDataItem, threadDataTime> *data) {
    return PoolData<TaskData>::New()->Init(type, data);
  }
  
  // undeferred task, no profiling
  static TaskData *New(int type) {
    return PoolData<TaskData>::New()->Init(type);
  }
  
  // explicit task, (thread-local) rData is bound on start of execution
  static TaskData *New(int type, const void* codeptr, TaskData* parentTask) {
    return PoolData<TaskData>::New()->Init(type, codeptr, parentTask);
  }
  
  // implicit task, use codeptr from parallel region, bind rData on creation
  static TaskData *New(ParallelData *Team, int type) {
    return PoolData<TaskData>::New()->Init(Team, type);
  }
  
  TaskData(DataPool<TaskData> *dp) : PoolData<TaskData>(dp) {}

  // start exclusive time and counter measurement for this task
  void startExclusive()
  {
    if ((Type & ompt_task_initial) && !gData->profiler_flags->profilingActive)
      return;
    if (Type & ompt_task_explicit){
      rData = getThreadRegionData(codePtr, parentTask->rData, ExplTaskRegion); // before start not clear on which thread explicit task is running, so fetch threadlocal rData here
    }
    assert(! (Type & ompt_task_undeferred));
    assert(rData != nullptr &&"startExclusive() should not be called for deferred tasks");
    rData->startExclusive();
  }

  // start inclusive time and counter measurement for this task
  void startInclusive()
  {
    if ((Type & ompt_task_initial) && !gData->profiler_flags->profilingActive)
      return;
    if (Type & ompt_task_explicit){
      rData = getThreadRegionData(codePtr, parentTask->rData, ExplTaskRegion); // before start not clear on which thread explicit task is running, so fetch threadlocal rData here
    }
    assert(! (Type & ompt_task_undeferred));
    assert(rData != nullptr &&"startInclusive() should not be called for deferred tasks");
    rData->startInclusive();
  }

  // stop exclusive time and counter measurement for this task
  void stopExclusive()
  {
    if ((Type & ompt_task_initial) && !gData->profiler_flags->profilingActive)
      return;
    assert(! (Type & ompt_task_undeferred));
    assert(rData != nullptr);
    rData->stopExclusive();
  }

  // stop inclusive time and counter measurement for this task
  void stopInclusive()
  {
    if ((Type & ompt_task_initial) && !gData->profiler_flags->profilingActive)
      return;
    assert(! (Type & ompt_task_undeferred));
    assert(rData != nullptr);
    rData->stopInclusive();
  }

};


static inline TaskData *ToTaskData(ompt_data_t *task_data) {
  return reinterpret_cast<TaskData *>(task_data->ptr);
}

