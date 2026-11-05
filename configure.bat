@echo off
rem ############################################################################
rem #
rem # Configure script for the FTA-850 programmer (Windows)
rem #
rem # Puts on the machine what the build and the program need:
rem #
rem #   1. The build tools: MinGW gcc, make, git, curl and the Unix tools the
rem #      amitk build uses, from MSYS2 (https://www.msys2.org).
rem #   2. The development packages the AMI tool kit links against on Windows:
rem #      OpenSSL, libpng and zlib, from MSYS2's pacman.
rem #   3. The AMI tool kit itself (amitk), fetched from GitHub into AMIDIR
rem #      (default USERPROFILE\projects\amitk, the Makefile's default too)
rem #      and built, if it is not there already.
rem #   4. Access to the radio's serial port: nothing to install on Windows,
rem #      the radio is a COM port on the usbser driver that comes with
rem #      Windows 10. The COM ports present are listed.
rem #
rem # Each of these is installed when missing, or, when it cannot be installed
rem # from here, reported with what to do, and the summary at the end says
rem # whether the program will build and run.
rem #
rem # Options:
rem #
rem # --prompt  Ask yes or no before each install.
rem # --check   Report what is missing and install nothing.
rem # --help    Print this.
rem #
rem # Packages come from MSYS2's pacman, found on the path, in MSYS2_ROOT or
rem # in C:\msys64. Without MSYS2 the missing packages are named for
rem # installing by hand.
rem #
rem ############################################################################

setlocal EnableDelayedExpansion

set ask=0
set check=0
for %%x in (%*) do (

    if "%%x" == "--prompt" (

        set ask=1

    ) else if "%%x" == "--check" (

        set check=1

    ) else if "%%x" == "--help" (

        echo Configure script for the FTA-850 programmer
        echo.
        echo configure [--prompt] [--check]
        echo.
        echo Installs what the build and the program need: the build tools, the
        echo development packages, the AMI tool kit, amitk, fetched and built in
        echo AMIDIR, default USERPROFILE\projects\amitk, all from MSYS2. --prompt asks
        echo yes or no before each install. --check reports and installs nothing.
        exit /b 0

    ) else (

        echo *** Option not recognized: %%x
        echo Terminating
        exit /b 1

    )

)

rem
rem The problems that leave the program unable to build or run are collected
rem in a file and printed again in the summary.
rem
set warnfile=%TEMP%\fta850_configure_warnings.txt
if exist "%warnfile%" del "%warnfile%"
set warned=0

rem ############################################################################
rem #
rem # Find MSYS2: pacman on the path, else in MSYS2_ROOT, else in C:\msys64.
rem # The toolchain's packages are named for the shell it belongs to, which
rem # the path of gcc tells: mingw64, ucrt64, clang64 or mingw32.
rem #
set pacman=
for /f "delims=" %%p in ('where pacman 2^>nul') do if not defined pacman set pacman=%%p
if not defined pacman if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\pacman.exe" set pacman=%MSYS2_ROOT%\usr\bin\pacman.exe
if not defined pacman if exist "C:\msys64\usr\bin\pacman.exe" set pacman=C:\msys64\usr\bin\pacman.exe
set msysroot=
if defined pacman set "msysroot=!pacman:\usr\bin\pacman.exe=!"

set gcc=
for /f "delims=" %%p in ('where gcc 2^>nul') do if not defined gcc set gcc=%%p
set prefix=mingw-w64-x86_64
set shell=mingw64
if defined gcc (

    echo !gcc! | findstr /i /c:"\ucrt64\" >nul && set prefix=mingw-w64-ucrt-x86_64 && set shell=ucrt64
    echo !gcc! | findstr /i /c:"\clang64\" >nul && set prefix=mingw-w64-clang-x86_64 && set shell=clang64
    echo !gcc! | findstr /i /c:"\mingw32\" >nul && set prefix=mingw-w64-i686 && set shell=mingw32

)

