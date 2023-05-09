
## Performance

### File Search

 The following searches are performed on a single large file cached in memory (~13GB, [`OpenSubtitles.raw.en.gz`](http://opus.nlpl.eu/download.php?f=OpenSubtitles/v2018/mono/OpenSubtitles.raw.en.gz)).

| Input | Command | Line Count | ripgrep | hypergrep |
| --- | :---| ---:| ---:| ---:|
| Literal with Regex Suffix | `hg -w 'Sherlock [A-Z]\w+'` | 7882 | 2.514 | **0.736** |
| Literal with Regex Suffix (with Line Number) | `hg -nw 'Sherlock [A-Z]\w+'` | 7882 | 3.272 | **2.168** |
| Simple Literal | `hg -nw 'Sherlock Holmes'` | 7653 | 2.411 | **2.179** |
| Simple Literal (Case Insensitive) | `hg -inw 'Sherlock Holmes'` | 7871 | 2.773 | **2.167** |
| Alternation of Literals | `hg -n 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty'` | 10078 | 2.542 | **2.169** |
| Alternation of Literals (Case insensitive) | `hg -in 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty'` | 10333 | 4.642 | **2.182** | 

### Directory Search

The following searches are performed on the entire [Linux kernel source tree](https://github.com/torvalds/linux) (after running `make defconfig && make -j8`).

| Input | Command | Line Count | ripgrep | hypergrep |
| --- |:---| ---:| ---:| ---:|
| Simple Literal | `hg -w 'PM_RESUME'` | 9 | 0.209 | 0.213 |
| Simple Literal (Case insensitive) | `hg -niw 'PM_RESUME'` | 39 | 0.209 | 0.212 |
| Regex with Literal Suffix | `hg -nw '[A-Z]+_SUSPEND'` | 538 | 0.216 | 0.212 |
| Unicode Greek | `hg -n '\p{Greek}'` | 105 | 0.397 | **0.215** |

## Workflow

![Workflow](doc/workflow.png)

## Build

```
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake ..
make
```
