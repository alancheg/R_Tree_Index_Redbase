### Overview
This project is a modified clone of the [Stanford Redbase.](https://web.stanford.edu/class/cs346/2015/redbase.html). I have implemented the Indexing Component to add an [R-Tree](http://dl.acm.org/citation.cfm?id=602266) index to it.

### Structure
Redbase is divided into four components:

```
                          +----------------------------------------+
                          |            Query Language              | 
                          +----------------------------------------+
                          |           System Management            |
                          +-------------------+--------------------+
R-Trees is added here---> |     Indexes       |  Record Management |
                          +-------------------+--------------------+
                          |              Paged File                |
                          +----------------------------------------+
```
1. **Paged File** - The PF component provides facilities for higher-level client components to perform file I/O in terms of pages. In the PF component, methods are provided to create, destroy, open, and close paged files, to scan through the pages of a given file, to read a specific page of a given file, to add and delete pages of a given file, and to obtain and release pages for scratch use. It also implements the buffer pool for use by the other componenets. The C++ API for the PF component is available [here](https://web.stanford.edu/class/cs346/2015/redbase-pf.html). We do not need to make any changes at this layer.

2. **Record Management** -  The RM component provides classes and methods for managing files of unordered records.  It has been implemented and no changes is required here. The API for this component is available [here](https://web.stanford.edu/class/cs346/2015/redbase-rm.html).  

3. **Indexing** - The IX component provides classes and methods for managing persistent indexes over unordered data records stored in paged files. Each data file may have any number of R-tree indexes associated with it. The indexes ultimately will be used to speed up processing of relational selections, joins, and condition-based update and delete operations. Like the data records themselves, the indexes are stored in paged files. This component is similar to the RM component and some code have be reused. The API for this component is specified [here](https://web.stanford.edu/class/cs346/2015/redbase-ix.html). There can be multiple different ways to implement the same functionality, and all of them are equally valid.

4. **System Management** - The SM compoment provides the following functions:
  - __Unix command line utilities__ - for creating and destroying RedBase databases, invoking the system
  - __Data definition language (DDL) commands__ - for creating and dropping relations, creating and dropping indexes
  - __System utilities__ - for bulk loading, help, printing relations, setting parameters
  - __Metadata management__ - for maintaining system catalogs
  Details can be found [here](https://web.stanford.edu/class/cs346/2015/redbase-sm.html).
  
5. **Query Language** - The QL component implements the language RQL (for "RedBase Query Language"). RQL's data retrieval command is a restricted version of the SQL Select statement. RQL's data modification commands are restricted versions of SQL's Insert, Delete, and Update statements. The QL component uses classes and methods from the IX, RM, and SM components. More details can be found [here](https://web.stanford.edu/class/cs346/2015/redbase-ql.html).


### Changes Done to Redbase

Just a Abstract summary of what I have done in these files. Please refer code for further understanding.


- redbase.h : added a MBR enum, and Mbr structure which contains 4 points, x_min, y_min, x_max, y_max

- interp.cc : Added handling of 'm' as Mbr data structre from query when given to create table.
			: data insertion handling.

- parse.y 	: Handling of MBR added to the file.
			: Addtion and handling on T_MBR data type from the input.

- sm_manager.cc : Valid Attribute type handling and Printing the values.
				: Correct offsetting for MBR data strcuture.

- parser_internal.h : Mbr data type addition.

- y.tab.h 	: For Mbr data type compilation issues.

- nodes.cc	: MBR data structure handling

- printer.cc : Printer issues for MBR data type.

- sacn.l :  Taking MBR Input addition in the insertion command.

- comparators.h : Comparision mechanism for MBR data type.

- ix.h : All the definitions of data types and functions in IndexHeader, IndexHandler, IndexManager and IndexScanner class.

- ix_indexhandle.cc, ix_internal.h , ix_manager.cc, ix_indexscan.cc : Insert Node, Delete Node, Search, Split and multiple other funtions
																		handling for the R-tree.

- rm_filescan.cc : File scan modification for MBR data type.

- ql_noderel.cc : Enable index related functionalities by setting 'useIndex' parameter as true.


### Steps for running

**Install the dependencies**

```
sudo apt-get install flex bison g++ g++-multilib git cmake make 
```


**Clone repository**

```
git clone git@github.com:asriv003/CS236_Redbase.git
```


**Build the code**

```
cd redbase
mkdir build
cd build
cmake ..
make
```


**Test**

```
./dbcreate Test
./redbase Test
```

**DDL Commands**

```
create table data(name c20, id i);
drop table data;
```

**DML Commands**

```
insert into data values ("abc", 1);
select * from data;
```

### Some other Reqiurements
1. We suggest the use of following open source projects to help you with the assignment:
- CMake: To generate build configurations.
- Ninja: Build tool written by the Chromium project, faster than GNU Make.
- Valgrind: For detecting memory leaks.
- GNU Flex and Bison: Used to implement the Query Language component.
- CTags: Great for navigating code.
- GTest: The Google C++ testing framework

### Special Thanks
Yifei Huang (yifeih@cs.stanford.edu)

###Valgrind Use
Please ensure you have packages with debug symbols installed. You can do this by following the instructions at DebuggingProgramCrash.

1.Make sure Valgrind is installed.

`sudo apt-get install valgrind`

2.Remove any old Valgrind logs:

`rm valgrind.log*`

3.Start the program under control of memcheck:


`G_SLICE=always-malloc G_DEBUG=gc-friendly  valgrind -v --tool=memcheck --leak-check=full --num-callers=40 --log-file=valgrind.log $(which <program>) <arguments>`

- N.B. valgrind can't solve paths, so you should feed it the full program path, to get it: `$(which <program>)`

- The program will start. It may take a while; this is normal, because Valgrind must perform extensive checking to detect memory errors.

- Perform any actions necessary to reproduce the crash.

- Package up the log files (no need if there is only one):

`tar -zcf valgrind-logs-<program>.tar.gz valgrind.log*`

-Attach the complete output from Valgrind, contained in `valgrind-logs-<program>.tar.gz`, in your bug report.