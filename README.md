<p align="center">
  <img height="100" src="https://github.com/p-ranav/hypergrep/assets/8450091/18aeff91-0c0b-4e1c-b7b9-628e377798e2"/>
</p>

## Highlights

* Search recursively for a regex pattern using [Intel Hyperscan](https://github.com/intel/hyperscan)
* When a git repository is detected, the repository index is searched using [libgit2](https://github.com/libgit2/libgit2)
* Similar to `grep`, `ripgrep`, `ugrep`, `The Silver Searcher` etc.
* C++17, Multi-threading, SIMD

## Performance

The following tests compare the performance of `hypergrep` against:

* [ripgrep](https://github.com/BurntSushi/ripgrep/) `v11.0.2`
* [ag 2.2.0 (The Silver Searcher)](https://github.com/ggreer/the_silver_searcher) `v2.2.0`
* [ugrep](https://github.com/Genivia/ugrep) `v3.11.2`

### System Details

| Type            | Value |
|:--------------- |:---- |
| Processor       | [11th Gen Intel(R) Core(TM) i9-11900KF @ 3.50GHz   3.50 GHz](https://ark.intel.com/content/www/us/en/ark/products/212321/intel-core-i911900kf-processor-16m-cache-up-to-5-30-ghz.html) |
| Instruction Set Extensions | Intel® SSE4.1, Intel® SSE4.2, Intel® AVX2, Intel® AVX-512 |
| Installed RAM   | 32.0 GB (31.9 GB usable) |
| SSD             | [ADATA SX8200PNP](https://www.adata.com/upload/downloadfile/Datasheet_XPG%20SX8200%20Pro_EN_20181017.pdf) |
| OS              | Ubuntu 20.04 LTS |
| C++ Compiler    | g++ (Ubuntu 11.1.0-1ubuntu1-20.04) 11.1.0 |

### Vcpkg Installed Libraries

[vcpkg](https://github.com/microsoft/vcpkg) commit: [6a3dd](https://github.com/microsoft/vcpkg/commit/6a3dd0874f153f8b375ec26210ea6d41dee3bb26)

| Library | Version | 
|:---|:---|
| **argparse** | 2.9 |
| **concurrentqueue** | 1.0.3#1 |
| **fmt** | 9.1.0#1 |
| **hyperscan** | 5.4.2 |
| **libgit2** | 1.4.2 |

### Directory Search: `torvalds/linux`

The following searches are performed on the entire [Linux kernel source tree](https://github.com/torvalds/linux) (after running `make defconfig && make -j8`). The commit used is [f1fcb](https://github.com/torvalds/linux/commit/f1fcbaa18b28dec10281551dfe6ed3a3ed80e3d6).

| Regex | Line Count | ag | ugrep | ripgrep | hypergrep |
| :---| ---:| ---:| ---:| ---:| ---:|
| Simple Literal<br/>`hg -nw 'PM_RESUME'` | 9 | 2.807 | 0.316 | 0.198 | **0.145** |
| Simple Literal (case insensitive)<br/>`hg -niw 'PM_RESUME'` | 39 | 2.904 | 0.435 | 0.203 | **0.145** |
| Regex with Literal Suffix<br/>`hg -nw '[A-Z]+_SUSPEND'` | 538 | 3.080 | 1.452 | 0.198 | **0.147** |
| Alternation of four literals<br/>`hg -nw '(ERR_SYS\|PME_TURN_OFF\|LINK_REQ_RST\|CFG_BME_EVT)'` | 16 | 3.085 | 0.410 | 0.407 | **0.148** |
| Unicode Greek<br/>`hg -n '\p{Greek}'` | 111 | 3.762 | 0.484 | 0.386 | **0.147** |

### Directory Search: `apple/swift`

The following searches are performed on the entire [Apple Swift source tree](https://github.com/apple/swift). The commit used is [3865b](https://github.com/apple/swift/commit/3865b5de6f2f56043e21895f65bd0d873e004ed9).

| Regex | Line Count | ag | ugrep | ripgrep | hypergrep |
| :---| ---:| ---:| ---:| ---:| ---:|
| Function/Struct/Enum declaration followed by a valid identifier and opening parenthesis<br/>`hg -n '(func\|struct\|enum)\s+[A-Za-z_][A-Za-z0-9_]*\s*\('` | 59069 | 1.148 | 0.954 | 0.184 | **0.094** |
| Words starting with alphabetic characters followed by at least 2 digits<br/>`hg -nw '[A-Za-z]+\d{2,}'` | 127855 | 1.169 | 1.238 | 0.186 | **0.145** |
| Workd starting with Uppercase letter, followed by alpha-numeric chars and/or underscores <br/>`hg -nw '[A-Z][a-zA-Z0-9_]*'` | 2012265 | 3.131 | 2.598 | 0.673 | **0.563** |
| Guard let statement followed by valid identifier<br/>`hg -n 'guard\s+let\s+[a-zA-Z_][a-zA-Z0-9_]*\s*=\s*\w+'` | 857 | 0.828 | 0.174 | 0.072 | **0.048** |

### Single Large File Search: `OpenSubtitles.raw.en.txt`

 The following searches are performed on a single large file cached in memory (~13GB, [`OpenSubtitles.raw.en.gz`](http://opus.nlpl.eu/download.php?f=OpenSubtitles/v2018/mono/OpenSubtitles.raw.en.gz)).

NOTE: `ag` (The Silver Searcher) can't handle files larger than 2147483647 bytes.

| Regex | Line Count | ugrep | ripgrep | hypergrep |
| :---| ---:| ---:| ---:| ---:|
| Literal with Regex Suffix<br/>`hg -nw 'Sherlock [A-Z]\w+' en.txt` | 7882 | 1.812 | 2.654 | **0.876** |
| Simple Literal<br/>`hg -nw 'Sherlock Holmes' en.txt` | 7653 | 1.888 | 1.813 | **0.697** |
| Simple Literal (case insensitive)<br/>`hg -inw 'Sherlock Holmes' en.txt` | 7871 | 6.945 | 2.139 | **0.709** |
| Alternation of Literals<br/>`hg -n 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty' en.txt` | 10078 | 6.886 | 1.808 | **0.732** |
| Alternation of Literals (case insensitive)<br/>`hg -in 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty' en.txt` | 10333 | 7.029 | 3.880 | **0.808** |
| Words surrounding a literal string<br/>`hg -n '\w+[\x20]+Holmes[\x20]+\w+' en.txt` | 5020 | too long | 1.812 | **0.679** |

## How It Works

`hypergrep` can search (1) a directory of files, (2) a git repository, or (3) a single file. The approach taken by `hypergrep` differs in small ways depending on what is being searched. 

For any directory search, the approach is: Find files that needs to be searched. Enqueue these files onto a lock-free queue. Spawn N threads that read from the queue and scan for matches. Continue until all relevant files are searched.  

![Workflow](doc/workflow.png)

### Git Repository Search

When a `.git` directory is detected in any folder, `hypergrep` tries to use [libgit2](https://libgit2.org/libgit2/#HEAD) to [open](https://libgit2.org/libgit2/#HEAD/group/repository/git_repository_open) the git repository. If the git repository is successfully opened, the git index file is loaded using [git_repository_index](https://libgit2.org/libgit2/#HEAD/group/repository/git_repository_index) and then [iterated](https://libgit2.org/libgit2/#HEAD/group/index/git_index_iterator_next). Candidate files are enqueued onto the queue and then subsequently searched in one of the search threads. **NOTE** that when working with git repositories, `hypergrep` chooses to search the index instead of evaluating each file against every `ignore` rule in every `.gitignore` file. Performing the gitignore check for every path is wasteful and generally much slower than iterating over the index. When dealing with git repositories, additionally, `hypergrep` searches each submodule in the active repository using [git_submodule_foreach](https://libgit2.org/libgit2/#HEAD/group/submodule/git_submodule_foreach). **NOTE** Submodules can be excluded using the  `--exclude-submodules` flag, which will further speed up any repository search.

### Directory Search

When searching any directory that is not in itself a git repository, `hypergrep` might still encounter a nested directory that happens to be a git repository. So, for any directory search, `hypergrep` first uses [std::filesystem::recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator) to iterate over all the files and directories. If git repositories are encountered, instead of iterating further into those directories, it reads and iterates the git index. Any further recursion into those directories is arrested. So when searching a top-level `~/projects` directory, `hypergrep` will essentially be searching every file in the git index of every git repository. For directories that are not git repositories, any candidate file that is discovered through recursive directory iteration is enqueued. 

### Large File Search

When searching single files, `hypergrep` will first memory map the file, then search it in parallel across multiple threads. Each thread will count the number of lines in its chunk and then enqueue that number onto a queue that it shares with its immediate successor thread. So each thread will first dequeue the line number that was enqueued by its predecessor and then continue the count from there. Since the file is memory-mapped, each thread is not confined to the boundaries of the chunk with which it is working. Instead, each thread keeps track of a `char* start` and a `char* end`, which are first initialized to the chunk boundaries. Before a search is performed, both pointers are decremented until the first newline character. The search is performed on this region of memory. Every thread follows this rule. **NOTE** that the first thread searching the first chunk does not need to decrement the `start` pointer. 

NOTE when performing directory search, large files are added to a backlog and then searched using the memory mapped method described above. This way, `hypergrep` moves from using `N` threads to search `N` files (dequeued from the queue) for small files, to using `N` threads to search just one file (for each large file). Using this approach dramatically speeds up the overall search process, especially when a 15GB file is suddenly encountered . In such cases, instead of using a single thread (using a `open(file) -> read_to(fixed_buffer) -> search` loop), maximum concurrency is utilized using memory mapping. 

## Build

### Install Dependencies with `vcpkg`

```bash
git clone https://github.com/microsoft/vcpkg
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg install concurrentqueue fmt argparse libgit2 hyperscan
```

### Build `hypergrep` using CMake

```
git clone https://github.com/p-ranav/hypergrep
cd hypergrep
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake ..
make
```
