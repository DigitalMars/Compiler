#_ makefile
# Copyright (C) 1984-1994 by Symantec
# Copyright (c) 2000-2012 by Digital Mars
# Written by Walter Bright
# All Rights Reserved
#       for C/C++ compiler parser and preprocessor (SCPP, SPP and HTOD)

# Suffixes:
#       n       WIN32 EXE
#       nd      WIN32 DLL

# Paths:

CBX=.
DMC=\dm
NTDLLSHELL=ntdllshell
TK=tk
INCLUDE=-I$(TK);$(NTDLLSHELL)\include;\dm\include
CC=dmc
LIBNT=$(DMC)\lib
SNN=$(LIBNT)\snn
# Where scp command copies to
SCPDIR=..\backup
SCPDIR=walter@mercury:dm

# Options:
#       TARGET          one of SCPP, SPP, HTOD for C, C++, Preprocessor
#       XFLG            other compiler switches
#       OPT             optimization switches
#       DEBUG           debug switches
#       NTDLL           dll switches
#       PREC            precompiled header switches
#       LFLAGS          flags to DOS linker
#       LINKN           linker to use for target

TARGET=
XFLG=
OPT=
DEBUG=-gl
NTDLL=-WD
PREC=-H -HP32 -HItotal.h -HO
#LFLAGS=/map/co
LFLAGS=/map/e/f/packcode/noe;
BASE=

##### Tools

# D compiler
DMD=e:\dmd2.074\windows\bin\dmd
# C++ compiler
CC=dmc
# Make program
MAKE=make
# Librarian
LIB=lib
# Delete file(s)
DEL=del
# Make directory
MD=mkdir
# Remove directory
RD=rmdir
# File copy
CP=cp
# De-tabify
DETAB=detab
# Convert line endings to Unix
TOLF=tolf
# Zip
ZIP=zip32
# Copy to another directory
SCP=$(CP)
SCP=\putty\pscp -i c:\.ssh\colossus.ppk
# PVS-Studio command line executable
PVS="c:\Program Files (x86)\PVS-Studio\x64\PVS-Studio"

LINKN=$(DMC)\bin\link /de

CFLAGS=$(INCLUDE) $(XFLG) $(OPT) $(DEBUG) -6 -D$(TARGET) -r -cpp

# Makerules:
.c.obj:
        $(CC) -c $(CFLAGS) $(PREC) $*

.asm.obj:
        $(CC) -c $(CFLAGS) $*

################ DUMMY #########################

noparameter:
        echo no parameter


################ RELEASES #########################

release:
        make clean
        make scppn
        make clean
        make sppn
        make clean
        make scppnd
        make clean
        make sppnd
        make clean
        make htodn
        make clean

################ NT COMMAND LINE RELEASE #########################

scppn:
#       make TARGET=SCPP OPT=-o "DEBUG=-gt -g -DSTATIC= -Nc" LFLAGS=/noi/noe/map scppn.exe
#       make TARGET=SCPP OPT=-o "DEBUG=-g -DSTATIC= -Nc" LFLAGS=/noi/noe/map/co scppn.exe
        make TARGET=SCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map scppn.exe

sppn:
        make TARGET=SPP  OPT=-o+space DEBUG= LFLAGS=/noi/noe/map sppn.exe

htodn:
        make TARGET=HTOD XFLG=-DSCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map htodn.exe

################ NT COMMAND LINE DEBUG #########################

debcppn:
        make TARGET=SCPP OPT= "DEBUG=-D -g -DMEM_DEBUG" LFLAGS=/noi/noe/co/m scppn.exe

debppn:
        make TARGET=SPP  OPT= "DEBUG=-D -g -DMEM_DEBUG" LFLAGS=/noi/noe/co/m sppn.exe

debhtodn:
        make TARGET=HTOD XFLG=-DSCPP  OPT= "DEBUG=-D -g -DMEM_DEBUG" LFLAGS=/noi/noe/co/m htodn.exe

################ NT DLL RELEASE #########################

scppnd:
        make TARGET=SCPP OPT=-o+space "XFLG=$(NTDLL)" DEBUG= LFLAGS=/noi/noe/map BASE=18087936 scppnd.dll
