# Run tests

Use `mulle-sde test` to run the tests. Each test is a file with the `.c`
extension, compiled and linked with **mulle-mmapallocator**.

The test file is named by its path from the project root, so the `test/`
prefix is part of the argument:

```bash
mulle-sde test --platform linux run test/30-shared/grow.c   # one test
mulle-sde test --platform linux run                         # all of them
mulle-sde test --platform linux rerun                       # only the failures
```

`--platform` (and `--configuration`, `--valgrind`, ...) must come *before* the
`run` subcommand. Without it the platform list comes from the environment; here
`MULLE_CRAFT_PLATFORMS` is `${MULLE_UNAME}:windows`, so a plain
`mulle-sde test run ...` cross-builds for Windows and runs the result under
wine. Pass `--platform linux` for a native build.

Extension   | Description
------------|-------------------------
`.c` | Test source
`.stdin`    | Command standard input
`.stdout`   | Expected command standard output


There are quite a few more options to tweak each test. 
See [mulle-test](//github.com/mulle-sde/mulle-test) for more info.


<!--
extension : mulle-sde/c-test-library-demo
directory : demo/all
template  : .../README.md
Suppress this comment with `export MULLE_SDE_GENERATE_FILE_COMMENTS=NO`
-->
