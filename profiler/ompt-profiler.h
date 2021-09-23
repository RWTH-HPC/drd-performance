#pragma once
#ifdef USE_PAPI
#include <papi.h>
#endif

#include <assert.h>
#include <atomic>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <omp-tools.h>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "safe_ptr.h"

#include "ompt-atomwrapper.h"

template <typename I,typename T>
struct RegionData;

//template<typename R> using containerLocalMap = std::map<const void*, R*>;
//template<typename R> using containerLocalMap = sf::contfree_safe_ptr<std::map<const void*, R*>>;
//template<typename R> using containerGlobalMap = sf::contfree_safe_ptr<std::map<const void*, R*>>;



struct ProfilerFlags {
  std::string papiArg;
  std::string papiGroup;
  std::string headPrefix;
  std::string linePrefix;
  std::string csvOutfile{""};
  char papiSep;
  bool papiOn;
  int papiErrorFatal;
  int timeOn;
  int profilingActive;
  int flatProfile;

  bool starts_with(std::string& t, std::string& str){
    return t.substr(0, str.size()) == str;
  }
  bool starts_with(std::string& t, const char* str){
    std::string tmp{str};
    return starts_with(t, tmp);
  }


  ProfilerFlags(const char *env)
      :
        papiArg(), papiGroup(), headPrefix(), linePrefix(),  papiSep(';'), papiOn(false), papiErrorFatal(true), timeOn(true), profilingActive(true), flatProfile(false)
  {
    if (env) {
      std::vector<std::string> tokens;
      std::string token;
      std::string str(env);
      std::istringstream iss(str);
      while (std::getline(iss, token, ' '))
        tokens.push_back(token);

      for (std::vector<std::string>::iterator it = tokens.begin();
           it != tokens.end(); ++it) {
#ifdef USE_PAPI
        if (sscanf(it->c_str(), "papi_error_fatal=%d", &papiErrorFatal))
          continue;
        if ( starts_with(*it, "papi=") )
        {
          if(papiOn)
          {
            std::cerr << "Ignoring PROFILER_OPTIONS " << *it
                      << " because a previous option already uses PAPI" << std::endl;
            continue;
          }
          papiArg = it->substr(5);
          papiOn = true;
          continue;
        }
        if ( starts_with(*it, "papi_group=") )
        {
          if(papiOn)
          {
            std::cerr << "Ignoring PROFILER_OPTIONS " << *it
                      << " because a previous option already uses PAPI" << std::endl;
            continue;
          }
          papiOn = true;
          papiGroup = it->substr(11);
          if(papiGroup == "CACHES")
          {
            papiArg = "MEM_INST_RETIRED:ALL_STORES;MEM_INST_RETIRED:ALL_LOADS;L1D:REPLACEMENT;L2_RQSTS:ALL_CODE_RD;L2_RQSTS:ALL_DEMAND_REFERENCES;L2_RQSTS:CODE_RD_MISS;LLC_REFERENCES;LLC_MISSES";
            continue;
          }
          if(papiGroup == "CYCLES")
          {
            papiArg = "CYCLE_ACTIVITY:STALLS_TOTAL;CYCLE_ACTIVITY:STALLS_MEM_ANY;CYCLE_ACTIVITY:CYCLES_MEM_ANY;CYCLE_ACTIVITY:STALLS_L1D_MISS;CYCLE_ACTIVITY:CYCLES_L1D_MISS;CYCLE_ACTIVITY:STALLS_L2_MISS;CYCLE_ACTIVITY:STALLS_L3_MISS;BR_INST_RETIRED:COND";
            continue;
          }
          if(papiGroup == "BRANCHES")
          {
            papiArg = "BR_INST_RETIRED:COND;BR_INST_RETIRED:ALL_BRANCHES;BR_INST_RETIRED:NOT_TAKEN;BR_MISP_RETIRED:COND;BR_MISP_RETIRED:ALL_BRANCHES;INST_RETIRED:ALL";
            continue;
          }
          if (papiSep!=';')
          {
            std::cerr << "Resetting separator to ';' as needed for PAPI group" << std::endl;
            papiSep=';';
          }

          continue;
        }
        if (sscanf(it->c_str(), "papi_sep=%c", &papiSep))
        {
          if (papiGroup!=""){
            std::cerr << "Ignoring PROFILER_OPTIONS " << *it
                      << " because a previous option already set PAPI group" << std::endl;
            papiSep=';';
          }
          continue;
        }

#endif
        if (starts_with(*it, "line_prefix="))
        {
          linePrefix = it->substr(12);
          continue;
        }
        if (starts_with(*it, "head_prefix="))
        {
          headPrefix = it->substr(12);
          continue;
        }
        if (starts_with(*it, "csv_outfile=")) {
          csvOutfile = it->substr(12);
          continue;
        }

        if (sscanf(it->c_str(), "timer=%d", &timeOn))
          continue;

        if (sscanf(it->c_str(), "profiling_active=%d", &profilingActive))
          continue;
          
        if(sscanf(it->c_str(), "flat_profile=%d", &flatProfile))
          continue;

        std::cerr << "Illegal values for PROFILER_OPTIONS variable: " << *it
                  << std::endl;
      }
    }
  }
};

