/*
 * ompt-profiler.cpp -- OMPT based profiling tool
 */

  //===----------------------------------------------------------------------===//
  //
  // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
  // See tools/archer/LICENSE.txt for details.
  // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
  //
  //===----------------------------------------------------------------------===//



#include <omp-tools.h>
#include <iostream>
#include <unordered_map>
#include <sstream>
#include <list>
#include <stack>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <dlfcn.h>
#include <sys/resource.h>
#include <unistd.h>
#include <libunwind.h>

#define EXTERNAL
#include "ompt-profiler.h"
#include "ompt-profiler-datastructures.h"

bool callbacks_registered{false};

#define OMPT_MULTIPLEX_TOOL_NAME "PROFILER"
//#define CLIENT_TOOL_LIBRARIES_VAR "PROFILER_TOOL_LIBRARIES"
#define OMPT_MULTIPLEX_CUSTOM_GET_CLIENT_PARALLEL_DATA(d) ((callbacks_registered)?&(ToParallelData(d)->client_data):d)
#define OMPT_MULTIPLEX_CUSTOM_GET_CLIENT_TASK_DATA(d) ((callbacks_registered)?&(ToTaskData(d)->client_data):d)
#include "ompt-multiplex.h"

#include "sanitizer_symbolizer.h"


#include "ompt-profiler-user.h"

#define TASK_ACTIVE 0x100


#define SCOREP_USER_REGION_ENTER(p)
#define SCOREP_USER_REGION_END(p)
#define SCOREP_USER_REGION_INIT(a,b,c)

globalData* gData=nullptr;
__thread threadData* tData=nullptr;
//TaskData* undeferred=nullptr;

#include <sys/time.h>
double omp_get_wtime() {
  struct timespec tval;
  clock_gettime(CLOCK_MONOTONIC, &tval);
  return (double)(tval.tv_sec) + 1.0E-09 * (double)(tval.tv_nsec);
}



static uint64_t my_next_id() {
  static uint64_t ID = 0;
  uint64_t ret = __sync_fetch_and_add(&ID, 1);
  return ret;
}


void takeMeasurement(){
    if (gData->profiler_flags->timeOn)
      tData->time = omp_get_wtime();
  #ifdef USE_PAPI
    if (gData->profiler_flags->papiOn)
      papiReadEventSet(tData->eventSet, tData->papiValues);
  #endif
}


//--------------------------//
// RegionData get functions //
//--------------------------//
template<>
RegionData<globalDataItem,globalDataTime>::RegionData(RegionKind kind, const void *codeptr, uint64_t regionId, RegionData<globalDataItem,globalDataTime>* parentPtr, std::string rname)
      : rKind(kind), codeptr(codeptr), rName(rname), callPathID(regionId), gParentPtr(parentPtr), sParentPtr(parentPtr),
         timeExclusive(0), timeInclusive(0)
#ifdef USE_PAPI
        ,counterExclusive(gData->eventCodes.eventCodes.size(), 0),
        counterInclusive(gData->eventCodes.eventCodes.size(), 0)
#endif
  { 
    if (rKind == UserRegion){
      rName = "user_" + rName;
    } else{
      rName += std::to_string(regionId);
    }
  }

template<>
RegionData<threadDataItem,threadDataTime>::RegionData(RegionKind kind, const void *codeptr, uint64_t regionId, RegionData<globalDataItem,globalDataTime>* gParentPtr, 
        RegionData<threadDataItem,threadDataTime>* tParentPtr, std::string rname)
      : rKind(kind), codeptr(codeptr), rName(rname), callPathID(regionId), gParentPtr(gParentPtr), sParentPtr(tParentPtr),
         timeExclusive(0), timeInclusive(0)
#ifdef USE_PAPI
        ,counterExclusive(gData->eventCodes.eventCodes.size(), 0),
        counterInclusive(gData->eventCodes.eventCodes.size(), 0)
#endif
  { 
    if (rKind == UserRegion){
      rName = "user_" + rName;
    } else{
      rName += std::to_string(regionId);
    }
  }

std::pair<uint64_t, RegionData<globalDataItem,globalDataTime>*>& 
getGlobalRegionData(const void *codeptr, RegionKind rKind,
                    const char *name)
{
  auto key = codeptr;
  auto regionMap = xlock_safe_ptr(*(gData->flatRegionMap));
  auto regionData = regionMap->find(key);
  if (regionData != regionMap->end()) 
    return regionData->second;
  RegionData<globalDataItem, globalDataTime> *r;
  auto regionId = 0;
  if (rKind!=UserRegion)
    regionId = ++(gData->regionIds);
  if (name) {
    r = new RegionData<globalDataItem, globalDataTime>(rKind, codeptr, regionId, nullptr, std::string(name));
  } else {
    r = new RegionData<globalDataItem, globalDataTime>(rKind, codeptr, regionId, nullptr);
  }
  auto ret = regionMap->emplace(std::make_pair(key, std::make_pair(regionId, r)));
  if (!ret.second) {
    printf("Deleting redundant map entry\n");
    delete r;
  }else{
    xlock_safe_ptr(*(gData->RegionList))->push_back(r);
  }
  return ret.first->second;
}