#       make TARGET=SCPP OPT=-o+space "XFLG=$(NTDLL)" DEBUG=-g LFLAGS=/noi/noe/map/co BASE=18087936 scppnd.dll

sppnd:
        make TARGET=SPP  OPT=-o+space "XFLG=$(NTDLL)" DEBUG= LFLAGS=/noi/noe/map BASE=21430272 sppnd.dll

################ NT DLL DEBUG #########################

debcppnd:
        make TARGET=SCPP OPT= "DEBUG=-g -D -DMEM_DEBUG -DTERMCODE" "XFLG=$(NTDLL)" LFLAGS=/noi/noe/map/co scppnd.dll

debppnd:
        make TARGET=SPP  OPT=-o+space "XFLG=$(NTDLL)" LFLAGS=/noi/noe/map/co sppnd.dll

#########################################

OBJ1a=blklst.obj blockopt.obj cpp.obj debug.obj ee.obj el.obj enum.obj err.obj
OBJ1b=evalu8.obj eh.obj exp.obj exp2.obj file.obj filename.obj func.obj adl.obj
OBJ1c=getcmd.obj ini.obj init.obj inline.obj loadline.obj msc.obj
OBJ1d=nspace.obj nwc.obj os.obj out.obj ph.obj ppexp.obj pragma.obj pseudo.obj
OBJ1e=struct.obj symbol.obj template.obj tk.obj token.obj type.obj dt.obj
OBJ1f=var.obj nteh.obj rtlsym.obj rtti.obj scope.obj tdbx.obj util.obj html.obj
OBJ1g=unialpha.obj entity.obj aa.obj ti_achar.obj ti_pvoid.obj backconfig.obj

OBJ2a= cgcod.obj cod1.obj cod2.obj cod3.obj cod4.obj cod5.obj cg87.obj cgxmm.obj code.obj
OBJ2b= cg.obj cgelem.obj cgcs.obj cgcv.obj tytostr.obj cgen.obj cgreg.obj compress.obj
OBJ2c= cgobj.obj cgsched.obj objrecor.obj iasm.obj ptrntab.obj newman.obj dwarf.obj
OBJ2d= go.obj gloop.obj gother.obj gflow.obj gdag.obj glocal.obj gsroa.obj pdata.obj cv8.obj
OBJ2e= errmsgs2.obj outbuf.obj bcomplex.obj htod.obj speller.obj utf.obj divcoeff.obj

ALLOBJS= $(OBJ1a) $(OBJ1b) $(OBJ1c) $(OBJ1d) $(OBJ1e) $(OBJ1f) $(OBJ1g) \
         $(OBJ2a) $(OBJ2b) $(OBJ2c) $(OBJ2d) $(OBJ2e)

#########################################

$(TARGET)n.exe : $(ALLOBJS) $(TARGET)n.lnk $(TARGET)n.def
        $(LINKN) @$*.lnk

$(TARGET)nd.dll : $(ALLOBJS) $(TARGET)nd.lnk $(TARGET)nd.def
        $(LINKN) @$*.lnk

#########################################

$(TARGET)n.def : makefile
        echo NAME $(TARGET)                     >  $*.def
        echo SUBSYSTEM CONSOLE                  >> $*.def
        echo EXETYPE NT                         >> $*.def
        echo CODE SHARED EXECUTE                >> $*.def
        echo STUB '$(LIBNT)\NTSTUB.EXE'         >> $*.def
        echo INCLUDE $(TARGET)n.tra             >> $*.def

$(TARGET)nd.def : makefile
        echo LIBRARY "$*.dll"                           > $*.def
        echo DESCRIPTION '$(TARGET) as a DLL'           >> $*.def
        echo EXETYPE NT                                 >> $*.def
        echo SUBSYSTEM WINDOWS                          >> $*.def
        echo CODE SHARED EXECUTE                        >> $*.def
        echo DATA WRITE                                 >> $*.def
        echo EXPORTS                                    >> $*.def
        echo   _$(TARGET)Version=_DllVersion@0  @1      >> $*.def
        echo   _$(TARGET)Entry=_DllEntry@12     @2      >> $*.def
        echo   _NetSpawnVersion=_NetSpawnVersion@0 @3   >> $*.def
        #echo   _NetSpawnVersion@0              @4      >> $*.def

