sgx-perf
========

Paper
-----

The paper will be presented at Middleware 2018 and is available here: https://www.ibr.cs.tu-bs.de/users/weichbr/papers/middleware2018.pdf and https://dl.acm.org/citation.cfm?id=3274824

Please cite us if you use our work in your research:
```
@inproceedings{weichbrodt2018middleware,
 author = {Weichbrodt, Nico and Aublin, Pierre-Louis and Kapitza, R\"{u}diger},
 title = {{sgx-perf: A Performance Analysis Tool for Intel SGX Enclaves}},
 booktitle = {Proceedings of the 19th International Middleware Conference},
 series = {Middleware '18},
 year = {2018},
 doi = {10.1145/3274808.3274824},
}
```

Prerequisites
--------------

sgx-perf was tested with the official Intel SGX SDK version 1.9 and 2.3. It should also works with other versions.
It requires the non-stripped PSW libraries (if you install the prebuild packages, you're fine).
If you have built the SDK/PSW from source, you need to disable stripping of the binary.
sgx-perf needs to access some PSW internal functions that are not exported by the URTS to find enclave start and end addresses.
Furthermore, you need `libelf`. 

Working set analysis of big enclaves (>60000 pages) requires a high value of `vm.max_map_count` as the working set analyser cuts up the enclave mappings into one mapping per enclave page.
Set it with `sysctl -w vm.max_map_count=2147483647` to `INT_MAX`.

Warning: It is possible, to set it to zero. Setting it to 4294967296/`UINT_MAX` is interpreted as zero.
If set to zero, you cannot launch any new processes as the kernel cannot map anything because its already at the limit.
Rebooting is the only help in this case.


Limitations
-----------

- The logger and working set analyzer only intercept `sgx_create_enclave` and NOT `sgx_create_enclave_ex`.
- SGXv2 features are not supported.
- The analyzer does not look at paging events, yet.
- The analyzer does not look at EDL imports, when given an EDL.
- Logger supports multiple enclaves in the same applications, but analyzer not really
- Logger might not be thread-safe

How to build
------------

    $ mkdir build
    $ cd build
    $ cmake ..
    $ make

This creates `lib/liblogger.so` for hardware mode and `lib/libloggersim.so` for simulation mode.
Also `lib/libenclws.so`/`lib/libenclswssim.so` for working set analysis.
And `bin/analyzer` for analysis.

How to execute
--------------

Both the logger and the working set analyser have to be preloaded to your application.
They do not require any changes to the application.
Example:

    $ LD_PRELOAD=path/to/lib/liblogger.so ./app

The logger will producer a `out-<pid>.db` file in the working directory. It is a sqlite3 database.
The working set analyser will print the working set of all started enclaves upon termination.
Furthermore, you can send a `SIGUSR1` to an application currently analysed by the working set analyser
to print the current counters and reset them.
This allows you to pick a certain window for analysis, e.g., to exclude a warm-up time.

.sgxperf
---------

The logger tries to load the config file `.sgxperf` in the current working directory.
The config file uses INI style.

Example:

    CountAEX=true
    TracePaging=true

Valid keys are:

    CountAEX
    TraceAEX
    TracePaging
    Benchmode

`CountAEX` counts AEXs during execution, `TraceAEX` also traces them (records timestamps). Trace implies count.
`TracePaging` traces paging events, this requires root and support for kprobes.
`Benchmode` actives benchmark mode, in this mode no result file is generated.

How to analyze
--------------

    $ ./analyzer /path/to/out-<pid>.db

Analysis takes some time depending on the size of the database.
A ~1GB database with ~6.6 million events in one thread takes about 1 minute on my laptop (i7-5600U).

The analyzer has some options that are be printed when given no arguments. Some common uses are:

Only print calls that have been called more than once:

    ./analyzer -e 2 -o 2 /path/to/out-<pid>.db

Create a graph with call dependencies (requires graphviz):

    ./analyzer -f /path/to/output.dot /path/to/out-<pid>.db
    dot -Tpdf /path/to/output.dot > graph.pdf

Create histogram/scatter plot from call data (requires gnuplot):

    ./analyzer -d folder-name-data /path/to/out-<pid>.db
    scripts/plot-all.bash folder-name

The `plot-all.bash` script will look for a folder called `folder-name-data` and create a `folder-name-plots`

Print narrowest ocall EDL signatures based on the gathered data:

    ./analyzer -i /path/to/out-<pid>.db


Integrate working set analyser in non-SDK applications
------------------------------------------------------

The workingset analyser can be integrated into non-SDK applications.
For example, for a dynamic integration do something like this:

    char* error = dlerror();
    // ws_init is exposed by the working set analyser
    int (*ws_init)(int eid, char* start, size_t size) = dlsym(RTLD_DEFAULT,
    "ws_init");
    if (error = dlerror()) {
        // No workingset analyser found
        fprintf(stderr, "Could not find ws_init(). (%s)\n", error);
    } else {
        // ws_init needs an enclave id, start of the enclave and its size
        ws_init(0, enclave_base_addr, enclave_size_in_bytes);
    }


