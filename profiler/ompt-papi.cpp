#define EXTERNAL extern
#include "ompt-profiler.h"
#include <mutex>

#if PAPI_VER_CURRENT < 0x06000000
std::mutex papiInitLock;
#endif

std::vector<std::string> split(const std::string& s, char delimiter)
{
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(s);
  while (std::getline(tokenStream, token, delimiter))
  {
    tokens.push_back(token);
  }
  return tokens;
}

unsigned long int getTid(){
  return tData->tId;
}

void handle_papi_error (const char* funcName, int retval)
{
  printf("PAPI error %d in (%s): %s\n", retval, funcName, PAPI_strerror(retval));
  gData->profiler_flags->papiOn=false;
  if (gData->profiler_flags->papiErrorFatal)
    exit(1);
}
  

void papiInit(){
#if PAPI_VER_CURRENT < 0x06000000
  const std::lock_guard<std::mutex> lock(papiInitLock);
#endif
  int retval = PAPI_library_init( PAPI_VER_CURRENT );
  if (retval != PAPI_VER_CURRENT && retval > 0) {
    handle_papi_error( "PAPI library version mismatch!\n", retval );
    return;
  }
  if ( retval != PAPI_VER_CURRENT ) {
    handle_papi_error( "PAPI_library_init", retval );
    return;
  }
  retval = PAPI_thread_init( getTid );
  if ( retval != PAPI_OK ) {
    handle_papi_error( "PAPI_thread_init", retval );
    return;
  }
  papiGetEventCodes(gData->eventCodes, gData->profiler_flags->papiArg, gData->profiler_flags->papiSep);
  if(gData->eventCodes.eventCodes.empty())
    gData->profiler_flags->papiOn=false;
/*  papiEventSet eventSet;
  papiCreateEventSet(eventSet ,gData->eventCodes);
  papiStartEventSet(eventSet);
  papiFiniEventSet(eventSet);*/
}

void papiInitThread(threadData& tData){
#if PAPI_VER_CURRENT < 0x06000000
  const std::lock_guard<std::mutex> lock(papiInitLock);
#endif
  papiCreateEventSet(tData.eventSet ,gData->eventCodes);
  tData.papiValues.resize(tData.eventSet.nEvents);
  papiStartEventSet(tData.eventSet);
}


void papiGetEventCodes(papiEventCodes& pEvents, std::string& papiArg, char delim)
{
    int retval;
    pEvents.eventCodes.clear();
    pEvents.eventNames.clear();
    
    std::vector<std::string> eventNames = split(papiArg, delim);
    
    if ( eventNames.empty() )
    {
        return;
    }

    for ( auto e : eventNames )
    {
        int code = -1;
        retval = PAPI_event_name_to_code(e.c_str(), &code);
        if ( retval != PAPI_OK )
        {
            handle_papi_error( "PAPI_event_name_to_code", retval );
        }

        /* Check for component 0 */
        int component = PAPI_COMPONENT_INDEX( code );
        if ( component != 0 ) 
        {
            printf("This profiler only supports native PAPI events, ignoring %s from component %d.\n", e.c_str(), component);
            continue;
        }
        pEvents.eventCodes.push_back(code);
        pEvents.eventNames.push_back(e);
    }
    assert(pEvents.eventCodes.size() == pEvents.eventNames.size());
    return;
}

void papiCreateEventSet(papiEventSet& events, const papiEventCodes& pEvents)
{
    int retval;
    events={PAPI_NULL, 0};
    
    if ( pEvents.eventCodes.empty() )
    {
        return;
    }
    retval = PAPI_create_eventset( &(events.eventSet) );
    if ( retval != PAPI_OK )
    {
        handle_papi_error( "PAPI_create_eventset", retval );
    }

    for ( auto e : pEvents.eventCodes )
    {
        /* Add event to event set */
        retval = PAPI_add_event( events.eventSet, e );
        if ( retval != PAPI_OK )
        {
            printf("PAPI_add_event(%i,%i), num events = %i, papi events:\n", events.eventSet, e, PAPI_num_events(events.eventSet));
            for(int i=0; i<pEvents.eventCodes.size(); i++)
              printf("(%s, %i), ",pEvents.eventNames[i].c_str(), pEvents.eventCodes[i]);
            printf("\n");
            handle_papi_error( "PAPI_add_event", retval );
        }
        events.nEvents++;
    }
#if 0
    retval = PAPI_add_events( events.eventSet, (int*)pEvents.eventCodes.data(), pEvents.eventCodes.size() );
    if ( retval != PAPI_OK )
    {
        handle_papi_error( "PAPI_add_events", retval );
    }
    events.nEvents = pEvents.eventCodes.size();
#endif
    return;
}

void papiStartEventSet(papiEventSet& events){
  int retval = PAPI_start( events.eventSet );
  if ( retval != PAPI_OK )
  {
      handle_papi_error( "PAPI_start", retval );
  }
}

void papiFiniEventSet(papiEventSet& events){
  std::vector<long long> values;
  values.resize(events.nEvents);
  int retval = PAPI_stop( events.eventSet , values.data());
  if ( retval != PAPI_OK )
  {
      handle_papi_error( "PAPI_start", retval );
  }
  retval = PAPI_cleanup_eventset( events.eventSet );
  if ( retval != PAPI_OK )
  {
      handle_papi_error( "PAPI_cleanup_eventset", retval );
  }
  retval = PAPI_destroy_eventset( &events.eventSet );
  if ( retval != PAPI_OK )
  {
      handle_papi_error( "PAPI_destroy_eventset", retval );
  }
}

// write PAPI register values into array "values"
void papiReadEventSet(papiEventSet& events, std::vector<long long>& values){
  int retval = PAPI_read( events.eventSet, values.data() );
  if ( retval != PAPI_OK )
  {
      handle_papi_error( "PAPI_read", retval );
  }
}

