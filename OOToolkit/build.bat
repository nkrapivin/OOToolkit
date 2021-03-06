SETLOCAL EnableDelayedExpansion

Rem Libraries to link in
set libraries=-lc -lkernel -lc++ -lSceVideoOut -lScePad -lSceUserService -lSceSystemService -lSceFreeType -lSceSysmodule -lSceAudioOut -lvorbis -logg

Rem Read the script arguments into local vars
set intdir=%1
set targetname=%~2
set outputPath=%3
set clangPath=D:\SDKs\LLVM10\bin

echo %intdir%
echo %targetname%
echo %outputPath%

set outputElf=%intdir%\%targetname%.elf
set outputOelf=%intdir%\%targetname%.oelf

@mkdir %intdir%

Rem Compile object files for all the source files
for %%f in (*.cpp) do (
    %clangPath%\clang++ -cc1 -triple x86_64-pc-freebsd-elf -munwind-tables -I"%OO_PS4_TOOLCHAIN%\\include" -I"%OO_PS4_TOOLCHAIN%\\include\\c++\\v1" -fuse-init-array -debug-info-kind=limited -debugger-tuning=gdb -emit-obj -o %intdir%\%%~nf.o %%~nf.cpp
)

Rem Get a list of object files for linking
set obj_files=
for %%f in (%1\\*.o) do set obj_files=!obj_files! .\%%f

Rem Link the input ELF
%clangPath%\ld.lld -m elf_x86_64 -pie --script "%OO_PS4_TOOLCHAIN%\link.x" -Loggvorbis --eh-frame-hdr -o "%outputElf%" "-L%OO_PS4_TOOLCHAIN%\\lib" %libraries% --verbose "%OO_PS4_TOOLCHAIN%\lib\crt1.o" %obj_files%

Rem Create the eboot
%OO_PS4_TOOLCHAIN%\bin\windows\create-eboot.exe -in "%outputElf%" --out "%outputOelf%" --paid 0x3800000000000011

Rem Cleanup
copy "eboot.bin" %outputPath%\eboot.bin
del "eboot.bin"

Rem Build package
echo %outputPath%
set outPathDequot=%outputPath:"=%
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.exe pkg_build %outPathDequot%pkg\pkg.gp4 %outPathDequot%

Rem Send package
set pkgName=IV1337-BREW01010_00-OPENORBISTOOLKIT.pkg
set ps4url=192.168.1.239:12800
set pcurl=192.168.1.218:5000
curl -v "http://%ps4url%/api/install" --data "{""type"":""direct"",""packages"":[""http://%pcurl%/%pkgName%""]}"