std::pair<uint64_t, RegionData<globalDataItem,globalDataTime>*>& 
getGlobalRegionData(const void *codeptr, RegionData<globalDataItem,globalDataTime>* parentPtr, RegionKind rKind,
                    const char *name)
{
  auto key = std::make_pair(codeptr, parentPtr?parentPtr->callPathID:0);
  auto regionMap = xlock_safe_ptr(*(gData->callpathRegionMap));
  auto regionData = regionMap->find(key);
  if (regionData != regionMap->end()) 
    return regionData->second;
  RegionData<globalDataItem, globalDataTime> *r;
  auto regionId = 0;
  if (rKind!=UserRegion)
    regionId = ++(gData->regionIds);
  if (name) {
    r = new RegionData<globalDataItem, globalDataTime>(rKind, codeptr, regionId, parentPtr, std::string(name));
  } else {
    r = new RegionData<globalDataItem, globalDataTime>(rKind, codeptr, regionId, parentPtr);
  }
  r->setCallpath();
  auto ret = regionMap->emplace(std::make_pair(key, std::make_pair(regionId, r)));
  if (!ret.second) {
    printf("Deleting redundant map entry\n");
    delete r;
  }else{
    xlock_safe_ptr(*(gData->RegionList))->push_back(r);
  }
  return ret.first->second;
}

/// return RegionData ptr corresponding to the given codeptr in the threadlocal region map
RegionData<threadDataItem, threadDataTime> *
getThreadRegionData(const void *codeptr, RegionData<threadDataItem,threadDataTime>* parentPtr, RegionKind rKind,
                    const char *name) 
{
  if (gData->profiler_flags->flatProfile) {
    auto key = codeptr;
    auto regionMap = tData->flatRegionMap;
    auto threadRegion = regionMap->find(key);
    if (threadRegion != regionMap->end())
      return threadRegion->second.second;
    auto globalRegion = getGlobalRegionData(codeptr, rKind, name);
    auto *r = new RegionData<threadDataItem, threadDataTime>(rKind, codeptr, globalRegion.first, nullptr, nullptr, globalRegion.second->rName);
    auto ret = regionMap->emplace(std::make_pair(key, std::make_pair(globalRegion.first, r)));
    return ret.first->second.second;
  } else {
    auto key = std::make_pair(codeptr, parentPtr?parentPtr->gParentPtr->callPathID:0);
    auto regionMap = tData->callpathRegionMap;
    auto threadRegion = regionMap->find(key);
    if (threadRegion != regionMap->end())
      return threadRegion->second.second;
    auto globalRegion = getGlobalRegionData(codeptr, parentPtr?parentPtr->gParentPtr:nullptr, rKind, name);
    auto *r = new RegionData<threadDataItem, threadDataTime>(rKind, codeptr, globalRegion.first, globalRegion.second, parentPtr, globalRegion.second->rName);
    auto ret = regionMap->emplace(std::make_pair(key, std::make_pair(globalRegion.first, r)));
    return ret.first->second.second;
  }
}


//-------------------------//
// OMPT Callback Functions //
//-------------------------//

/// OMPT event callbacks for handling threads.

static void ompt_profiler_thread_begin(ompt_thread_t thread_type,
                                   ompt_data_t *thread_data) {
  //if(gData->profiler_flags->profilingActive){
    ParallelDataPool::ThreadDataPool = new ParallelDataPool;
    TaskDataPool::ThreadDataPool = new TaskDataPool;
//    if (!undeferred)
//      undeferred = TaskData::New(ompt_task_undeferred);
    if(!tData)
      tData = new threadData{my_next_id()};
    thread_data->ptr = tData;
  #ifdef USE_PAPI
    if (gData->profiler_flags->papiOn)
      papiInitThread(*tData);
  #endif
  //}
}


/// Called when a thread finished. Adds the measured values of the threadlocal map to the global region map.
static void ompt_profiler_thread_end(ompt_data_t *thread_data) {
  if (gData->profiler_flags->flatProfile) {
    auto regionMap = tData->flatRegionMap;
    for(auto it = regionMap->begin(); it != regionMap->end(); it++)
    {
      auto grData = xlock_safe_ptr(*(gData->flatRegionMap))->find(it->first);
      grData->second.second->addRegion(it->second.second);
    }  
  } else {
    auto regionMap = tData->callpathRegionMap;
    for(auto it = regionMap->begin(); it != regionMap->end(); it++)
    {
      auto grData = xlock_safe_ptr(*(gData->callpathRegionMap))->find(it->first);
      grData->second.second->addRegion(it->second.second);
    }  
  }

  #ifdef USE_PAPI
    if (gData->profiler_flags->papiOn)
      papiFiniEventSet(tData->eventSet);
  #endif
  // cleanup
  if (tData) {
    delete tData;
    tData = nullptr;
  }
  delete ParallelDataPool::ThreadDataPool;
  delete TaskDataPool::ThreadDataPool;
}


/// OMPT event callbacks for handling parallel regions.

// callback function for ompt_callback_parallel_begin
// called when parallel construct is encountered, before any implicit task is created for the corresponding parallel region
static void ompt_profiler_parallel_begin(ompt_data_t *parent_task_data,
                                     const ompt_frame_t *parent_task_frame,
                                     ompt_data_t *parallel_data,
                                     uint32_t requested_team_size, int flag,
                                     const void *codeptr_ra) {

  TaskData *parentPtr = ToTaskData(parent_task_data);
  ParallelData *Data = ParallelData::New(codeptr_ra, parentPtr);
  parallel_data->ptr = Data;

  if(gData->profiler_flags->profilingActive){
    SCOREP_USER_REGION_ENTER(omppar)
    takeMeasurement();
    //exklusiv stoppen
    ToTaskData(parent_task_data)->stopExclusive();
  }
}

// callback function for ompt_callback_parallel_end
// called after implicit-task-end event of thread, but before it resumes execution of the encountering task
static void ompt_profiler_parallel_end(ompt_data_t *parallel_data,
                                   ompt_data_t *parent_task_data, int flag,
                                   const void *codeptr_ra) {
  ParallelData *Data = ToParallelData(parallel_data);
  Data->Delete();
  if (gData->profiler_flags->profilingActive) {
    SCOREP_USER_REGION_END(omppar)
    // exklusiv starten (takeMeasurement from implicit task end used)
    ToTaskData(parent_task_data)->startExclusive();
  }
}


