
## Performance

### File Search

 Single large file cached in memory (~13GB, [`OpenSubtitles.raw.en.gz`](http://opus.nlpl.eu/download.php?f=OpenSubtitles/v2018/mono/OpenSubtitles.raw.en.gz))

| Input | Command | Line Count | ripgrep | hypergrep |
| --- | :---| ---:| ---:| ---:|
| Literal with Regex Suffix | hg -nw 'Sherlock [A-Z]\w+' | 7882 | 3.272 | **2.202** |
| Simple Literal | `hg -nw 'Sherlock Holmes'` | 7653 | 2.411 | 2.204 |
| Simple Literal (Case Insensitive) | `hg -inw 'Sherlock Holmes'` | 7871 | 2.773 | **2.214** |
| Alternation of Literals | `hg -n 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty'` | 10078 | 2.542 | **2.247** |
| Alternation of Literals (Case insensitive) | `hg -in 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty'` | 10333 | 4.642 | **2.253** | 

### Directory Search

| Input | Command | Line Count | ripgrep | hypergrep |
| --- |:---| ---:| ---:| ---:|
| Simple Literal | `hg -w 'PM_RESUME'` | 9 | 0.215 | **0.184** |
| Simple Literal (Case insensitive) | `hg -niw 'PM_RESUME'` | 9 | 0.221 | 0.221 |
| Regex with Literal Suffix | `hg -w '[A-Z]+_SUSPEND'` | 538 | 0.219 | **0.184** |
| Unicode Greek | `hg -n '\p{Greek}'` | 105 | 0.394 | **0.210** |

## Build

```
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake ..
make
```

## Workflow

![Workflow](doc/workflow.png)