rem
rem Where the AMI tool kit lives: AMIDIR if set (setpath.bat in amitk does
rem not set it, so it is set by hand), else the Makefile's Windows default,
rem the profile folder (a command window has no HOME).
rem
rem The Makefile hands AMIDIR to the compiler unquoted, so a space in the
rem path (as in most USERPROFILE paths) breaks the build. A path with a
rem space is taken by its short name instead, C:\Users\SCOTTF~1\..., which
rem is the same folder without the space; the Makefile does the same for
rem its default. That fails only where short names are turned off, and then
rem the space is reported.
rem
set amiuserdir=0
if defined AMIDIR (

    set amitkdir=%AMIDIR%
    set amiuserdir=1

) else (

    set amitkdir=%USERPROFILE%\projects\amitk

)
set amispaced=0
if not "%amitkdir%" == "%amitkdir: =%" (

    for %%d in ("%amitkdir%") do set amitkdir=%%~sd
    if not "!amitkdir!" == "!amitkdir: =!" set amispaced=1

)
set amirepo=https://github.com/samiam95124/amitk.git

rem ############################################################################
rem #
rem # 1. The build tools
rem #
rem # Each tool maps to the MSYS2 package that carries it. The compiler and
rem # binutils are the toolchain of the shell, the rest are MSYS2's own.
rem #
echo Build tools
set pkg_gcc=%prefix%-toolchain
set pkg_ar=%prefix%-toolchain
set pkg_make=make
set pkg_git=git
set pkg_curl=curl
set pkg_sed=sed
set pkg_grep=grep
set pkg_diff=diffutils
set pkg_tar=tar
set pkg_rm=coreutils
set pkg_cp=coreutils
set pkg_mv=coreutils
set pkg_ls=coreutils

set missingtools=
set "toolpkgs= "
for %%t in (gcc ar make git curl sed grep diff tar rm cp mv ls) do (

    where /q %%t
    if !errorlevel! equ 0 (

        echo     %%t: found

    ) else (

        echo     %%t: not found
        set missingtools=!missingtools! %%t
        echo "!toolpkgs!" | findstr /c:" !pkg_%%t! " >nul || set "toolpkgs=!toolpkgs!!pkg_%%t! "

    )

)
if "%missingtools%" == "" goto toolsdone

if not defined pacman (

    call :warn "Build tools missing, nothing can be compiled:%missingtools%. Install MSYS2 from https://www.msys2.org, then in its shell: pacman -S --needed %toolpkgs%"
    goto toolsdone

)

call :askinstall "the build tools (pacman -S %toolpkgs%)"
if errorlevel 1 (

    call :warn "Build tools missing, nothing can be compiled:%missingtools%. In the MSYS2 shell: pacman -S --needed %toolpkgs%"
    goto toolsdone

)

"%pacman%" -S --needed --noconfirm %toolpkgs%
if errorlevel 1 (

    call :warn "The build tools did not install:%missingtools%"
    goto toolsdone

)

rem
rem Installed, but the tools are only found through the path. Say so when
rem they are still not, and use them from MSYS2 for the rest of this run.
rem
set stillmissing=
for %%t in (%missingtools%) do where /q %%t || set stillmissing=!stillmissing! %%t
if not "%stillmissing%" == "" (

    call :warn "Installed, but not on the PATH:%stillmissing%. Add %msysroot%\%shell%\bin and %msysroot%\usr\bin to the PATH, then run configure again"
    set "PATH=%msysroot%\%shell%\bin;%msysroot%\usr\bin;%PATH%"

)
for /f "delims=" %%p in ('where gcc 2^>nul') do if not defined gcc set gcc=%%p

:toolsdone

rem ############################################################################
rem #
rem # 2. The development packages amitk and the panel link against
rem #
rem # Each is checked by pacman when there is one, and otherwise by compiling
rem # an include of its header, which checks whatever toolchain gcc is.
rem #
echo Development packages
set probe=%TEMP%\fta850_configure_probe.c
set missingreq=
set pkgsinstalled=1
call :checkpkg %prefix%-openssl openssl/ssl.h
call :checkpkg %prefix%-libpng png.h
call :checkpkg %prefix%-zlib zlib.h
if exist "%probe%" del "%probe%"
if "%missingreq%" == "" goto pkgsdone