// callback function for ompt_callback_implicit_task
// called with ompt_scope_begin
// called with ompt_scope_end as endpoint after barrier synchronization of finished implicit task
static void ompt_profiler_implicit_task(ompt_scope_endpoint_t endpoint,
                                    ompt_data_t *parallel_data,
                                    ompt_data_t *task_data,
                                    unsigned int team_size,
                                    unsigned int thread_num, int type) {
  switch (endpoint) {

  case ompt_scope_begin:
  case ompt_scope_beginend:
    // initial task
    if(type & ompt_task_initial){
      ParallelData *Data = ParallelData::New(nullptr, nullptr);
      if (parallel_data)
        parallel_data->ptr = Data;
      task_data->ptr = tData->initialTask = TaskData::New(Data, type | TASK_ACTIVE);
    } 
    // other task
    else {
      task_data->ptr = TaskData::New(ToParallelData(parallel_data), type | TASK_ACTIVE);
    }


    if (ToTaskData(task_data)->profilingActive) {
      if (thread_num != 0 || type & ompt_task_initial) {
        takeMeasurement();
      }
      // exclusive start
      ToTaskData(task_data)->startExclusive();
      // inclusive start
      ToTaskData(task_data)->startInclusive();
      ToTaskData(task_data)->isStarted = true;
    }

    if (endpoint == ompt_scope_begin)
      break;
    [[clang::fallthrough]];
  case ompt_scope_end:
    if (ToTaskData(task_data)->profilingActive) {
      takeMeasurement();
      // exclusive stop
      ToTaskData(task_data)->stopExclusive();
      // inclusive stop
      ToTaskData(task_data)->stopInclusive();
    }
    if (type & ompt_task_initial) {
      //ToParallelData(parallel_data)->Delete();
    }
    ToTaskData(task_data)->Delete();
    break;
  }
}

static void ompt_profiler_sync_region(ompt_sync_region_t kind,
                                  ompt_scope_endpoint_t endpoint,
                                  ompt_data_t *parallel_data,
                                  ompt_data_t *task_data,
                                  const void *codeptr_ra) {
  if(gData->profiler_flags->profilingActive){
    switch (endpoint) {
    case ompt_scope_begin:
    case ompt_scope_beginend:
      takeMeasurement();
      // exclusive stop
      ToTaskData(task_data)->stopExclusive();
      // inclusive stop
      ToTaskData(task_data)->stopInclusive();
      ToTaskData(task_data)->inBarrier=true;
      switch (kind) {
        case ompt_sync_region_barrier_implementation:
          break;
        case ompt_sync_region_barrier_implicit:
        case ompt_sync_region_barrier_explicit:
        case ompt_sync_region_barrier: {
          SCOREP_USER_REGION_ENTER(ompbar)
          break;
        }

        case ompt_sync_region_taskwait:
          SCOREP_USER_REGION_ENTER(omptw)
          break;

        case ompt_sync_region_taskgroup:
          break;

        default:
          break;
      }
      if (endpoint == ompt_scope_begin)
        break;
      [[clang::fallthrough]];
    case ompt_scope_end:
      takeMeasurement();
      // exclusive stop
      ToTaskData(task_data)->startExclusive();
      // inclusive stop
      ToTaskData(task_data)->startInclusive();
      ToTaskData(task_data)->inBarrier=false;
      switch (kind) {
        case ompt_sync_region_barrier_implementation:
          break;

        case ompt_sync_region_barrier_implicit:
        case ompt_sync_region_barrier_explicit:
        case ompt_sync_region_barrier: {
          SCOREP_USER_REGION_END(ompbar)
          break;
        }

        case ompt_sync_region_taskwait: {
          SCOREP_USER_REGION_END(omptw)
          break;
        }

        case ompt_sync_region_taskgroup: {
          break;
        }

        default:
          break;
      }
      break;
    }
  }
}

/// OMPT event callbacks for handling tasks.

static void ompt_profiler_task_create(
    ompt_data_t *parent_task_data, /* id of parent task            */
    const ompt_frame_t *parent_frame, /* frame data for parent task   */
    ompt_data_t *new_task_data, /* id of created task           */
    int type, int has_dependences,
    const void *codeptr_ra) /* pointer to outlined function */
{
    TaskData *Data = nullptr;
    if(type & ompt_task_initial){
      printf("Trying to handle pre-5.0 initial task\n");
      ParallelData *PData = ParallelData::New(nullptr, nullptr);
      new_task_data->ptr = tData->initialTask = Data = TaskData::New(PData, type | TASK_ACTIVE);
      if (Data->profilingActive) {
        takeMeasurement();
        // exclusive start
        Data->startExclusive();
        // inclusive start
        Data->startInclusive();
        Data->isStarted = true;
      }
      return;
    } 
    // other task


    if (type & ompt_task_undeferred) {
//      Data = undeferred;
      Data = TaskData::New(type);
    } else if (type & ompt_task_explicit || type & ompt_task_target) {
      TaskData *ParentTask = ToTaskData(parent_task_data);
      Data = TaskData::New(type, codeptr_ra, ParentTask);
    }
    new_task_data->ptr = Data;
}