########################################

$(TARGET)n.lnk : makefile
        echo $(OBJ1a)+                          >  $*.lnk
        echo $(OBJ1b)+                          >> $*.lnk
        echo $(OBJ1c)+                          >> $*.lnk
        echo $(OBJ1d)+                          >> $*.lnk
        echo $(OBJ1e)+                          >> $*.lnk
        echo $(OBJ1f)+                          >> $*.lnk
        echo $(OBJ1g)+                          >> $*.lnk
        echo $(OBJ2a)+                          >> $*.lnk
        echo $(OBJ2b)+                          >> $*.lnk
        echo $(OBJ2c)+                          >> $*.lnk
        echo $(OBJ2d)+                          >> $*.lnk
        echo $(OBJ2e)$(LFLAGS)                  >> $*.lnk
        echo $*                                 >> $*.lnk
        echo $*                                 >> $*.lnk
        echo tdb+                               >> $*.lnk
        echo $(SNN)+                            >> $*.lnk
        echo $(LIBNT)\kernel32+                 >> $*.lnk
        echo $(LIBNT)\user32                    >> $*.lnk
        echo $*.def;                            >> $*.lnk

$(TARGET)nd.lnk : makefile
        echo $(NTDLLSHELL)\lib\netsploc+        >  $*.lnk
        echo $(OBJ1a)+                          >> $*.lnk
        echo $(OBJ1b)+                          >> $*.lnk
        echo $(OBJ1c)+                          >> $*.lnk
        echo $(OBJ1d)+                          >> $*.lnk
        echo $(OBJ1e)+                          >> $*.lnk
        echo $(OBJ1f)+                          >> $*.lnk
        echo $(OBJ1g)+                          >> $*.lnk
        echo $(OBJ2a)+                          >> $*.lnk
        echo $(OBJ2b)+                          >> $*.lnk
        echo $(OBJ2c)+                          >> $*.lnk
        echo $(OBJ2d)+                          >> $*.lnk
        echo $(OBJ2e)$(LFLAGS)/base:$(BASE)     >> $*.lnk
        echo $*.dll                             >> $*.lnk
        echo $*                                 >> $*.lnk
        echo $(NTDLLSHELL)\lib\spwnlnd+         >> $*.lnk
        echo tdb+                               >> $*.lnk
        echo $(SNN)+                            >> $*.lnk
        echo $(LIBNT)\kernel32+                 >> $*.lnk
        echo $(LIBNT)\user32                    >> $*.lnk
        echo $*.def;                            >> $*.lnk

####################################################

test.obj : test.c

##################### INCLUDE MACROS #####################

CCH= cc.h cdef.h ty.h outbuf.h $(TK)\mem.h $(TK)\list.h $(TK)\vec.h
TOTALH=$(CCH) total.sym

##################### GENERATED SOURCE #####################

elxxx.c cdxxx.c optab.c debtab.c fltables.c tytab.c : \
        cdef.h cc.h oper.h ty.h optabgen.c
        $(CC) -cpp optabgen -I$(TK)
        optabgen

msgs2.d msgs2.h msgs2.c : msgsx.exe
        msgsx

msgsx.exe : msgsx.c
        dmc msgsx -D$(TARGET)

##################### ZIP ################################

RELEASE= sc.exe scppn.exe sppn.exe scd.dll scppnd.dll sppnd.dll