set pkgsinstalled=0
if not defined pacman (

    call :warn "Packages missing, the build will fail. Install MSYS2 from https://www.msys2.org, then in its shell: pacman -S --needed%missingreq%"
    goto pkgsdone

)

call :askinstall "the packages:%missingreq%"
if errorlevel 1 (

    call :warn "Packages missing, the build will fail: pacman -S --needed%missingreq%"
    goto pkgsdone

)

"%pacman%" -S --needed --noconfirm %missingreq%
if errorlevel 1 (

    call :warn "The packages did not install:%missingreq%"
    goto pkgsdone

)
set pkgsinstalled=1

:pkgsdone

rem ############################################################################
rem #
rem # 3. The AMI tool kit
rem #
rem # Fetched into amitkdir if it is not there, then configured and built if its
rem # libraries are not there. amitk's own configure.bat installs the same
rem # packages as above without asking, so when the packages were declined it
rem # is run with --check, which only reports.
rem #
echo AMI tool kit (amitk) in %amitkdir%
set amiok=0
set amisrc=0
if exist "%amitkdir%\.git\" set amisrc=1
if exist "%amitkdir%\Makefile" set amisrc=1

rem
rem amitk is not fetched into a path that keeps its space (see above), and
rem one already there is reported at the end.
rem
if "%amisrc%" == "1" (

    echo     source: present
    goto amifetched

)

echo     source: not present
if "%amispaced%" == "1" (

    call :warn "amitk would go to %amitkdir%, whose space make cannot pass to the compiler: set AMIDIR to a path without one, such as C:/projects/amitk, then run configure again"
    goto amifetched

)
where /q git
if errorlevel 1 (

    call :warn "No git, so amitk cannot be fetched: nothing will build without it"
    goto amifetched

)

call :askinstall "amitk, cloned from %amirepo%"
if errorlevel 1 (

    call :warn "amitk is not installed: nothing will build without it. git clone %amirepo% %amitkdir%"
    goto amifetched

)

rem
rem The submodules are the curses programs (nano, htop, BSDGames), which the
rem libraries do not need, and their servers are not always up, so they are
rem fetched after the clone and a failure there is only noted.
rem
for %%d in ("%amitkdir%") do set amiparent=%%~dpd
if not exist "%amiparent%" md "%amiparent%"
git clone %amirepo% "%amitkdir%"
if errorlevel 1 (

    call :warn "The clone of amitk failed"
    goto amifetched

)
git -C "%amitkdir%" submodule update --init --recursive
if errorlevel 1 (

    echo     submodules: not all fetched. They are only the curses programs, which the
    echo     libraries do not need. git submodule update --init --recursive in
    echo     %amitkdir% fetches them later.

)

:amifetched

if not exist "%amitkdir%\Makefile" goto amidone

if exist "%amitkdir%\lib\libami_graph.a" if exist "%amitkdir%\lib\libami_plain.a" (

    echo     libraries: built
    set amiok=1
    goto amidone

)

echo     libraries: not built
call :askinstall "amitk's libraries (configure and make in %amitkdir%)"
if errorlevel 1 (

    call :warn "amitk is not built: nothing will build without it. cd %amitkdir%, then configure and make"
    goto amidone

)

set amiconf=
if "%pkgsinstalled%" == "0" set amiconf=--check
pushd "%amitkdir%"
call "%amitkdir%\configure.bat" %amiconf%
if errorlevel 1 (

    popd
    call :warn "The amitk build failed: see the messages above, then cd %amitkdir%, configure and make"
    goto amidone

)
make
if errorlevel 1 (

    popd
    call :warn "The amitk build failed: see the messages above, then cd %amitkdir%, configure and make"
    goto amidone

)
popd
if exist "%amitkdir%\lib\libami_graph.a" if exist "%amitkdir%\lib\libami_plain.a" (

    set amiok=1
    goto amidone

)
call :warn "amitk built, but its libraries are not in %amitkdir%\lib"

