`hypergrep` is a line-oriented search tool that will recursively search paths for regex pattern matches. Using the input pattern(s), an Intel Hyperscan pattern database is compiled for the search. If manual filtering is used, a secondary pattern database is compiled for filtering out unwanted paths. Once ready, each input path is searched in a multi-threaded SIMD-vectorized manner. 

## Workflow

`hypergrep` is designed to handle three specific use-cases:

1. A directory of files
2. A git repository, which may have submodules
3. A single file.

Below is a diagram that illustrates the workflow used by `hypergrep`.

![Workflow](workflow.png)

### Git Repository Search

When a `.git` directory is detected in any folder, `hypergrep` tries to use [libgit2](https://libgit2.org/libgit2/#HEAD) to [open](https://libgit2.org/libgit2/#HEAD/group/repository/git_repository_open) the git repository. If the git repository is successfully opened, the git index file is loaded using [git_repository_index](https://libgit2.org/libgit2/#HEAD/group/repository/git_repository_index) and then [iterated](https://libgit2.org/libgit2/#HEAD/group/index/git_index_iterator_next). Candidate files are enqueued onto the queue and then subsequently searched in one of the search threads. **NOTE** that when working with git repositories, `hypergrep` chooses to search the index instead of evaluating each file against every `ignore` rule in every `.gitignore` file. Additionally, `hypergrep` searches each submodule in the active repository using [git_submodule_foreach](https://libgit2.org/libgit2/#HEAD/group/submodule/git_submodule_foreach). **NOTE** Submodules can be excluded using the  `--ignore-submodules` flag, which will further speed up any repository search.

### Directory Search

When searching any directory that is not in itself a git repository, `hypergrep` might still encounter a nested directory that happens to be a git repository. So, for any directory search, `hypergrep` traverses the directory tree in a multi-threaded fashion, enqueuing any subdirectories into a queue for further processing. If git repositories are encountered, instead of iterating further into those directories, `hypergrep` reads and iterates the git index. Any further recursion into those directories is arrested. For directories that are not git repositories, any candidate file that is discovered through recursive directory iteration is enqueued and searched.

### Large File Search

When searching single files, `hypergrep` will first memory map the file, then search it in parallel across multiple threads. Each thread covers a portion of the file and saves its local results to a thread-specific queue. A consumer thread at the end of the pipeline is responsible for dequeueing from each thread-specific queue, figuring out the line numbers, and printing each result correctly. Note that during directory search, `hypergrep` handles any large files encountered during iteration using this approach.

## Design Decisions

1. Files are searched in chunks of `64 KiB` (`65536 bytes`). If such a chunk does not have any newlines, skip this file. Example: minified JS file.
2. A file is marked as a "large" file if its size exceeds `1 MiB` (`1048576 bytes`). Large files, as described above, will be memory mapped and searched in a multi-threaded fashion. 
3. Lines longer than `4096 bytes` are omitted - the number of matches in such lines is still printed. This cleans up the output and simplifies the implementation as well.
4. If `-w/--word-regexp` is used, any `-F` argument will be discarded and the pattern will not be treated as a literal anymore (because of the `\b`pattern`\b` bookends that are added to support `-w`)
5. Trimming whitespace using `--trim` will trim any `' '` (`0x20` whitespace) and any `'\t'` (`0x09` tab character) at the start of any matching line.