SRC=    tassert.h html.h html.c \
        ini.c cgcv.h cv4.h tdb.h loadline.d ppexp.c errmsgs2.c tdbx.c \
        allocast.h go.h iasm.h cg.c scdll.h objrecor.c rtlsym.c util.c \
        total.h exh.h cpp.h iasm.c type.c enum.c nspace.c err.c \
        filename.c blklst.c tytostr.c ee.c symbol.c \
        msgsx.c eh.c template.c rtti.c scope.h scope.c token.h outbuf.h \
        outbuf.c trace.c dt.h file.c parser.h cod5.c type.h newman.c out.c \
        ty.h glocal.c pseudo.c init.c nteh.c \
        code.c el.c el.h ptrntab.c cpp.c func.c debug.c struct.c evalu8.c os.c \
        exp2.c blockopt.c exp.c gdag.c gflow.c gother.c global.h oper.h \
        optabgen.c cgcs.c cgcv.c go.c var.c cod2.c cc.h gloop.c msc.c cod3.c \
        code.h cgsched.c cgen.c ph.c cgreg.c inline.c cgcod.c token.c \
        pragma.c cod1.c cg87.c cgxmm.c cod4.c cgobj.c nwc.c getcmd.c tk.c cdef.h \
        cgelem.c rtlsym.h dt.c strtold.c \
        bcomplex.h bcomplex.c adl.c castab.c unialpha.c entity.c \
        aa.h aa.c tinfo.h ti_achar.c htod.c md5.h speller.h speller.c utf.h utf.c \
        ti_pvoid.c xmm.h obj.h code_stub.h code_x86.h platform_stub.c pdata.c cv8.c \
	backconfig.c ph2.c util2.c divcoeff.d dwarf.h dwarf.c dwarfeh.c \
	elfobj.c elfobj1.c machobj.c compress.c varstats.h varstats.c gsroa.c

TKSRC= $(TK)\mem.h $(TK)\list.h $(TK)\vec.h $(TK)\filespec.h \
        $(TK)\mem.c $(TK)\list.c $(TK)\vec.c $(TK)\filespec.c

SHELLSRC= $(NTDLLSHELL)\include\dllrun.h $(NTDLLSHELL)\include\network.h \
        $(NTDLLSHELL)\include\nsidde.h $(NTDLLSHELL)\include\netspawn.h

LIBSRC= $(NTDLLSHELL)\lib\spwnlnd.lib $(NTDLLSHELL)\lib\netsploc.obj

BUILDSRC= sppn.tra sccn.tra htodn.tra scppn.tra makefile posix.mak

LICENSE= license.txt gpl.txt

# D back end
BACKSRC= cdef.h cc.h oper.h ty.h optabgen.c \
	global.h code.h code_x86.h code_stub.h platform_stub.c \
	type.h dt.h cgcv.h \
	el.h iasm.h rtlsym.h \
	bcomplex.c blockopt.c cg.c cg87.c cgxmm.c \
	cgcod.c cgcs.c cgcv.c cgelem.c cgen.c cgobj.c \
	cgreg.c var.c compress.c \
	cgsched.c cod1.c cod2.c cod3.c cod4.c cod5.c \
	code.c symbol.c debug.c dt.c ee.c el.c \
	evalu8.c go.c gflow.c gdag.c gsroa.c \
	gother.c glocal.c gloop.c newman.c \
	nteh.c os.c out.c outbuf.c ptrntab.c rtlsym.c \
	type.c melf.h mach.h mscoff.h bcomplex.h \
	cdeflnx.h outbuf.h token.h tassert.h \
	elfobj.c cv4.h dwarf2.h exh.h go.h \
	dwarf.c dwarf.h machobj.c \
	strtold.c aa.h aa.c tinfo.h ti_achar.c \
	md5.h md5.c ti_pvoid.c xmm.h ph2.c util2.c \
	mscoffobj.c obj.h pdata.c cv8.c backconfig.c \
	divcoeff.d dwarfeh.c varstats.h varstats.c compress.c \
	backend.txt

VERSION=857

zip:
        del dm$(VERSION).zip
        zip32 -u dm$(VERSION) $(RELEASE)

zipsrc:
        del dm$(VERSION)src.zip
        zip32 -u dm$(VERSION)src $(SRC) tdb.lib $(LICENSE) backend.txt
        zip32 -u dm$(VERSION)src $(TKSRC)
        zip32 -u dm$(VERSION)src $(SHELLSRC)
        zip32 -u dm$(VERSION)src $(LIBSRC)
        zip32 -u dm$(VERSION)src $(BUILDSRC)

detab:
        detab $(SRC) $(TKSRC) $(SHELLSRC)

tolf:
        tolf $(SRC) $(TKSRC) $(SHELLSRC) $(BUILDSRC) $(LICENSE)

