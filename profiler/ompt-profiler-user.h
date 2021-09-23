#include <omp-tools.h>

enum ompt_profiler_regions {
  ompt_profiler_region_begin = 65,
  ompt_profiler_region_end = 66,
  ompt_profiler_region_register = 67,
  ompt_profiler_region_begin_name = 68,
  ompt_profiler_region_end_name = 69
};

struct ompt_region_data_t {
    const char* name;
    ompt_data_t data;
};

#ifdef OMPT_PROFILER_NO_USER
#define OMPT_REGION_DEFINE(name)
#define OMPT_REGION_REGISTER(name, id)
#define OMPT_REGION_BEGIN(name)
#define OMPT_REGION_END(name)
#define OMPT_REGION_BEGIN_ID(id)
#define OMPT_REGION_END_ID(id)
#else
#define OMPT_REGION_DEFINE(name) struct ompt_region_data_t ompt_region_##name={#name, ompt_data_none}
#define OMPT_REGION_REGISTER(name, id)                                         \
  do {                                                                         \
    omp_control_tool(ompt_profiler_region_register, 0, &ompt_region_##name);   \
    id = ompt_region_##name.data.value;                                        \
  } while (0)
#define OMPT_REGION_BEGIN(name)                                                \
  omp_control_tool(ompt_profiler_region_begin, 0, &ompt_region_##name)
#define OMPT_REGION_END(name)                                                  \
  omp_control_tool(ompt_profiler_region_end, 0, &ompt_region_##name)
#define OMPT_REGION_BEGIN_ID(id)                                               \
  omp_control_tool(ompt_profiler_region_begin, id, NULL)
#define OMPT_REGION_END_ID(id)                                                 \
  omp_control_tool(ompt_profiler_region_end, id, NULL)
#endif
