# cgcs_malloc
 An implementation of malloc - a memory allocator function, written in C.

## Building:

Run the included `makebuilds` script to have `cmake`<br>
create Unix-Makefile builds in the following modes:
- `Debug`
- `Release`
- `MinSizeRel`
- `RelWithDebInfo`

```
% ./makebuilds
```

A `build/make` subdirectory will be created with subdirectories<br>
of the modes described above. 

If we want to create a `Debug` build
of our demo program (which will also build the `cgcs_malloc` library):

```
make -C ./build/make/Debug/demo
```

Generally,
```
make -C ./build/make/[build-mode]/[target-name]
```

If you want to use an alternative build system, i.e. Xcode or Visual Studio<br>
(see the list of supported generators on your system using `cmake -help`), invoke the following:
```
% cmake -S ./ -B ./build/[generator-name] -G "[generator-name]"
```

For example, for Xcode:
```
% cmake -S ./ -B ./build/xcode -G "Xcode"
```
