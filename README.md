
## Performance

### File Search

 Single large file cached in memory (~13GB, [`OpenSubtitles.raw.en.gz`](http://opus.nlpl.eu/download.php?f=OpenSubtitles/v2018/mono/OpenSubtitles.raw.en.gz))

| Input | Command | Line Count | ripgrep | hypergrep |
| --- | :---| ---:| ---:| ---:|
| Literal with Regex Suffix | `hg -w 'Sherlock [A-Z]\w+'` | 7882 | 2.520 | **0.752** |
| Simple Literal | `hg -w 'Sherlock Holmes'` | 7673 | 2.397 | 2.204 |
| Simple Literal (Case Insensitive) | `hg -iw 'Sherlock Holmes'` | 7892 | 2.773 | **2.209** |
| Alternation of Literals | `hg -n 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty'` | 10078 | 2.446 | **2.210** |
| Alternation of Literals (Case insensitive) | `hg -in 'Sherlock Holmes\|John Watson\|Irene Adler\|Inspector Lestrade\|Professor Moriarty'` | 10333 | 4.582 | **2.227** |
| subtitles_surrounding_words crash | | | | |

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