#ifdef USE_PAPI
struct papiEventSet{
  int eventSet;
  int nEvents;
};

struct papiEventCodes{
  std::vector<std::string> eventNames;
  std::vector<int> eventCodes;
};
#endif

struct pair_hash {
    std::size_t operator () (const std::pair<const void*,uint64_t> &p) const {
        return std::hash<uint64_t>{}((uint64_t)p.first ^ p.second >> 32 ^ p.second << 32);  
    }
};

struct globalData{
  ProfilerFlags *profiler_flags;
  double totalTime;
  double activeTime;
#ifdef USE_PAPI
  papiEventCodes eventCodes;
#endif
  sf::contfree_safe_ptr<std::unordered_map<std::pair<const void*,uint64_t>, std::pair<uint64_t, RegionData<globalDataItem,globalDataTime>*>, pair_hash>>* callpathRegionMap;
  sf::contfree_safe_ptr<std::unordered_map<const void*, std::pair<uint64_t, RegionData<globalDataItem,globalDataTime>*>>>* flatRegionMap;
  sf::contfree_safe_ptr<std::vector<RegionData<globalDataItem,globalDataTime>*>>* RegionList;
  std::atomic<uint64_t> regionIds{0};
  
  std::vector<std::string> userRegionNames;
//  sf::contfree_safe_ptr<std::vector<RegionData<threadDataItem,threadDataTime>*>> initialTasks;
  int maxUserID{1};
};

extern globalData* gData;

struct threadData{
  double time;
#ifdef USE_PAPI
  papiEventSet eventSet{0,0};
  std::vector<long long> papiValues;
#endif
  struct TaskData* initialTask{nullptr};
  std::unordered_map<std::pair<const void*,uint64_t>,std::pair<uint64_t, RegionData<threadDataItem,threadDataTime>*>, pair_hash>* callpathRegionMap;
  std::unordered_map<const void*,std::pair<uint64_t, RegionData<threadDataItem,threadDataTime>*>>* flatRegionMap;
//  RegionData<threadDataItem,threadDataTime>* localInitTask{nullptr};
//  std::vector<RegionData<threadDataItem, threadDataTime> *> userRegionMap;
  std::map<std::string, RegionData<threadDataItem,threadDataTime>*> userRegionMap;
  uint64_t tId;
  threadData(uint64_t tid): tId(tid){
    if (gData->profiler_flags->flatProfile)
      flatRegionMap = new std::unordered_map<const void*, std::pair<uint64_t, RegionData<threadDataItem,threadDataTime>*>>;
    else
      callpathRegionMap = new std::unordered_map<std::pair<const void*,uint64_t>, std::pair<uint64_t, RegionData<threadDataItem,threadDataTime>*>, pair_hash>;
  }
  ~threadData(){
    if (gData->profiler_flags->flatProfile)
      delete flatRegionMap;
    else
      delete callpathRegionMap;
  }
};



#ifdef USE_PAPI
void papiInit();
void papiInitThread(threadData& tData);
void papiGetEventCodes(papiEventCodes& pEvents, std::string& papiArg, char delim);
void papiCreateEventSet(papiEventSet& events, const papiEventCodes& pEvents);

void papiStartEventSet(papiEventSet& events);
void papiFiniEventSet(papiEventSet& events);
void papiReadEventSet(papiEventSet& events, std::vector<long long>& values);
#endif


/// Required OMPT inquiry functions.
EXTERNAL ompt_get_parallel_info_t ompt_get_parallel_info;
EXTERNAL ompt_get_thread_data_t ompt_get_thread_data;
EXTERNAL ompt_finalize_tool_t ompt_finalize_tool;
EXTERNAL ompt_get_task_info_t ompt_get_task_info;

extern __thread threadData* tData;



//---------------//
// RegionData    //
//---------------//


enum RegionKind {
  ParallelRegion = 0,
  ImplTaskRegion = 1,
  ExplTaskRegion = 2,
  InitTaskRegion = 3,
  BarrierRegion = 4,
  TaskwaitRegion = 5,
  LockRegion = 6,
  CriticalRegion = 7,
  OrderedRegion = 8,
  AtomicRegion = 9,
  UserRegion = 10,
  NoneRegion
};

/*RegionData<threadDataItem, threadDataTime> *
getThreadRegionData(const void *codeptr, RegionData<globalDataItem,globalDataTime>* parentPtr, RegionKind rKind,
                    const char *name = nullptr);*/

RegionData<threadDataItem, threadDataTime> *
getThreadRegionData(const void *codeptr, RegionData<threadDataItem,threadDataTime>* parentPtr, RegionKind rKind,
                    const char *name = nullptr);