install:
        cp $(SRC) $(BUILDSRC) tdb.lib $(LICENSE) \dm\src\dmc
        cp $(TKSRC) \dm\src\dmc\tk
        cp $(SHELLSRC) \dm\src\dmc\ntdllshell\include
        cp $(LIBSRC) \dm\src\dmc\ntdllshell\lib

installbin:
        cp scppn.exe sppn.exe scppnd.dll sppnd.dll htodn.exe \dm\bin

git: detab tolf $(BUILDSRC)
        $(SCP) $(SRC) $(BUILDSRC) tdb.lib $(LICENSE) $(SCPDIR)/Compiler/dm/src/dmc/
        $(SCP) $(TKSRC) $(SCPDIR)/Compiler/dm/src/dmc/tk
        $(SCP) $(SHELLSRC) $(SCPDIR)/Compiler/dm/src/dmc/ntdllshell/include
        $(SCP) $(LIBSRC) $(SCPDIR)/Compiler/dm/src/dmc/ntdllshell/lib

gitback: detab tolf $(BACKSRC)
        $(SCP) $(BACKSRC) walter@mercury:forks/dmd/src/backend/


##################### SPECIAL BUILDS #####################

total.sym : $(CCH) token.h parser.h el.h scope.h global.h obj.h type.h oper.h cpp.h $(TK)\filespec.h dt.h msgs2.h bcomplex.h total.h
        $(CC) -c $(CFLAGS) -HFtotal.sym total.h

os.obj : os.c
        $(CC) -c $(CFLAGS) os.c

strtold.obj : strtold.c
        $(CC) -c -o strtold.c

divcoeff.obj : divcoeff.d
        $(DMD) -c -O -release -betterC divcoeff.d

loadline.obj : loadline.d msgs2.d
        $(DMD) -c -O -release -betterC loadline.d

################# Source file dependencies ###############

DEP=total.sym msgs2.h msgs2.c elxxx.c cdxxx.c optab.c debtab.c fltables.c tytab.c