// callback for ompt_callback_task_schedule
// called when thread switches task at a task scheduling point, not called when switching from/to merged task
// prior_task_status indicates why the prior task was suspended
static void ompt_profiler_task_schedule(ompt_data_t *first_task_data,
                                    ompt_task_status_t prior_task_status,
                                    ompt_data_t *second_task_data) {

    TaskData *FromTask = ToTaskData(first_task_data);
    TaskData *ToTask = ToTaskData(second_task_data);

    if (ToTask->Included() || FromTask->Included()){
      if (FromTask->Included() && (prior_task_status == ompt_task_complete || prior_task_status == ompt_task_cancel || prior_task_status == ompt_task_detach))
        FromTask->Delete();
      return;
    }

    if(ToTask->profilingActive || FromTask->profilingActive){
      takeMeasurement();
    }
    // completed/canceled implicit task do not appear here, only in implicit-task-end
    // (early fulfill, late_fulfill)
    // - ignore completely
    // first canceled, second started (ompt_task_cancel)
    // - first task: if explicit stop both, if implicit stop exclusive
    // - second task: if not isStarted start both, else start exclusive
    // first not really completed, but measurement should be stopped (detach)
    // - first task: stop both
    // - second task: if not isStarted start both, else start exclusive
    // first completed and freed, second starts (complete)
    // - first task: if explicit stop both, if implicit stop exclusive
    // - second task: if not isStarted start both, else start exclusive
    // first suspended, second starts (yield, switch)
    // - first task: stop exclusive
    // - second task: if not isStarted start both, else start exclusive
    // for all explicit inclusive stops: delete first_task_data, implicit task data gets deleted in implicit task end event

    if (!(prior_task_status == ompt_task_early_fulfill || prior_task_status == ompt_task_late_fulfill)) {
      if(FromTask->profilingActive){
        if(!FromTask->inBarrier)
          FromTask->stopExclusive(); //  exclusive stop
        else
          FromTask->startInclusive(); //exclusive start
      }
      if(ToTask->profilingActive) {
        if(!ToTask->inBarrier)
          ToTask->startExclusive(); //exclusive start
        else
          ToTask->stopInclusive(); //exclusive start
        if (!(ToTask->isStarted)){ // inclusive start for explicit tasks (isStarted for implicit tasks always true)
            ToTask->startInclusive();
            ToTask->isStarted = true;
        }
      }
      if(prior_task_status == ompt_task_complete || prior_task_status == ompt_task_cancel || prior_task_status == ompt_task_detach){ // inclusive stop for explicit tasks
        if(FromTask->profilingActive){
          FromTask->stopInclusive();
        }
        FromTask->Delete();
      }
    }
    // if we switch to an inactive task, we start execution of the task
  /*  if (! (ToTask->Type & TASK_ACTIVE) ) {
      SCOREP_USER_REGION_ENTER(ToTask->regionHandle)
      ToTaskData(second_task_data)->Type |= TASK_ACTIVE; // enable task-active bit
    }else{
      // otherwise we end execution of the previous task
      SCOREP_USER_REGION_END(FromTask->regionHandle)
      ToTaskData(first_task_data)->Type &= ~TASK_ACTIVE; // disable task-active bit
    }*/
}


