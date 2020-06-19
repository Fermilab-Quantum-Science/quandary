# Quandary - Quantum control on HPC clusters
This project implements a parallel-in-time optimization solver for quantum control. The underlying quantum dynamics model open quantum systems, using the Lindblad master equation to evolve the density matrix in time. The control problem aims to find control pulses that realize a certain gate, i.e. drive the system to a desired target state

## Requirements:
To build this project, you need to have the following packages installed:
* Petsc [https://www.mcs.anl.gov/petsc/]
* Xbraid [https://github.com/XBraid/xbraid], on branch 'solveadjointwithxbraid'

## Installation
* Download XBraid, switch to the 'solveadjointwithxbraid' branch and build the shared library:
    - git clone https://github.com/XBraid/xbraid.git
    - cd xbraid
    - git checkout solveadjointwithxbraid
    - make braid
* Install Petsc (see Petsc manual for installation guide). Set the `PETSC_DIR` and `PETSC_ARCH` variables. Make sure to include the Petsc directory in the `LD_LIBRARY_PATH`:
 `export LD_LIBRARY_PATH = $LD_LIBRARY_PATH:$PETSC_DIR/$PETSC_ARCH`
* In the main directory of this project, adapt the beginning of the Makefile to set the path to XBraid and Petsc. 
* Type `make cleanup` to clean the build directory.
* Type `make -j main` to build the code. 

### Notes for installing Petsc
* Clone Petsc from github
* By default, Petsc will compile in debug mode. To configure petsc with compiler optimization, run
  `./configure --with-debugging=0 --with-fc=0 --with-cxx=mpicxx --with-cc=mpicc COPTFLAGS='-O3' CXXOPTFLAGS='-O3'`
* The output of `./configure` reports on how to set the `PETSC_DIR` and `PETSC_ARCH` variables
* Compile petsc with `make all test`

### Petsc on LC 
* Petc is already installed on LC machines, in the directory
`/usr/tce/packages/petsc/petsc-3.12.4-mvapich2-2.3-gcc-4.8-redhat`
* To use it, load the following modules
`module load gcc/8.1.0`
`module load mvapich2/2.3`
* Set the `PETSC_DIR` variable to point to the Petsc folder and add it to the `LD_LIBRARY_PATH`:
`export PETSC_DIR=/usr/tce/packages/petsc/petsc-3.12.4-mvapich2-2.3-gcc-4.8-redhat`
`export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PETSC_DIR`
* You'll have to do this in each new terminal unless you put all this into the `~/.bashrc` file, which should be loaded whenever you start a new terminal. You can also load the file yourself with `source ~/.bashrc`.
 
## Running
The code builds into the executable `./main`. It takes one argument being the name of the config file. The config file `AxC.cfg`, lists all possible config options. It is currently set to run the Alice-Cavity testcase (3x20 levels).
* `./main AxC.cfg` for serial run
* `srun -n9 ./main AxC.cfg` for parallel run using 9 cores