aa.obj : aa.h tinfo.h aa.c
backconfig.obj : $(DEP) $(backconfig.dep) backconfig.c
bcomplex.obj : bcomplex.h bcomplex.c
blklst.obj : $(DEP) $(blklst.dep) blklst.c
blockopt.obj : $(DEP) $(blockopt.dep) blockopt.c
cg.obj : $(DEP) $(cg.dep) cg.c
cgcs.obj : $(DEP) $(cgcs.dep) cgcs.c
cgobj.obj : $(DEP) $(cgobj.dep) cgobj.c
cgreg.obj : $(DEP) $(cgreg.dep) cgreg.c
cod1.obj : $(DEP) $(cod1.dep) cod1.c
cod2.obj : $(DEP) $(cod2.dep) cod2.c
cod3.obj : $(DEP) $(cod3.dep) cod3.c
cod4.obj : $(DEP) $(cod4.dep) cod4.c
cod5.obj : $(DEP) $(cod5.dep) cod5.c
code.obj : $(DEP) $(code.dep) code.c
compressj.obj : $(DEP) $(compress.dep) compress.c
cv8.obj : $(DEP) $(cv8.dep) cv8.c
debug.obj : $(DEP) $(debug.dep) debug.c
dt.obj : $(DEP) $(dt.dep) dt.c
dwarf.obj : $(DEP) $(dwarf.dep) dwarf.h dwarf.c
dwarfeh.obj : $(DEP) $(dwarfeh.dep) dwarf.h dwarfeh.c
ee.obj : $(DEP) $(ee.dep) ee.c
eh.obj : $(DEP) $(eh.dep) eh.c
el.obj : $(DEP) $(el.dep) el.c
enum.obj : $(DEP) $(enum.dep) enum.c
errmsgs2.obj : $(DEP) $(errmsgs2.dep) errmsgs2.c msgs2.c
evalu8.obj : $(DEP) $(evalu8.dep) evalu8.c
filename.obj : $(DEP) $(filename.dep) filename.c
glocal.obj : $(DEP) $(glocal.dep) glocal.c
gloop.obj : $(DEP) $(gloop.dep) gloop.c
htod.obj : $(DEP) $(htod.dep) htod.c
html.obj : $(DEP) $(html.dep) html.c
init.obj : $(DEP) $(init.dep) init.c
inline.obj : $(DEP) $(inline.dep) inline.c
msc.obj : $(DEP) $(msc.dep) msc.c
nteh.obj : $(DEP) $(nteh.dep) nteh.c
objrecor.obj : $(DEP) $(objrecor.dep) objrecor.c
out.obj : $(DEP) $(out.dep) out.c
pdata.obj : $(DEP) $(pdata.dep) pdata.c
pragma.obj : $(DEP) $(pragma.dep) pragma.c
pseudo.obj : $(DEP) $(pseudo.dep) pseudo.c
ptrntab.obj : $(DEP) $(ptrntab.dep) iasm.h ptrntab.c
speller.obj : speller.h speller.c
template.obj : $(DEP) $(template.dep) template.c
ti_achar.obj : tinfo.h ti_achar.c
ti_pvoid.obj : tinfo.h ti_pvoid.c
tk.obj : $(DEP) $(tk.dep) tk.c
type.obj : $(DEP) $(type.dep) type.c
utf.obj : utf.h utf.c
util.obj : $(DEP) $(util.dep) util.c
cgcv.obj : $(DEP) $(cgcv.dep) cgcv.c
cgelem.obj : $(DEP) $(cgelem.dep) cgelem.c
gdag.obj : $(DEP) $(gdag.dep) gdag.c
getcmd.obj : $(DEP) $(getcmd.dep) getcmd.c
gother.obj : $(DEP) $(gother.dep) gother.c
gsroa.obj : $(DEP) $(gsroa.dep) gsroa.c
exp2.obj : $(DEP) $(exp2.dep) castab.c exp2.c
err.obj : $(DEP) $(err.dep) err.c
func.obj : $(DEP) $(func.dep) func.c
adl.obj : $(DEP) $(adl.dep) adl.c
nspace.obj : $(DEP) $(nspace.dep) nspace.c
nwc.obj : $(DEP) $(nwc.dep) nwc.c
iasm.obj : $(DEP) $(iasm.dep) iasm.h iasm.c
go.obj : $(DEP) $(go.dep) go.c
struct.obj : $(DEP) $(struct.dep) struct.c
gflow.obj : $(DEP) $(gflow.dep) gflow.c
cgcod.obj : $(DEP) $(cgcod.dep) cgcod.c
cgen.obj : $(DEP) $(cgen.dep) cgen.c
ph.obj : $(DEP) $(ph.dep) ph.c
token.obj : $(DEP) $(token.dep) token.c
rtlsym.obj : $(DEP) $(rtlsym.dep) rtlsym.c
symbol.obj : $(DEP) $(symbol.dep) symbol.c
tytostr.obj : $(DEP) $(tytostr.dep) tytostr.c
cg87.obj : $(DEP) $(cg87.dep) cg87.c
cgxmm.obj : $(DEP) xmm.h $(cgxmm.dep) cgxmm.c
cpp.obj : $(DEP) $(cpp.dep) cpp.c
exp.obj : $(DEP) $(exp.dep) exp.c
file.obj : $(DEP) $(file.dep) file.c
outbuf.obj : $(DEP) $(outbuf.dep) outbuf.c
ppexp.obj : $(DEP) $(ppexp.dep) ppexp.c
newman.obj : $(DEP) $(newman.dep) newman.c
rtti.obj : $(DEP) $(rtti.dep) rtti.c
scope.obj : $(DEP) $(scope.dep) scope.c
tdbx.obj : $(DEP) $(tdbx.dep) tdbx.c
var.obj : $(DEP) $(var.dep) var.c
unialpha.obj : unialpha.c

################### Utilities ################

clean:
        del *.obj
        del *.lnk
        del *.sym
        del *.dep
        del *.def
#       del *.bak
        del msgsx.exe
        del optabgen.exe
        del *.map
        del *.tmp
        del *.lst
        del *.exp
        del *.dbg
        del *.res
        del msgs.c msgs2.d msgs2.h msgs2.c elxxx.c cdxxx.c optab.c debtab.c fltables.c tytab.c

###################################
