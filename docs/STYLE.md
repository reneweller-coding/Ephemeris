# Code style

The C++ of Ephemeris follows the programming guideline of the Computer Graphics and Virtual Reality group of the
Universität Bremen (CGVR, <https://cgvr.informatik.uni-bremen.de/progr/guidelines.shtml>): its layout, its names and
its comment template. Where the code does something else, this page says what and why. The six repositories of the
family (Totality, Ephemeris, Parhelion, Phosphene, Noctuary, Empyrean) share these rules; `cmake/Family.cmake` and
`.clang-format` are the same in all of them.

## Layout

`.clang-format` at the root lays the code out; run it on what you change:

    clang-format -i --style=file <files>

- Braces on lines of their own, under each other (no K&R).
- Spaces inside parentheses: `if ( x > 0 )`, `f( a, b )`; a space either side of a binary operator.
- The pointer and reference sign at the name: `float *out`, `const Score &score`.
- Four spaces a level, no tabs.
- Declarations in a row and their trailing `///<` comments in columns.
- Comments are never reflowed and string literals never split (scripts in `Tools` read them), and the includes
  keep the order they were written in.

The commit that brought the code into this layout is listed in `.git-blame-ignore-revs`; `git blame` passes over it
after

    git config blame.ignoreRevsFile .git-blame-ignore-revs

## Names

| What | How | Example |
|---|---|---|
| classes, structs | upper camel case | `ParamStore` |
| functions, methods | lower camel case | `noteOn()` |
| data members | `m_` | `m_sampleRate` |
| static data members | `M_` | (none so far) |
| constants | upper camel case, no `k` | `MaxSteps` |
| enum types | upper camel case and `E` | `SyncDivE` |
| enumerators | capitals | `SyncDivE::COUNT` |
| template type parameters, type aliases | `T` at the end | `LadderT` |
| macros | capitals, the prefix `EPH_` (shared code `FAMILY_`) | `EPH_FORCE_SCALAR` |
| namespace | short, small | `eph` |

## Documentation

The comment template of the guideline, written for Doxygen:

- Every file starts with a `/** @file ... */` block: `@brief`, what the file is for, `@author`, and where the file
  asks a switch of the build, `@par Compile Flags` with one `@arg @c NAME - what it does` per switch.
- Every function and function macro has a `/** ... */` block: `@brief`, one `@param[in]`, `@param[out]` or
  `@param[in,out]` per parameter in the order of the signature, `@return` for anything but `void`, `@pre` where the
  function asks something of its caller (a thread, a call before it). There is no `@throw`: the code throws no
  exceptions.
- Members, enumerators and constants have a `///<` comment behind them.

The test `doccheck` (ctest, `cmake/Family.cmake`) runs Doxygen over Core, Plugin, the Quest app (where there is
one), Tools and Tests with every warning switched on and fails on any gap: something undocumented, a parameter or a
return value not described, a documentation error, an `@param` without its direction.

## Where the code does something else

- **120 columns, not 80.** The guideline allows exceptions to its 80; the long names and the parameter lists of an
  audio engine would otherwise break nearly every call.
- **Enum types start with a capital** (`SyncDivE`, the guideline writes `myEnumTypeE`): they are types like the
  classes.
- **Local variables in lower camel case** (`sampleRate`, the guideline writes `mylocalvar` or `my_local_var`): they
  read like the parameters and members they come from. They were left as they were.
- **File names.** Template code is in the headers (`.h`), generated tables in `.inc` and `.inl` files, not `.hh`.
- **Generated files** (`.clang-format-ignore`) keep the layout of the scripts in `Tools` that write them; change the
  script and run it again rather than the file.
- **Names the outside world knows stay as they are**: parameter keys (`filter.cutoff`), the keys of the saved state,
  set files, OSC addresses, the strings of the user interface. Renaming one would break saved sets, a host's
  automation and the family jam.
- **The guideline's language rules are not taken over:**
  - copy constructor and assignment operator are not declared everywhere, and `operator=` does not return `void`: the
    classes follow the rule of zero, and a class that must not be copied deletes the two;
  - not every class has a virtual destructor: only the polymorphic ones do (a vtable has no place in a voice);
  - a parameter may be a non-const reference: the documentation says `[out]` or `[in,out]`, and JUCE's overrides
    (`paint( juce::Graphics &g )`) fix their signatures;
  - overrides are marked `override`, not `virtual` again;
  - small inline functions, also inline constructors and overrides, stay in the class;
  - constructors may do work: JUCE's components and processors build themselves in their constructors, as the
    framework expects;
  - exceptions: none are thrown. The audio thread must not throw or allocate, so failures are return values (`bool`,
    an empty result, an error string).
