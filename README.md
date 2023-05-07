
## Performance

### File Search

| Input | Line Count | ripgrep | hypergrep |
| --- | ---:| ---:| ---:|
| Single large file cached in memory (~13GB, [`OpenSubtitles.raw.en.gz`](http://opus.nlpl.eu/download.php?f=OpenSubtitles/v2018/mono/OpenSubtitles.raw.en.gz))<br/>`hg -w 'Sherlock [A-Z]\w+' en.txt` | 7882 | 2.520 | **0.752** |

### Directory Search

| Input | Line Count | ripgrep | hypergrep |
| --- | ---:| ---:| ---:|
| Simple Literal<br/>`hg -w 'PM_RESUME'` | 9 | 0.215 | **0.184** |
| Simple Literal (Case insensitive)<br/>`hg -niw 'PM_RESUME'` | 9 | 0.221 | 0.221 |
| Regex with Literal Suffix<br/>`hg -w '[A-Z]+_SUSPEND'` | 538 | 0.219 | **0.184** |
| Unicode Greek<br/>`hg -n '\p{Greek}'` | 105 | 0.394 | **0.210**|

## Build

```
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake ..
make
```

## Workflow

![Workflow](doc/workflow.png)