static int ompt_profiler_control_tool (
uint64_t command,
uint64_t modifier,
void *arg,
const void *codeptr_ra
){
  // control tool commands for starting/stopping the profiling

  //  std::cout << "In Control tool with command " << command << std::endl;
  if (command == omp_control_tool_start) {
    if (gData->profiler_flags->profilingActive == 0) {
      gData->profiler_flags->profilingActive = 1;
      gData->activeTime -= omp_get_wtime();
      takeMeasurement();
      // exclusive start
      tData->initialTask->rData->startExclusive();
      // inclusive start
      tData->initialTask->rData->startInclusive();
    }
  } 
  
  else if (command == omp_control_tool_pause) {
    if (gData->profiler_flags->profilingActive == 1) {
      gData->activeTime += omp_get_wtime();
      takeMeasurement();
      // exclusive start
      tData->initialTask->rData->stopExclusive();
      // inclusive start
      tData->initialTask->rData->stopInclusive();
      gData->profiler_flags->profilingActive = 0;
    }
  } 
  
  else if (command == omp_control_tool_flush) {
    return 1;
  } 
  
  else if (command == omp_control_tool_end) {
    if (ompt_finalize_tool)
      ompt_finalize_tool();
    gData->profiler_flags->profilingActive = 0;
  } 
  
  //
  // control tool commands for user regions
  //

  // register a new user region
  else if (command == ompt_profiler_region_register) {
    ompt_region_data_t *regData = (ompt_region_data_t *)arg;
    if (regData->data.value == 0) {
      regData->data.value = gData->maxUserID++;
      gData->userRegionNames.resize(gData->maxUserID);
      gData->userRegionNames[regData->data.value] =
        std::string(regData->name);
    }
  } 
  
  // start measurement of user region, create RegionData if not existing yet
  else if (command == ompt_profiler_region_begin &&
             gData->profiler_flags->profilingActive == 1) {
               
    // get current task
//    ompt_data_t *task_parallel_data;
    ompt_data_t *task_data;
    int exists_task =  ompt_get_task_info(0, NULL, &task_data, NULL,
                                       /*&task_parallel_data*/ NULL, NULL);                                       
    if(!exists_task)
      return 0;
    auto parentPtr = ToTaskData(task_data)->rData;
    
    // create/get region
    RegionData<threadDataItem, threadDataTime> *rData;
    if (modifier > 0) {
        return 1;
/*      if (modifier >= gData->userRegionNames.size())
        return 1;
      if (modifier >= tData->userRegionMap.size())
        tData->userRegionMap.resize(gData->maxUserID, nullptr);
      rData = tData->userRegionMap[modifier];
      if (rData == nullptr) {
        rData = getThreadRegionData(codeptr_ra, parentPtr->parentPtr, UserRegion, gData->userRegionNames[modifier].c_str());
        tData->userRegionMap[modifier] = rData;
      }*/
    }
    else {
      ompt_region_data_t *regData = (ompt_region_data_t *)arg;
      printf("Begin region '%s'\n", regData->name);
      rData = (RegionData<threadDataItem, threadDataTime> *)regData->data.ptr;
      if (!rData) {
        rData = getThreadRegionData(codeptr_ra, parentPtr, UserRegion, regData->name);
        regData->data.ptr = rData;
      }
    }
    // start measurement
    takeMeasurement();
    parentPtr->stopExclusive();
    rData->startInclusive();
    rData->startExclusive();
    
    // set region of task to started region
    ToTaskData(task_data)->rData = rData;
    
  } 
  
  else if (command == ompt_profiler_region_end &&
             gData->profiler_flags->profilingActive == 1) {
               
    // get current task
    int task_type, thread_num;
    ompt_frame_t *frame;
    ompt_data_t *task_parallel_data;
    ompt_data_t *task_data;
    int exists_task =  ompt_get_task_info(0, &task_type, &task_data, &frame,
                                       &task_parallel_data, &thread_num);                                       
    if(!exists_task)
      return 0;
    auto parentPtr = ToTaskData(task_data)->rData;
    
    // get region and stop measurement
    RegionData<threadDataItem, threadDataTime> *rData;
    if (modifier > 0) {
        return 1;
/*      if (modifier >= tData->userRegionMap.size())
        return 1;
      rData = tData->userRegionMap[modifier];*/
    } else {
      ompt_region_data_t *regData = (ompt_region_data_t *)arg;
      printf("End region '%s'\n", (const char*) regData->name);
      rData = (RegionData<threadDataItem, threadDataTime> *)regData->data.ptr;
    }
    if (!rData)
      return 1;
    takeMeasurement();
    rData->stopExclusive();
    rData->stopInclusive();
    parentPtr->startExclusive();
    
    // set region of task to parent of stopped region
//    ToTaskData(task_data)->rData = parentPtr;
    
  }
  // start measurement of user region, create RegionData if not existing yet
  else if (command == ompt_profiler_region_begin_name &&
             gData->profiler_flags->profilingActive == 1) {
               
    ompt_data_t *task_data;
    int exists_task =  ompt_get_task_info(0, NULL, &task_data, NULL,
                                       /*&task_parallel_data*/ NULL, NULL);                                       
    if(!exists_task)
      return 0;
    auto parentPtr = ToTaskData(task_data)->rData;
    
    // create/get region
    RegionData<threadDataItem, threadDataTime> *rData;
    const char *regName = (const char *)arg;
    printf("Begin region '%s'\n", regName);
    auto uRegion = tData->userRegionMap.find(std::string(regName));
    if (uRegion == tData->userRegionMap.end())
    {
      printf(" Begin region '%s' not found\n", regName);
      rData = getThreadRegionData(codeptr_ra, parentPtr, UserRegion, regName);
      tData->userRegionMap[std::string(regName)] = rData;
    }
    else
    {
      printf(" Begin region '%s' found\n", regName);
      rData = uRegion->second;
    }
    // start measurement
    takeMeasurement();
    parentPtr->stopExclusive();
    rData->startInclusive();
    rData->startExclusive();
    
    // set region of task to started region
    rData->sParentPtr = ToTaskData(task_data)->rData;
    ToTaskData(task_data)->rData = rData;
    
  } 
  
  else if (command == ompt_profiler_region_end_name &&
             gData->profiler_flags->profilingActive == 1) {
               
    ompt_data_t *task_data;
    int exists_task =  ompt_get_task_info(0, NULL, &task_data, NULL,
                                       /*&task_parallel_data*/ NULL, NULL);                                       
    if(!exists_task)
      return 0;
    auto* rData = ToTaskData(task_data)->rData;
    
    // get region and stop measurement
//    RegionData<threadDataItem, threadDataTime> *rData;
    const char *regName = (const char *)arg;
    if(rData->rKind != UserRegion) {
      printf("Expect active user region at end of %s\n", regName);
      return 1;
    }
    printf("End region '%s'\n", regName);
    if(rData->rName != std::string(regName)){
      printf("Mismatch in User region name: '%s' != '%s'\n", regName, rData->rName.c_str());
    }
    auto* parentPtr = rData->sParentPtr;
/*    auto uRegion = tData->userRegionMap.find(std::string(regName));
    if (uRegion == tData->userRegionMap.end())
    {
      printf(" End region '%s' not found\n", regName);
      rData = getThreadRegionData(codeptr_ra, parentPtr, UserRegion, regName);
      tData->userRegionMap[std::string(regName)] = rData;
    }
    else
    {
      printf(" End region '%s' found\n", regName);
      rData = uRegion->second;
    }*/
    takeMeasurement();
    rData->stopExclusive();
    rData->stopInclusive();
    parentPtr->startExclusive();
    ToTaskData(task_data)->rData = parentPtr;
    rData->sParentPtr = nullptr;
    
//    ToTaskData(task_data)->rData = rData;
    // set region of task to parent of stopped region
//    ToTaskData(task_data)->rData = parentPtr;
    
  }
  
  else {
    return 1;
  }


  return 0;
}

//----------------------//
//  OMPT Initialization //
//----------------------//


