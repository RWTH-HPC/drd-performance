# How To Run Measurements #

## General Information ##

The workflow for running measurements is based on [JUBE](https://apps.fz-juelich.de/jsc/jube/jube2/docu/glossar.html). 
So far, the data race benchmark consists only of specific input data for
SPEC OMP 2012 benchmark applications. The tool configuration supports to use
different Archer and ThreadSanitizer configurations, as well as Intel
Inspector XE.


## Quick Start ##
The respository is set up, so that it can be used on Claix (RWTH Aachen
University cluster) without changes.
On other systems, loading modules might be different.

### The configuration assumes environmental modules available on claix ###
Modules are loaded in these files:
- scripts/build-archer-paper.sh
- scripts/build-profiler.sh
- scripts/prepare-and-run.sh
- jube/spec_config.xml

### Bootstrap and run ###
Link an available and installed Spec OMP 2012 directory to `spec` into the 
top directory of this repository. Alternatively, unpack and install Spec OMP
2012 into this directory. Then unpack Spec-OMP2012-drd.tar.gz into this
directory.
Make sure that the modules (or similar modules) loaded in above files can be
found on your system.
Then execute:
```
bash scripts/prepare-and-run.sh
```

This script will:
- build the different versions of clang used in the Correctness'21 paper
- build the profiler
- run jube with a dummy experiment [12] x [350.md] x [base,archer,tsan,archer-noreads,tsan-noreads,archer-sampling,tsan-sampling]

The default configuration will not create batch jobs to execute the
experiments. Change "submit" in spec_master_jube.xml to sbatch for
submitting a SLURM job or run prepare-and-run.sh within a batch job.

## Before The First Measurement ##

Before the first experiment, the JUBE config file has to be configured. Change to `jube` and open `spec_config.xml`. 

The first parameter set stores some information about the platform used, but is only used for display purposes.

The second parameter set configures which PAPI counters are measured. There exist three predefined groups of counters, but in papi_userdef own counters can be specified. Note that the number of counters that can be measured simultaneously depends on the system used for the measurements. Which group is measured is decided when starting a measurement, see "Option Configurations".

The module_config parameter specifies which modules have to be loaded in order for the measurement to run. The following commands should be given in the order listed here:
- module purge // unload all loaded modules
- module load *<gcc compiler\>*
- module load *<clang compiler\>*
- module load *<jube, version 2.2.2\>* // note: version 2.2.3 has problem and is not working with our workflow so far
- module load *<papi\>* // only if hw counter measurements desired
- module load *<scorep\>* // only if measurements with Score-P desired
- module li // prints all loaded modules for verification purposes

The next parameter takes the path to the chosen application and is called "spec_rundir".

The last parameter determines the time limit for a SLURM job. *(Note: Maybe this does not work as intended, please check the generated SLURM bash files.)*


As a second file, open `spec_master_jube.xml` and configure the experiment
space:

The group `master_fixed_params` specifies comma-separated lists:
- `applications`: the apps to use for the experiment
- `compiler_variants`: the compiler / tool configurations to run
- `thread_nums`: the thread counts to use for the experiment

For an initial test, it might make sense to limit the experiment space to a
single app, selected compiler variants and a single thread count

### Dependencies ###
- Spec OMP 2012 is an external dependency for licensing reasons
  - To manually use the drd input set, untar Spec-OMP2012-drd.tar.gz into the spec directory. For use with the jube workflow, unpacking is not necessary
- gcc, clang, icc, Intel Inspector XE are necessary for profiling the different tools
- The OMPT profiler is necessary to collect the performance profiles
- python3 and matplotlib is necessary to run the scripts to generate the diagrams

#### OMPT Profiler ####

The profiler can be compiled as a subproject of a complete LLVM or LLVM/OpenMP build or standalone. The profiler relies on LLVM source code in compliler-rt. To build the profiler as part of an LLVM build, copy the profiler directory to llvm-project/openmp/tools/. Then build LLVM with clang and OpenMP as usually.
To build the profiler standalone, available clang-11 or newer is required. Also LLVM sources are required.
The following commands configure and build the profiler:
```
mkdir build-profiler && cd build-profiler
CC=clang CXX=clang++ cmake ../DR-performance/profiler/ -DSANITIZER_SYMBOLIZER_SOURCE_DIR=/path/to/llvm-project/compiler-rt/lib/sanitizer_common/
make
```
If PAPI is found during the configure step, papi can be used to collect hardware performance counters.

## Running a Measurement ##

Before starting a measurement using JUBE the output directory has to be specified: `export JUBE_RES_DIR=<path_to_output_dir>`. 

Additionally, load JUBE: `module load <jube, version 2.2.2>`. *(Note: the workflow was tested with 2.2.2; 2.2.3 has a problem causing an error with message "invalid cross-device link". Newer versions might work as well.)*

JUBE takes XML files as input. Our workflow makes use of nested JUBE calls. To start a measurement, JUBE has to be started with the corresponding master XML file. Change to `jube` and execute `jube run spec_master_jube.xml [--tag ...]`. For a list of optional tags, refer to "Optional Configurations".

The JUBE call will run the application on the SLURM batch system. After submitting it there, the control returns to the user. You can check the status of your measurements using `squeue -u $USER`. After all jobs have finished, execute `jube continue <path_to_output> --id <run_id>` (the exact command also gets printed by JUBE). *(Note: if the experiment should not be submitted to the SLURM system, change the parameter "submit" in spec_master_jube.xml from sbatch to any other batch command. Check the comments for how to use input redirector "<")*

If everything worked, the diagrams can now be generated using the script `generate_diagrams_paper.py` in `scripts`. *(Note: With changing subset of experiment space, the script might need some adjustments to draw all plots correctly)*

## Optional Configurations ##

There exist different optional tags that can be passed to JUBE to configure the measurement. All tags have to be passed after the --tag parameter and are separated using a whitespace.

### PAPI Counters ###

By default, the measurement of PAPI hardware counters is disabled. However, the measurement of different groups of PAPI counters can be enabled by passing *exactly* one of the following tags. The actually measured counters then depend on the parameters specified in the JUBE config file (see above).
- papi_caches
- papi_cycles
- papi_branches
- papi_userdef

### Call-path vs. flat profile ###

The OMPT profiler is capable of generating either a call-path or a flat profile. By default, the call-path profile is generated. If a flat profile is desired, the tag "flat_profile" has to be passed.

### Application Size ###

The SPEC OMP2012 benchmark suite comes in four different sizes: test, train, drd, ref. By default drd is used, but the others can be used by passing the size as a tag.

## Collecting the contained data ##

```
export JUBE_RES_DIR=<jube work dir>
jube run --tag profile papi_caches flat_profile -- DR-performance/jube/spec_master_jube.xml
jube continue $JUBE_RES_DIR
python3 scripts/generate_rastered_paper.py
python3 scripts/generate_diagrams.py
```  

The scripts have a path variable in the top to specify the experiment directory. The current directory contains a data directory with the results of two experiments. The generated plots are located in the plots directory.

## Running with Intel Inspector XE ##
The nested tasking in 376.kdtree leads to significant runtime overhead for Intel Inspector. We had to reduce the problem size even below test to finish within reasonable time. We suggest to modify `spec_exec_jube.xml` from `'376.kdtree-drd'  : '350000 10 2',` to `'376.kdtree-drd'  : '14000 10 2',`.