:amidone

rem
rem The Makefile finds amitk through AMIDIR, or at the default above. An
rem AMIDIR set by hand wants forward slashes, because make hands it to the
rem MSYS2 shell, and no space: one with a space is pointed at its short name.
rem
if not "%amiok%" == "1" goto amidirdone
set amifwd=%amitkdir:\=/%
if "%amispaced%" == "1" (

    call :warn "amitk is at %amitkdir%, whose space make cannot pass to the compiler, and this drive has no short names: move it to a path without a space, such as C:\projects\amitk, and set AMIDIR to that"
    goto amidirdone

)
if "%amiuserdir%" == "1" if not "%AMIDIR%" == "%amitkdir%" (

    call :warn "AMIDIR has a space in it, which make cannot pass to the compiler: set AMIDIR=%amifwd%, the same folder by its short name"
    goto amidirdone

)

:amidirdone

rem ############################################################################
rem #
rem # 4. Access to the radio's serial port
rem #
rem # The radio is a CDC-ACM device, which the usbser driver that comes with
rem # Windows 10 presents as a COM port, open to everyone. The ports present
rem # now are listed from the registry.
rem #
echo Serial port
echo     no setup needed on Windows: the radio is a COM port when plugged in, in CP mode
set comports=0
for /f "tokens=1,3" %%a in ('reg query HKLM\HARDWARE\DEVICEMAP\SERIALCOMM 2^>nul ^| findstr /i "REG_SZ"') do (

    echo     %%b  ^(%%a^)
    set comports=1

)
if "%comports%" == "0" echo     no COM ports at the moment

rem ############################################################################
rem #
rem # Station data, carried in the repository; "make data" refreshes it
rem #
if exist data\airports.csv if exist data\airport-frequencies.csv if exist data\navaids.csv (

    echo Station data: present, make data refreshes it
    goto datadone

)
call :warn "Station data is missing from data\: the station finder will have nothing to search (make data)"

:datadone

rem ############################################################################
rem #
rem # Summary
rem #
echo.
if "%warned%" == "1" (

    echo Not ready:
    type "%warnfile%"
    del "%warnfile%"
    echo.
    echo Fix these, then run configure again.
    exit /b 1

)
if "%check%" == "1" (

    echo Everything is in place

) else (

    echo Everything is in place: run make, then fta850 ^(or fta850 --sim^)

)
exit /b 0

rem ############################################################################
rem #
rem # Routines
rem #

rem
rem Ask before an install when --prompt was given. The argument is what is
rem about to be installed. Returns 0 to go ahead, 1 to skip. With --check
rem nothing is installed, so the answer is always to skip.
rem
:askinstall
if "%check%" == "1" exit /b 1
if "%ask%" == "0" exit /b 0
choice /c yn /m "Install %~1"
if errorlevel 2 exit /b 1
exit /b 0

rem
rem Note a problem that leaves the program unable to build or run. The lines
rem are printed again in the summary.
rem
:warn
echo *** %~1
>> "%warnfile%" echo     %~1
set warned=1
exit /b 0

rem
rem Check a development package is installed: %1 the MSYS2 package, %2 the
rem header it carries. By pacman when there is one, else by compiling an
rem include of the header, else it cannot be told and counts as missing.
rem Adds the package to the missing list when it is not there.
rem
:checkpkg
set found=0
if defined pacman (

    "%pacman%" -Q %1 >nul 2>&1 && set found=1

) else if defined gcc (

    echo #include ^<%2^> > "%probe%"
    gcc -fsyntax-only "%probe%" >nul 2>&1 && set found=1

)
if "%found%" == "1" (

    echo     %1: installed

) else (

    echo     %1: not installed
    set missingreq=%missingreq% %1

)
exit /b 0