// for a given event "ename", SET_CALLBACK registers function ompt_profiler_ename as callback for ompt_callback_ename
// callback , signature , variable to store result , required support level
#define SET_OPTIONAL_CALLBACK_T(event, type, result, level)                             \
  do {                                                                                  \
    ompt_callback_##type##_t profiler_##event = &ompt_profiler_##event;                 \
    result = ompt_set_callback(ompt_callback_##event,                                   \
                                (ompt_callback_t)profiler_##event);                     \
    if (result < level)                                                                 \
      printf("Registered callback '" #event "' is not supported at " #level " (%i)\n",  \
             result);                                                                   \
  } while (0)

#define SET_CALLBACK_T(event, type)                              \
  do {                                                           \
    int res;                                                     \
    SET_OPTIONAL_CALLBACK_T(event, type, res, ompt_set_always);  \
  } while (0)

#define SET_CALLBACK(event) SET_CALLBACK_T(event, event)

static int ompt_profiler_initialize(ompt_function_lookup_t lookup, int device_num,
                                ompt_data_t *tool_data) {
  ompt_set_callback_t ompt_set_callback =
      (ompt_set_callback_t)lookup("ompt_set_callback");
  if (ompt_set_callback == NULL) {
    std::cerr << "Could not set callback, exiting..." << std::endl;
    std::exit(1);
  }
  ompt_finalize_tool =
      (ompt_finalize_tool_t)lookup("ompt_finalize_tool");

  ompt_get_parallel_info =
      (ompt_get_parallel_info_t)lookup("ompt_get_parallel_info");

  ompt_get_thread_data = (ompt_get_thread_data_t)lookup("ompt_get_thread_data");

  ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");


  if(gData->profiler_flags->papiOn || gData->profiler_flags->timeOn) {
    SET_CALLBACK(thread_begin);
    SET_CALLBACK(thread_end);
    SET_CALLBACK(parallel_begin);
    SET_CALLBACK(implicit_task);
    SET_CALLBACK(sync_region);
    SET_CALLBACK(parallel_end);

    SET_CALLBACK(task_create);
    SET_CALLBACK(task_schedule);

    SET_CALLBACK(control_tool);
    callbacks_registered = true;
  }
  tData = new threadData{my_next_id()};

  #ifdef USE_PAPI
  if (gData->profiler_flags->papiOn)
    papiInit();
  #endif

  if (gData->profiler_flags->flatProfile)
    gData->flatRegionMap = new sf::contfree_safe_ptr<std::unordered_map<const void*, std::pair<uint64_t, RegionData<globalDataItem,globalDataTime>*>>>;
  else
    gData->callpathRegionMap = new sf::contfree_safe_ptr<std::unordered_map<std::pair<const void*,uint64_t>, std::pair<uint64_t, RegionData<globalDataItem,globalDataTime>*>, pair_hash>>;

  gData->RegionList = new sf::contfree_safe_ptr<std::vector<RegionData<globalDataItem,globalDataTime>*>>;


  gData->totalTime = -omp_get_wtime();
  // gdata activeTime initialisieren, nur starten falls profiler active
  if (gData->profiler_flags->profilingActive) {
    gData->activeTime = -omp_get_wtime();
  }
  else {
    gData->activeTime = 0;
  }

  __sanitizer::SetCommonFlagsDefaults();
  {
    // Override some common flags defaults.
    __sanitizer::CommonFlags cf;
    cf.CopyFrom(*__sanitizer::common_flags());
    cf.allow_addr2line = true;
    cf.verbosity=2;
    cf.stack_trace_format = "    #%n %f %S %M";
    __sanitizer::OverrideCommonFlags(cf);
  }
  __sanitizer::InitializeCommonFlags();

  return 1; // success
}




/////////////////////////
/* ******************* */
/*    OMPT FINALIZE    */
/* ******************* */
/////////////////////////

void mergeThreadTrees(RegionData<threadDataItem, threadDataTime>* region, RegionData<globalDataItem, globalDataTime> *parentRegion);
static void printWallAndActiveTime();
static void constructCSVHeader(std::stringstream& header, bool useCsv);
static void printFlatProfile(bool useCsv, std::stringstream& csvFile);
static void printCallPathProfile(bool useCsv, std::stringstream& csvFile);
static void constructMetaDataLines(std::stringstream& stream, std::stringstream& sourceLine, const void * const region_ptr);

#ifdef USE_PAPI
static void print_papi_caches(std::vector<globalDataItem>& counter_excl, std::vector<globalDataItem>& counter_incl, std::stringstream& region_output);
#endif


/// print final measurement results
static void ompt_profiler_finalize(ompt_data_t *tool_data) {
  
  // always print wall and active time
  printWallAndActiveTime();

  // if no measurements were done, return
  if (!gData->profiler_flags->papiOn && !gData->profiler_flags->timeOn)
    return;

  std::cout << get_current_dir_name() << std::endl;
  
  // -- PRINTING -- //

  bool useCsv = (gData->profiler_flags->csvOutfile != "");
  bool flatProfile = (gData->profiler_flags->flatProfile);

  // prepare stringstream containing the header of the CSV output
  std::stringstream header;
  if (useCsv){
    constructCSVHeader(header, useCsv);
  }
  
  // prepare stringstream for CSV output containing the measurements, print measurement values to stdout
  std::stringstream csvFileStream{};
  
  if (flatProfile) {
    printFlatProfile(useCsv, csvFileStream);
  }
  else {
    printCallPathProfile(useCsv, csvFileStream);
  }

  // write header and data from map to CSV file
  if (useCsv) {
    std::cout << "Logging to " << gData->profiler_flags->csvOutfile
              << std::endl;
    std::ofstream csvFile(gData->profiler_flags->csvOutfile);
    csvFile << header.str();
    csvFile << csvFileStream.str();
    csvFile.close();
  }
  
//  delete globalRegionMap;
//  delete globalInitTask;
  if (gData->profiler_flags->flatProfile)
    delete gData->flatRegionMap;
  else
    delete gData->callpathRegionMap;
  delete gData->RegionList;
} // end of ompt_finalize


static void printWallAndActiveTime() {
  gData->totalTime += omp_get_wtime();
  if (gData->profiler_flags->profilingActive == 1) {gData->activeTime += omp_get_wtime();} // if profiler active, stop active time
  printf("JUBE WALLTIME: %.12e \n", gData->totalTime);
  printf("JUBE ACTIVE TIME: %.12e \n", gData->activeTime);
}

static void constructCSVHeader(std::stringstream& header, bool useCsv) {
  header << gData->profiler_flags->headPrefix
           << "\"Codeptr\",\"Filename\",\"Codeline\",\"Region\",";
           
  if(!gData->profiler_flags->flatProfile)
    header << "\"CallPathID\",";
    
  header << "\"Time_Exclusive\",\"Time_Inclusive\"";
    
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn && useCsv) {
      for(size_t i=0; i< gData->eventCodes.eventNames.size(); i++)
        header << ",\"" << gData->eventCodes.eventNames[i] << "_excl" << "\",\"" << gData->eventCodes.eventNames[i] << "_incl" << "\"";
      if (gData->profiler_flags->papiGroup == "CACHES")
          header << ",\"L1D_W_A = L1D_W_H_excl\",\"L1D_W_A = L1D_W_H_incl\",\"L1D_R_H_excl\",\"L1D_R_H_incl\",\"L2D_H_excl\",\"L2D_H_incl\",\"L3D_A_excl\",\"L3D_A_incl\",\"L3_H_excl\",\"L3_H_incl\",\"MEM_excl\",\"MEM_incl\"";
    }
#endif

  header << "\n";
}