/// store timer and counter values for one region (1 region per codeptr)
template <typename I, typename T> struct RegionData {
  // identification members
  RegionKind rKind;
  const void *codeptr{nullptr};
  std::string rName;
  uint64_t callPathID;
  RegionData<globalDataItem,globalDataTime>* gParentPtr{nullptr};
  RegionData<I,T>* sParentPtr{nullptr};

  // static members
  std::atomic<int> children{0};
  std::string callPathString{"1"};

  // measurement-related members
  T timeExclusive;
  T timeInclusive;
  I startCountExclusive{0}, stopCountExclusive{0};
  I startCountInclusive{0}, stopCountInclusive{0};
  I nThreads{0};

#ifdef USE_PAPI
  std::vector<I> counterExclusive;
  std::vector<I> counterInclusive;
#endif

  // constructors
  RegionData(RegionKind kind, const void *codeptr, uint64_t regionId, RegionData<I,T>* parentPtr, std::string rname);
  RegionData(RegionKind kind, const void *codeptr, uint64_t regionId, RegionData<globalDataItem,globalDataTime>* gParentPtr, RegionData<I,T>* tParentPtr, std::string rname);

  RegionData(RegionKind kind, const void *codeptr, uint64_t regionId, RegionData<I,T>* parentPtr)
      : RegionData(kind, codeptr, regionId, parentPtr, regionKindToString(kind)) {}

  RegionData() : RegionData(NoneRegion, nullptr, nullptr) {callPathID = 0;}

  void setCallpath(){
    if (gParentPtr) {
      callPathString = gParentPtr->callPathString + "." + std::to_string(++(gParentPtr->children));
      printf("%s: Creating RegionData %s: %li, %p\n", __PRETTY_FUNCTION__, callPathString.c_str(), callPathID, codeptr);
    }
  }

private:
  std::string regionKindToString(RegionKind rKind) {
    switch (rKind) {
    case ParallelRegion:
      return "ParallelRegion";
      break;
    case ImplTaskRegion:
      return "ImplTaskRegion";
      break;
    case ExplTaskRegion:
      return "ExplTaskRegion";
      break;
    case InitTaskRegion:
      return "InitTaskRegion";
      break;
    case BarrierRegion:
      return "BarrierRegion";
      break;
    case TaskwaitRegion:
      return "TaskwaitRegion";
      break;
    case LockRegion:
      return "LockRegion";
      break;
    case CriticalRegion:
      return "CriticalRegion";
      break;
    case OrderedRegion:
      return "OrderedRegion";
      break;
    case AtomicRegion:
      return "AtomicRegion";
      break;
    case UserRegion:
      return "UserRegion";
      break;
    case NoneRegion:
      return "NoneRegion";
      break;
    }
    return "";
  }

public:
/***--------------------------------------------***/
/***--- Functions to start/stop measurements ---***/
/***--------------------------------------------***/

  // start the measurement of time and PAPI counters for the region
  // basic idea: value_region = stopvalue - startvalue so
  // at start: value -= startvalue
  // at stop: value += stopvalue
  void startExclusive() 
  {
    startCountExclusive++;
    if (gData->profiler_flags->timeOn) {
      timeExclusive.add(-tData->time);
    }
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn) {
      for (size_t i = 0; i < tData->papiValues.size(); i++)
        counterExclusive[i].add(-tData->papiValues[i]);
    }
#endif
  }

  void startInclusive() 
  {
    startCountInclusive++;
    if (gData->profiler_flags->timeOn) {
      timeInclusive.add(-tData->time);
    }
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn) {
      for (size_t i = 0; i < tData->papiValues.size(); i++)
        counterInclusive[i].add(-tData->papiValues[i]);
    }
#endif
  }

  // stop the measurement of time and PAPI counters for the region
  // basic idea: see start
  void stopExclusive() 
  {
    stopCountExclusive++;
    if (gData->profiler_flags->timeOn)
      timeExclusive.add(tData->time);
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn) {
      for (size_t i = 0; i < tData->papiValues.size(); i++)
        counterExclusive[i].add(tData->papiValues[i]);
    }
#endif
  }

  void stopInclusive() 
  {
    stopCountInclusive++;
    if (gData->profiler_flags->timeOn)
      timeInclusive.add(tData->time);
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn) 
    {
      for (size_t i = 0; i < tData->papiValues.size(); i++)
        counterInclusive[i].add(tData->papiValues[i]);
    }
#endif
  }

/***--------------------------------------------***/
/***----- Function to add up measurements ------***/
/***--------------------------------------------***/
  template <typename IO, typename TO>
  void addRegion(RegionData<IO, TO> *other) {
    nThreads.add(1);
    // add exclusive times and counters
    timeExclusive.add(other->timeExclusive.load());
    startCountExclusive.add(other->startCountExclusive.load());
    stopCountExclusive.add(other->stopCountExclusive.load());
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn)
      for (size_t i = 0; i < other->counterExclusive.size(); i++)
        counterExclusive[i].add(other->counterExclusive[i].load());
#endif
    // add inclusive times and counters
    timeInclusive.add(other->timeInclusive.load());
    startCountInclusive.add(other->startCountInclusive.load());
    stopCountInclusive.add(other->stopCountInclusive.load());
#ifdef USE_PAPI
    if (gData->profiler_flags->papiOn)
      for (size_t i = 0; i < other->counterInclusive.size(); i++)
        counterInclusive[i].add(other->counterInclusive[i].load());
#endif
  }
}; // end of RegionData
