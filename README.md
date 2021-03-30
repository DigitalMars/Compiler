# Source Code to the Digital Mars C and C++ compilers.

http://www.digitalmars.com

## Running the compiler:

https://digitalmars.com/ctg/sc.html

## Building the compiler:

1. Download and install Digital Mars C++ compiler from https://www.digitalmars.com/download/freecompiler.html
2. Download and install DMD 2.074.1 from http://downloads.dlang.org/releases/2017/
3. In the DMD installation, edit the file `src\druntime\importcore\stdc\stdio.d` and replace
   `shared stdout = &_iob[1];`
   with
   `enum stdout = &_iob[1];`.
4. Run `git submodule update --init` to make sure you have up-to-date submodules.
5. Change directory to `dm\src\dmc`
6. Make sure the `dm\bin\make.exe` program is on your `PATH`.
7. Execute the commands:
   `make clean`
   `make scppn`
You might need to edit the `makefile` to set the path to your DMD installation.

# Updating the backend

In order to update the backend to a more recent version, do the following:
1. Go to the DMD submodule:  `cd dm/src/dmd`;
2. Fetch the latest commits: `git fetch`
3. Checkout the desired commit, e.g.:
   - `git checkout origin/master`: Checkout the latest version of the DMD backend;
   - `git checkout v2.095.0`: Checkout the state of the backend as of DMD v2.095.0;
   - `git checkout 385312b93`: Checkout the state of the backend as of commit `385312b93`;
4. Leave the `dmd` repository, e.g. `cd ../../` to come back to the root of this repository;
5. Commit the change to the submodule: `git add dm/src/dmd && git commit -m "Update DMD backend to master"`

Alternatively, when pulling from this repository, remember to always run `git submodule update --init`
if the submodule have been updated. This is visible from `git status`:
```shell
$ git diff
diff --git a/dm/src/dmd b/dm/src/dmd
index 123456789..abcdef123 160000
--- a/dm/src/dmd
+++ b/dm/src/dmd
@@ -1 +1 @@
-Subproject commit 67ca0a14c4d4d3161541eda27a4126f889d53546
+Subproject commit 385312b93239311c038ffd8c221ac62738006382
```

To learn more about `git submodule`, see [this section of the Pro Git book](https://git-scm.com/book/en/v2/Git-Tools-Submodules).