static void constructMetaDataLines(std::stringstream& stream, std::stringstream& sourceLine, const void * const region_ptr) {

  auto translation = __sanitizer::Symbolizer::GetOrInit()->SymbolizePC(((__sanitizer::uptr)(region_ptr))-1); // -1 to get correct line and not next operation
    
#if 0
    if (translation->info.file)
      std::cout << "File:" << std::string(translation->info.file) << std::endl;
    std::cout << "Column:" << translation->info.column << std::endl;
    std::cout << "Address:0x" << std::hex << translation->info.address << std::dec << std::endl;
    std::cout << "Line:" << translation->info.line << std::endl;
#endif

    // store metadata of region for convenience
    std::string filename = "n/a";
    if (translation->info.file)
      filename = std::string(translation->info.file);
    filename = "\"" +  filename + "\"";
      
    std::string line = "n/a";
    if (translation->info.line)
      line = std::to_string(translation->info.line);
    line = "\"" + line + "\"";

    if (translation->info.file)
      sourceLine << region_ptr << " "
                 << std::string(translation->info.file) << ":"
                 << translation->info.line << ":" << translation->info.column;         
    
    stream << gData->profiler_flags->linePrefix 
            << "\"" << region_ptr << "\","
            << filename << "," 
            << line << "," ;

}

static void printFlatProfile(bool useCsv, std::stringstream& csvFile) {
  /*auto x_safe_map = xlock_safe_ptr(*(gData->flatRegionMap));
  for (auto it = x_safe_map->begin(); it != x_safe_map->end(); it++) {
    auto regionData = it->second.second;*/
  auto x_safe_list = xlock_safe_ptr(*(gData->RegionList));
  for (auto it = x_safe_list->begin(); it != x_safe_list->end(); it++) {
    auto regionData = *it;    std::stringstream metaData{};
    std::stringstream sourceLine{};
    std::stringstream csvLine{};

    constructMetaDataLines(metaData, sourceLine, regionData->codeptr);

    std::cout << "Exclusive: " << sourceLine.str() << " " << regionData->rName << ":"
              << regionData->timeExclusive.load() << " ("
              << (regionData->timeExclusive.load()/regionData->nThreads.load()) << ")" << std::endl;

    std::cout << "Inclusive: " << sourceLine.str() << " " << regionData->rName << ":"
              << regionData->timeInclusive.load() << " ("
              << (regionData->timeInclusive.load()/regionData->nThreads.load()) << ")" << std::endl;

    std::cout << "Exclusive: started " << regionData->startCountExclusive.load() << " stopped " << regionData->stopCountExclusive.load() << std::endl;
    std::cout << "Inclusive: started " << regionData->startCountInclusive.load() << " stopped " << regionData->stopCountInclusive.load() << std::endl;
        

    if(useCsv) {
      csvLine << metaData.str() << "\"" << regionData->rName << "\","
              << regionData->timeExclusive.load() << ","
              << regionData->timeInclusive.load();
    }

    #ifdef USE_PAPI
    if (gData->profiler_flags->papiOn) {
      for (size_t i=0; i< regionData->counterExclusive.size(); i++) {
        auto exclC = regionData->counterExclusive[i].load();
        auto inclC = regionData->counterInclusive[i].load();
        
        if (useCsv) {
          csvLine << "," << exclC << "," << inclC;
        }
        
        std::cout << "  " << std::setfill(' ') << std::setw(30)
                  << gData->eventCodes.eventNames[i] << " : " << exclC << " ("
                  << (exclC / regionData->nThreads.load()) << "), " << inclC << " ("
                  << (inclC / regionData->nThreads.load()) << ")" << std::endl;
      }
      if (gData->profiler_flags->papiGroup == "CACHES")
        print_papi_caches(regionData->counterExclusive, regionData->counterInclusive, csvLine); //TODO: check if passing works
    }
    #endif

    if (useCsv) {
      csvFile << csvLine.str() << "\n";
    }

  }
}

static void printCallPathProfile(bool useCsv, std::stringstream& csvFile) {
/*  auto x_safe_map = xlock_safe_ptr(*(gData->callpathRegionMap));
  for (auto it = x_safe_map->begin(); it != x_safe_map->end(); it++) {*/
  auto x_safe_list = xlock_safe_ptr(*(gData->RegionList));
  for (auto it = x_safe_list->begin(); it != x_safe_list->end(); it++) {
    auto regionData = *it;
    std::stringstream metaData{};
    std::stringstream sourceLine{};
    std::stringstream csvLine{};

    constructMetaDataLines(metaData, sourceLine, regionData->codeptr);

    std::cout << "Exclusive: " << sourceLine.str() << " " << regionData->rName << "("<< regionData->callPathString <<"):"
              << regionData->timeExclusive.load() << " ("
              << (regionData->timeExclusive.load()/regionData->nThreads.load()) << ")" << std::endl;

    std::cout << "Inclusive: " << sourceLine.str() << " " << regionData->rName << "("<< regionData->callPathString <<"):"
              << regionData->timeInclusive.load() << " ("
              << (regionData->timeInclusive.load()/regionData->nThreads.load()) << ")" << std::endl;

    std::cout << "Exclusive: started " << regionData->startCountExclusive.load() << " stopped " << regionData->stopCountExclusive.load() << std::endl;
    std::cout << "Inclusive: started " << regionData->startCountInclusive.load() << " stopped " << regionData->stopCountInclusive.load() << std::endl;
        

    if(useCsv) {
      csvLine << metaData.str() << "\"" << regionData->rName << "\",\""
              << regionData->callPathString << "\","
              << regionData->timeExclusive.load() << ","
              << regionData->timeInclusive.load();
    }

    #ifdef USE_PAPI
    if (gData->profiler_flags->papiOn) {
      for (size_t i=0; i< regionData->counterExclusive.size(); i++) {
        auto exclC = regionData->counterExclusive[i].load();
        auto inclC = regionData->counterInclusive[i].load();
        
        if (useCsv) {
          csvLine << "," << exclC << "," << inclC;
        }
        
        std::cout << "  " << std::setfill(' ') << std::setw(30)
                  << gData->eventCodes.eventNames[i] << " : " << exclC << " ("
                  << (exclC / regionData->nThreads.load()) << "), " << inclC << " ("
                  << (inclC / regionData->nThreads.load()) << ")" << std::endl;
      }
      if (gData->profiler_flags->papiGroup == "CACHES")
        print_papi_caches(regionData->counterExclusive, regionData->counterInclusive, csvLine); //TODO: check if passing works
    }
    #endif

    if (useCsv) {
      csvFile << csvLine.str() << "\n";
    }

  }
}

#ifdef USE_PAPI
static void print_papi_caches(std::vector<globalDataItem>& counter_excl, std::vector<globalDataItem>& counter_incl, std::stringstream& region_output) {
  std::string name;
  long long value_excl;
  long long value_incl;
  bool useCsv = (region_output.rdbuf()->in_avail() != 0);

// MEM_INST_RETIRED:ALL_STORES;MEM_INST_RETIRED:ALL_LOADS;L1D:REPLACEMENT;L2_RQSTS:ALL_CODE_RD;L2_RQSTS:ALL_DEMAND_REFERENCES;L2_RQSTS:CODE_RD_MISS;LLC_REFERENCES;LLC_MISSES
// 0                           1                          2               3                    4                              5                     6              7

  name = "L1D_W_A = L1D_W_H";
  value_excl = counter_excl[0].load();
  value_incl = counter_incl[0].load();
  std::cout << "  " << std::setfill(' ') << std::setw(30) << name << " : " << value_excl << "," << value_incl << std::endl;
  if (useCsv)
    region_output << "," << value_excl << "," << value_incl;

  name = "L1D_R_H";
  value_excl = counter_excl[1].load() - counter_excl[2].load();
  value_incl = counter_incl[1].load() - counter_incl[2].load();
  std::cout << "  " << std::setfill(' ') << std::setw(30) << name << " : " << value_excl << "," << value_incl << std::endl;
  if (useCsv)
    region_output << "," << value_excl << "," << value_incl;

  name = "L2D_H";
  value_excl = counter_excl[2].load() - counter_excl[6].load() + counter_excl[5].load();
  value_incl = counter_incl[2].load() - counter_incl[6].load() + counter_incl[5].load();
  std::cout << "  " << std::setfill(' ') << std::setw(30) << name << " : " << value_excl << "," << value_incl << std::endl;
  if (useCsv)
    region_output << "," << value_excl << "," << value_incl;

  name = "L3D_A";
  value_excl = counter_excl[6].load() - counter_excl[5].load();
  value_incl = counter_incl[6].load() - counter_incl[5].load();
  std::cout << "  " << std::setfill(' ') << std::setw(30) << name << " : " << value_excl << "," << value_incl << std::endl;
  if (useCsv)
    region_output << "," << value_excl << "," << value_incl;

  name = "L3_H";
  value_excl = counter_excl[6].load() - counter_excl[7].load();
  value_incl = counter_incl[6].load() - counter_incl[7].load();
  std::cout << "  " << std::setfill(' ') << std::setw(30) << name << " : " << value_excl << "," << value_incl << std::endl;
  if (useCsv)
    region_output << "," << value_excl << "," << value_incl;

  name = "MEM";
  value_excl = counter_excl[7].load();
  value_incl = counter_incl[7].load();
  std::cout << "  " << std::setfill(' ') << std::setw(30) << name << " : " << value_excl << "," << value_incl << std::endl;
  if (useCsv)
    region_output << "," << value_excl << "," << value_incl;
}
#endif



//------------------//
//  OMPT Start Tool //
//------------------//

extern "C"
ompt_start_tool_result_t *ompt_start_tool(unsigned int omp_version,
                                          const char *runtime_version) {
  gData = new globalData{};
  pagesize = getpagesize();
  const char *options = getenv("PROFILER_OPTIONS");
  if (options)
    std::cout << options << std::endl;
  gData->profiler_flags = new ProfilerFlags(options);
  static ompt_start_tool_result_t ompt_start_tool_result = {
      &ompt_profiler_initialize, &ompt_profiler_finalize, {.ptr=gData}};
    std::cout << "OMPT scorep regions 2"
              << std::endl;
  return &ompt_start_tool_result;
}
